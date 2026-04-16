#include <assert.h>
#include <rtems/extension.h>
#include <rtems/rtems/attr.h>
#include <rtems/rtems/cache.h>
#include <rtems/rtems/status.h>
#include <rtems/rtems/tasks.h>
#include <stdint.h>

#include "experiment_common.h"
#include "expr_configure.h"

/*=====================================================================
 * Experiment 4: Working-set Overlap — L1 / L2 separation
 *
 * W-1: Solo (WS_L1_FIT, Core 0)                    — no interference
 * W-2: L1 thrashing (WS_L1_FIT x2, Core 0 RR)     — L1 thrashing
 * W-3: L1 independent (WS_L1_FIT x2, diff cores)   — no L1 interference
 * W-4: L2 competition (WS_L2_QUARTER x2, diff cores)— L2 competition
 * W-5: L2 heavy (WS_L2_PRESSURE x2, diff cores)    — L2 heavy pressure
 * W-6: L1 safe (WS_L1_FIT/2 x2, Core 0 RR)        — within L1
 *=====================================================================*/

static volatile uint8_t exp4_task_e_buf[WS_FULL_SIZE]
  __attribute__((aligned(4096)));

static volatile uint8_t exp4_task_f_buf[WS_FULL_SIZE]
  __attribute__((aligned(4096)));

typedef struct
{
  volatile uint8_t * array;
  int size;
  rtems_id done_sem;
  task_stats_t * stats;
  int expected_cpu;
  int task_name_idx;
} exp4_task_arg_t;

static task_stats_t exp4_stats[2];
static exp4_task_arg_t exp4_args[2];

static rtems_task exp4_periodic_task(rtems_task_argument arg)
{
  exp4_task_arg_t * ta = (exp4_task_arg_t *)(uintptr_t)arg;
  rtems_id period_id;
  rtems_status_code status;

  char idx = (char)('0' + ta->task_name_idx);
  rtems_rate_monotonic_create(rtems_build_name('E', '4', 'P', '0'), &period_id);

  int sweeps = ta->stats->role == TASK_ROLE_WORKER
                 ? NUM_STRESS_ITERATIONS_WORKER
                 : NUM_STRESS_ITERATIONS_INTERFERER;

  uint64_t loop_overhead_start = rtems_clock_get_uptime_nanoseconds();
  empty_workload(ta->array, ta->size, sweeps);
  uint64_t loop_overhead_end = rtems_clock_get_uptime_nanoseconds();
  ta->stats->loop_overhead_ns = loop_overhead_end - loop_overhead_start;

  uint64_t start_time = rtems_clock_get_uptime_nanoseconds();
  for (int iter = 0; iter < NUM_TASK_ITERATIONS; iter++)
  {
    if (rtems_rate_monotonic_period(period_id, TASK_PERIOD_TICKS) ==
        RTEMS_TIMEOUT)
    {
      printf("Task %d missed period at iteration %d.\n", ta->task_name_idx,
             iter);
      exit(1);
    }
    assert((int)rtems_scheduler_get_processor() == ta->expected_cpu);

    uint64_t t0 = rtems_clock_get_uptime_nanoseconds();
    if (ta->stats->op_type == LOAD_OP)
    {
      load_workload(ta->array, ta->size, sweeps);
    }
    else
    {
      store_workload(ta->array, ta->size, sweeps);
    }
    uint64_t t1 = rtems_clock_get_uptime_nanoseconds();

    record_tat(ta->stats, t1 - t0);
  }
  uint64_t end_time = rtems_clock_get_uptime_nanoseconds();
  ta->stats->total_exec_ns = end_time - start_time;

  if ((status = rtems_rate_monotonic_delete(period_id)) != 0) exit(1);
  rtems_semaphore_release(ta->done_sem);
  rtems_task_exit();
}

static void run_single(volatile uint8_t * buf, int size, int cpu,
                       const char * label)
{
  rtems_id sem_id, task_id;

  printf("--- %s ---\n", label);
  memset((void *)buf, 0, (size_t)size);
  memset(&exp4_stats[0], 0, sizeof(task_stats_t));

  /* Worker task */
  exp4_stats[0].role = TASK_ROLE_WORKER;
  exp4_stats[0].cpu_id = cpu;
  exp4_stats[0].task_idx = 0;
  exp4_stats[0].op_type = LOAD_OP;
  exp4_stats[0].n_accesses =
    NUM_STRESS_ITERATIONS_WORKER * (size / L1_LINE_SIZE);

  /* Interferer task */
  exp4_stats[1].role = TASK_ROLE_INTERFERER;
  exp4_stats[1].cpu_id = cpu;
  exp4_stats[1].task_idx = 1;
  exp4_stats[1].op_type = LOAD_OP;
  exp4_stats[1].n_accesses =
    NUM_STRESS_ITERATIONS_INTERFERER * (size / L1_LINE_SIZE);

  rtems_semaphore_create(rtems_build_name('D', 'N', '4', '0'), 0,
                         RTEMS_SIMPLE_BINARY_SEMAPHORE, 0, &sem_id);

  exp4_args[0].array = buf;
  exp4_args[0].size = size;
  exp4_args[0].done_sem = sem_id;
  exp4_args[0].stats = &exp4_stats[0];
  exp4_args[0].expected_cpu = cpu;
  exp4_args[0].task_name_idx = 0;

  rtems_task_create(rtems_build_name('W', 'K', '4', '0'), TASK_PRIORITY_WORKER,
                    TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT, &task_id);
  SET_AFFINITY(task_id, cpu);
  rtems_task_start(task_id, exp4_periodic_task,
                   (rtems_task_argument)(uintptr_t)&exp4_args[0]);

  rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
  print_task_stats(&exp4_stats[0], label);

  rtems_task_delete(task_id);
  rtems_semaphore_delete(sem_id);
  printf("--- %s: Done ---\n", label);
}

