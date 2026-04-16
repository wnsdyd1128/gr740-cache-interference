#include <assert.h>
#include <rtems/extension.h>
#include <rtems/rtems/attr.h>
#include <rtems/rtems/cache.h>
#include <rtems/rtems/status.h>
#include <rtems/rtems/tasks.h>
#include <stdint.h>

#include "experiment_common.h"
#include "expr_configure.h"

/** =====================================================================
 * Experiment 2: False Sharing
 *
 * Purpose: quantify the TAT overhead caused by false sharing — two tasks
 * on different cores accessing distinct variables that share the same
 * cache line, triggering repeated cross-core write-invalidations.
 *
 * GR740 cache topology:
 *   L1 per core : 16 KiB, 4-way, 32 B line, write-through
 *   L2 shared   :  2 MiB, 4-way, 32 B line
 *
 * Data layout:
 *
 *   fs_elem_t (sizeof == L1_LINE_SIZE == 32 B):
 *     [worker_val:4B][intf_val:4B][pad:24B]  ← one cache line
 *
 *   fs_array[FS_ELEM_COUNT]: WS_L1_FIT bytes total.
 *   Task A pointer : &fs_array[0].worker_val   stride = L1_LINE_SIZE
 *   Task B pointer : &fs_array[0].intf_val     stride = L1_LINE_SIZE
 *     → each access pair shares the same cache line → false sharing
 *
 *   No-sharing baseline: two independent WS_L1_FIT arrays
 *   (ns_worker_buf, ns_intf_buf), each aligned to 4096 B.
 *   → guaranteed separate cache lines, bus bandwidth contention only.
 *
 * Scenario matrix:
 *   F-1  False Sharing   fs_array   Core 0 + Core 1  RMW  cross-core
 *invalidation F-2  No Sharing      ns_*_buf   Core 0 + Core 1  RMW  independent
 *arrays, bus BW only F-3  Solo Baseline   fs_array   Core 1 only      RMW  zero
 *interference reference
 *
 * CAAS limitation demonstrated:
 *   Both F-1 and F-2 tasks have identical per-task WS (WS_L1_FIT).
 *   CAAS computes the same CA_i for both scenarios and cannot
 *   distinguish the false-sharing layout from the padded layout.
 *   The TAT delta (F-1 − F-2) = pure false sharing cost, invisible to CAAS.
 *===================================================================== **/

/* fs_elem_t: worker_val and intf_val share one 32-byte cache line.
 * sizeof(fs_elem_t) == L1_LINE_SIZE is required so that the
 * existing workload functions (stride = L1_LINE_SIZE) land exactly
 * on the next element's same field with each iteration step. */
typedef struct
{
  volatile uint32_t worker_val; /* offset  0 ~  3 */
  volatile uint32_t intf_val;   /* offset  4 ~  7 ← same 32-B cache line */
  uint8_t pad[L1_LINE_SIZE - 2 * sizeof(uint32_t)]; /* offset 8 ~ 31 */
} __attribute__((aligned(L1_LINE_SIZE))) fs_elem_t;

#define FS_ELEM_COUNT (WS_L1_FIT / sizeof(fs_elem_t))

static fs_elem_t fs_array[FS_ELEM_COUNT] __attribute__((aligned(4096)));

/* No-sharing buffers: fully independent cache lines for each task */
static volatile uint8_t ns_worker_buf[WS_FULL_SIZE]
  __attribute__((aligned(4096)));
static volatile uint8_t ns_intf_buf[WS_FULL_SIZE]
  __attribute__((aligned(4096)));

typedef struct
{
  volatile uint8_t * array;
  int size;
  rtems_id done_sem;
  task_stats_t * stats;
  int expected_cpu;
  int task_name_idx;
} exp2_task_arg_t;

static task_stats_t exp2_stats[2];
static exp2_task_arg_t exp2_args[2];