static void run_pair(volatile uint8_t * w_buf, int w_size, int w_cpu,
                     volatile uint8_t * i_buf, int i_size, int i_cpu,
                     const char * label)
{
  rtems_id sem_id, task_e, task_f;

  printf("--- %s ---\n", label);
  memset((void *)w_buf, 0, (size_t)w_size);
  memset((void *)i_buf, 0, (size_t)i_size);
  memset(exp4_stats, 0, sizeof(exp4_stats));

  /* Worker task */
  exp4_stats[0].role = TASK_ROLE_WORKER;
  exp4_stats[0].cpu_id = w_cpu;
  exp4_stats[0].task_idx = 0;
  exp4_stats[0].op_type = LOAD_OP;
  exp4_stats[0].n_accesses =
    NUM_STRESS_ITERATIONS_WORKER * (w_size / L1_LINE_SIZE);

  /* Interferer task */
  exp4_stats[1].role = TASK_ROLE_INTERFERER;
  exp4_stats[1].cpu_id = i_cpu;
  exp4_stats[1].task_idx = 1;
  exp4_stats[1].op_type = LOAD_OP;
  exp4_stats[1].n_accesses =
    NUM_STRESS_ITERATIONS_INTERFERER * (i_size / L1_LINE_SIZE);

  rtems_semaphore_create(rtems_build_name('D', 'N', '4', '1'), 0,
                         RTEMS_COUNTING_SEMAPHORE, 0, &sem_id);

  exp4_args[0].array = w_buf;
  exp4_args[0].size = w_size;
  exp4_args[0].done_sem = sem_id;
  exp4_args[0].stats = &exp4_stats[0];
  exp4_args[0].expected_cpu = w_cpu;
  exp4_args[0].task_name_idx = 0;

  exp4_args[1].array = i_buf;
  exp4_args[1].size = i_size;
  exp4_args[1].done_sem = sem_id;
  exp4_args[1].stats = &exp4_stats[1];
  exp4_args[1].expected_cpu = i_cpu;
  exp4_args[1].task_name_idx = 1;

  rtems_task_create(rtems_build_name('T', 'E', '4', '0'), TASK_PRIORITY_WORKER,
                    TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT, &task_e);
  SET_AFFINITY(task_e, w_cpu);

  rtems_task_create(rtems_build_name('T', 'F', '4', '0'), TASK_PRIORITY_WORKER,
                    TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT, &task_f);
  SET_AFFINITY(task_f, i_cpu);

  rtems_task_start(task_e, exp4_periodic_task,
                   (rtems_task_argument)(uintptr_t)&exp4_args[0]);
  rtems_task_start(task_f, exp4_periodic_task,
                   (rtems_task_argument)(uintptr_t)&exp4_args[1]);

  rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
  rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

  print_task_stats(&exp4_stats[0], label);
  print_task_stats(&exp4_stats[1], label);
  rtems_semaphore_delete(sem_id);
  printf("--- %s: Done ---\n", label);
}

/* W-1: Solo, WS_L1_FIT, Core 0 */
void run_exp4_W1(void)
{
  run_single(exp4_task_e_buf, WS_L1_FIT, 0, "EXP4 W-1: Solo (L1 fit, Core 0)");
}

/* W-2: L1 thrashing, same core */
void run_exp4_W2(void)
{
  run_pair(exp4_task_e_buf, WS_L1_FIT, 0, exp4_task_f_buf, WS_L1_FIT, 0,
           "EXP4 W-2: L1 thrashing (Core 0 RR)");
}

/* W-3: L1 independent, different cores */
void run_exp4_W3(void)
{
  run_pair(exp4_task_e_buf, WS_L1_FIT, 0, exp4_task_f_buf, WS_L1_FIT, 1,
           "EXP4 W-3: L1 independent (Core 0+1)");
}

/* W-4: L2 competition, different cores */
void run_exp4_W4(void)
{
  run_pair(exp4_task_e_buf, WS_L2_QUARTER, 0, exp4_task_f_buf, WS_L2_QUARTER, 1,
           "EXP4 W-4: L2 competition (Core 0+1)");
}

/* W-5: L2 heavy pressure, different cores */
void run_exp4_W5(void)
{
  run_pair(exp4_task_e_buf, WS_L2_PRESSURE, 0, exp4_task_f_buf, WS_L2_PRESSURE,
           1, "EXP4 W-5: L2 heavy pressure (Core 0+1)");
}

/* W-6: L1 safe, combined fits in L1 */
void run_exp4_W6(void)
{
  run_pair(exp4_task_e_buf, WS_L1_FIT / 2, 0, exp4_task_f_buf, WS_L1_FIT / 2, 0,
           "EXP4 W-6: L1 safe (half each, Core 0 RR)");
}

void run_exp4_workingset(void)
{
  printf("=== EXP4: Working-set Overlap ===\n\n");
  (void)exp4_task_e_buf[0];
  (void)exp4_task_f_buf[0];

#ifdef RUN_W1
  run_exp4_W1();
#endif

#ifdef RUN_W2
  run_exp4_W2();
#endif

#ifdef RUN_W3
  run_exp4_W3();
#endif

#ifdef RUN_W4
  run_exp4_W4();
#endif

#ifdef RUN_W5
  run_exp4_W5();
#endif

#ifdef RUN_W6
  run_exp4_W6();
#endif

  printf("\n=== EXP4: Complete ===\n\n");
}