static rtems_task exp2_periodic_task(rtems_task_argument arg)
{
  exp2_task_arg_t * ta = (exp2_task_arg_t *)(uintptr_t)arg;
  rtems_id period_id;
  rtems_status_code status;

  char idx = (char)('0' + ta->task_name_idx);
  rtems_rate_monotonic_create(rtems_build_name('E', '2', 'P', idx), &period_id);

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

static void run_two_tasks(volatile uint8_t * a_arr, volatile uint8_t * b_arr,
                          int a_cpu, int b_cpu, const char * label)
{
  rtems_id sem_id, task_a, task_b;

  printf("--- %s ---\n", label);

  memset((void *)a_arr, 0, WS_L1_FIT);
  memset((void *)b_arr, 0, WS_L1_FIT);

  memset(exp2_stats, 0, sizeof(exp2_stats));

  exp2_stats[0].role = TASK_ROLE_WORKER;
  exp2_stats[0].cpu_id = a_cpu;
  exp2_stats[0].task_idx = 0;
  exp2_stats[0].op_type = LOAD_OP;
  exp2_stats[0].n_accesses =
    NUM_STRESS_ITERATIONS_WORKER * (WS_L1_FIT / L1_LINE_SIZE);

  exp2_stats[1].role = TASK_ROLE_INTERFERER;
  exp2_stats[1].cpu_id = b_cpu;
  exp2_stats[1].task_idx = 1;
  exp2_stats[1].op_type = STORE_OP;
  exp2_stats[1].n_accesses =
    NUM_STRESS_ITERATIONS_INTERFERER * (WS_L1_FIT / L1_LINE_SIZE);

  rtems_semaphore_create(rtems_build_name('D', 'N', '2', '0'), 0,
                         RTEMS_COUNTING_SEMAPHORE, 0, &sem_id);

  exp2_args[0].array = a_arr;
  exp2_args[0].size = WS_L1_FIT;
  exp2_args[0].done_sem = sem_id;
  exp2_args[0].stats = &exp2_stats[0];
  exp2_args[0].expected_cpu = a_cpu;
  exp2_args[0].task_name_idx = 0;

  exp2_args[1].array = b_arr;
  exp2_args[1].size = WS_L1_FIT;
  exp2_args[1].done_sem = sem_id;
  exp2_args[1].stats = &exp2_stats[1];
  exp2_args[1].expected_cpu = b_cpu;
  exp2_args[1].task_name_idx = 1;

  rtems_task_create(rtems_build_name('W', 'K', '2', '0'), TASK_PRIORITY_WORKER,
                    TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT, &task_a);
  SET_AFFINITY(task_a, a_cpu);

  rtems_task_create(rtems_build_name('I', 'F', '2', '0'),
                    TASK_PRIORITY_INTERFERER, TASK_STACK_SIZE,
                    RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT, &task_b);
  SET_AFFINITY(task_b, b_cpu);

  rtems_task_start(task_a, exp2_periodic_task,
                   (rtems_task_argument)(uintptr_t)&exp2_args[0]);
  rtems_task_start(task_b, exp2_periodic_task,
                   (rtems_task_argument)(uintptr_t)&exp2_args[1]);

  rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
  rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

  print_task_stats(&exp2_stats[0], label);
  print_task_stats(&exp2_stats[1], label);
  rtems_semaphore_delete(sem_id);
  printf("--- %s: Done ---\n", label);
}

static void run_solo_task(volatile uint8_t * arr, int cpu, const char * label)
{
  rtems_id sem_id, task_id;

  printf("--- %s ---\n", label);
  memset((void *)arr, 0, WS_L1_FIT);
  memset(&exp2_stats[0], 0, sizeof(task_stats_t));

  exp2_stats[0].role = TASK_ROLE_WORKER;
  exp2_stats[0].cpu_id = cpu;
  exp2_stats[0].task_idx = 0;
  exp2_stats[0].op_type = RMW_OP;
  exp2_stats[0].n_accesses =
    NUM_STRESS_ITERATIONS_WORKER * (WS_L1_FIT / L1_LINE_SIZE);

  rtems_semaphore_create(rtems_build_name('D', 'N', '2', '3'), 0,
                         RTEMS_SIMPLE_BINARY_SEMAPHORE, 0, &sem_id);

  exp2_args[0].array = arr;
  exp2_args[0].size = WS_L1_FIT;
  exp2_args[0].done_sem = sem_id;
  exp2_args[0].stats = &exp2_stats[0];
  exp2_args[0].expected_cpu = cpu;
  exp2_args[0].task_name_idx = 0;

  rtems_task_create(rtems_build_name('W', 'K', '2', '3'), TASK_PRIORITY_WORKER,
                    TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT, &task_id);
  SET_AFFINITY(task_id, cpu);
  rtems_task_start(task_id, exp2_periodic_task,
                   (rtems_task_argument)(uintptr_t)&exp2_args[0]);

  rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

  print_task_stats(&exp2_stats[0], label);
  rtems_semaphore_delete(sem_id);
  printf("--- %s: Done ---\n", label);
}

/** -----------------------------------------------------------------
 * F-1: False Sharing — cross-core write-invalidation
 *
 * Configuration:
 *   Worker      : fs_array[i].worker_val (offset 0), Core 0, RMW
 *   Interferer  : fs_array[i].intf_val   (offset 4), Core 1, RMW
 *   Data layout : both fields share one 32-B cache line per element
 *   WS          : WS_L1_FIT each → FS_ELEM_COUNT elements
 *   Iterations  : NUM_TASK_ITERATIONS = 10000, Period = 100 ticks
 *
 * Mechanism:
 *   Every RMW by Core 0 on worker_val write-throughs to L2 and
 *   invalidates the same line in Core 1's L1. Every RMW by Core 1
 *   on intf_val causes the mirror invalidation. Despite accessing
 *   completely different variables, both tasks pay a cross-core L1
 *   invalidation penalty on every access.
 *
 * Expected result:
 *   Observe: min/avg/max TAT (us), time_per_access (ns),
 *   time_per_access_w/o_lo (ns).
 *   TAT significantly elevated vs F-3 (solo); delta isolates the
 *   false sharing write-invalidation cost per cache line access.
 *
 * CAAS limitation:
 *   CA_i is identical for both tasks (WS_L1_FIT, RMW pattern).
 *   CAAS has no shared-address-line metric and cannot distinguish
 *   this layout from the padded no-sharing layout in F-2.
 *----------------------------------------------------------------- **/
void run_exp2_F1(void)
{
  run_two_tasks((volatile uint8_t *)&fs_array[0].worker_val,
                (volatile uint8_t *)&fs_array[0].intf_val, 0, 1,
                "EXP2 F-1: False sharing (Core 0+1, same cache line)");
}

/** -----------------------------------------------------------------
 * F-2: No Sharing — independent arrays, bus bandwidth only
 *
 * Configuration:
 *   Worker      : ns_worker_buf[WS_L1_FIT], Core 0, RMW
 *   Interferer  : ns_intf_buf[WS_L1_FIT],   Core 1, RMW
 *   Data layout : separate 4096-B-aligned buffers → no shared lines
 *   Iterations  : NUM_TASK_ITERATIONS = 10000, Period = 100 ticks
 *
 * Mechanism:
 *   Each task sweeps its own independent WS_L1_FIT buffer.
 *   No cache line is shared between tasks, so there is no
 *   write-invalidation traffic. The only shared resource is the
 *   AHB bus and L2 controller (equivalent to exp1 A-4, RMW).
 *   Serves as the control group for F-1.
 *
 * Expected result:
 *   Observe: min/avg/max TAT (us), time_per_access (ns).
 *   TAT higher than F-3 (solo) due to bus contention, but lower
 *   than F-1. Delta (F-1 − F-2) = pure false sharing overhead.
 *
 * CAAS limitation:
 *   Same CA_i as F-1. CAAS cannot distinguish F-1 from F-2.
 *----------------------------------------------------------------- **/
void run_exp2_F2(void)
{
  run_two_tasks(ns_worker_buf, ns_intf_buf, 0, 1,
                "EXP2 F-2: No sharing (Core 0+1, independent arrays)");
}

/** -----------------------------------------------------------------
 * F-3: Solo Baseline — single task, no interference
 *
 * Configuration:
 *   Worker      : fs_array[i].worker_val (offset 0), Core 1, RMW
 *   Interferer  : none
 *   WS          : WS_L1_FIT → FS_ELEM_COUNT elements
 *   Iterations  : NUM_TASK_ITERATIONS = 10000, Period = 100 ticks
 *
 * Mechanism:
 *   Single task sweeps worker_val fields of fs_array in isolation.
 *   No invalidation traffic; task monopolizes its L1 and the AHB bus.
 *   Provides the zero-interference TAT reference for F-1 and F-2.
 *
 * Expected result:
 *   Observe: min/avg/max TAT (us), time_per_access (ns).
 *   Lowest TAT of the F-series; use as baseline to compute
 *   overhead percentage in F-1 and F-2.
 *----------------------------------------------------------------- **/
void run_exp2_F3(void)
{
  run_solo_task((volatile uint8_t *)&fs_array[0].worker_val, 1,
                "EXP2 F-3: Solo baseline (Core 1, worker_val only)");
}

void run_exp2_false_sharing(void)
{
  printf("=== EXP2: False Sharing ===\n\n");
  (void)fs_array[0].worker_val;
  (void)ns_worker_buf[0];
  (void)ns_intf_buf[0];

#ifdef RUN_F1
  run_exp2_F1();
#endif
#ifdef RUN_F2
  run_exp2_F2();
#endif
#ifdef RUN_F3
  run_exp2_F3();
#endif

  printf("\n=== EXP2: Complete ===\n\n");
}