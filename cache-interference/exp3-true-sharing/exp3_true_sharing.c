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
 * Experiment 3: True Sharing (Producer-Consumer)
 *
 * Purpose: quantify the TAT overhead caused by true sharing — a consumer
 * task (LOAD) and a producer task (STORE) intentionally accessing the
 * same cache lines, triggering repeated cross-core write-invalidations
 * on the consumer's L1.
 *
 * GR740 cache topology:
 *   L1 per core : 16 KiB, 4-way, 32 B line, write-through, no write-allocate
 *   L2 shared   :  2 MiB, 4-way, 32 B line
 *
 * Mechanism:
 *   GR740's L1 is write-through with no write-allocate. Every producer
 *   STORE bypasses L1 and goes directly to L2, simultaneously
 *   invalidating the same line in the consumer's L1. The consumer must
 *   then re-fetch from L2 on the next LOAD, paying full L2 latency
 *   instead of an L1 hit.
 *
 * Data layout:
 *   shared_buf[WS_L1_FIT] : single buffer both tasks access (true sharing)
 *   ts_worker_buf[WS_L1_FIT] : consumer's private buffer (no-sharing baseline)
 *   ts_intf_buf[WS_L1_FIT]   : producer's private buffer (no-sharing baseline)
 *
 * Scenario matrix:
 *   T-1  True Sharing   shared_buf (both)      Core 0(LOAD) + Core 1(STORE)
 *   T-2  No Sharing     independent buffers    Core 0(LOAD) + Core 1(STORE)
 *   T-3  Solo Baseline  shared_buf (LOAD only) Core 1 only
 *
 *   T-1 vs T-2 : delta = pure true sharing (write-invalidation) cost
 *   T-1 vs T-3 : delta = full cross-core coherence + bus BW overhead
 *
 * CAAS limitation demonstrated:
 *   Both T-1 and T-2 consumers have identical per-task WS (WS_L1_FIT,
 *   LOAD pattern) → identical CA_i. CAAS has no information about
 *   whether the co-scheduled producer writes to the same addresses,
 *   so it cannot distinguish true sharing from independent access.
 *===================================================================== **/

static volatile uint8_t shared_buf[WS_L1_FIT] __attribute__((aligned(4096)));
static volatile uint8_t ts_worker_buf[WS_L1_FIT] __attribute__((aligned(4096)));
static volatile uint8_t ts_intf_buf[WS_L1_FIT] __attribute__((aligned(4096)));

typedef struct
{
  volatile uint8_t * array;
  int size;
  rtems_id done_sem;
  task_stats_t * stats;
  int expected_cpu;
  int task_name_idx;
} exp3_task_arg_t;

static task_stats_t exp3_stats[2];
static exp3_task_arg_t exp3_args[2];

static rtems_task exp3_periodic_task(rtems_task_argument arg)
{
  exp3_task_arg_t * ta = (exp3_task_arg_t *)(uintptr_t)arg;
  rtems_id period_id;
  rtems_status_code status;

  char idx = (char)('0' + ta->task_name_idx);
  rtems_rate_monotonic_create(rtems_build_name('E', '3', 'P', idx), &period_id);

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

  if ((status = rtems_rate_monotonic_delete(period_id)) != 0)
    exit(1);

  rtems_semaphore_release(ta->done_sem);
  rtems_task_exit();
}

/* Run consumer (LOAD, worker_arr) + producer (STORE, intf_arr) concurrently.
 * When worker_arr == intf_arr the two tasks access the same cache lines
 * (true sharing); when they differ, only bus bandwidth is shared. */
static void run_consumer_producer(volatile uint8_t * worker_arr,
                                  volatile uint8_t * intf_arr,
                                  int worker_cpu, int intf_cpu,
                                  const char * label)
{
  rtems_id sem_id, task_worker, task_intf;

  printf("--- %s ---\n", label);

  memset((void *)worker_arr, 0, WS_L1_FIT);
  if (intf_arr != worker_arr)
    memset((void *)intf_arr, 0, WS_L1_FIT);

  memset(exp3_stats, 0, sizeof(exp3_stats));

  /* Worker = consumer: LOAD on the (possibly shared) array */
  exp3_stats[0].role = TASK_ROLE_WORKER;
  exp3_stats[0].cpu_id = worker_cpu;
  exp3_stats[0].task_idx = 0;
  exp3_stats[0].op_type = LOAD_OP;
  exp3_stats[0].n_accesses =
    NUM_STRESS_ITERATIONS_WORKER * (WS_L1_FIT / L1_LINE_SIZE);

  /* Interferer = producer: STORE on the (possibly shared) array */
  exp3_stats[1].role = TASK_ROLE_INTERFERER;
  exp3_stats[1].cpu_id = intf_cpu;
  exp3_stats[1].task_idx = 1;
  exp3_stats[1].op_type = STORE_OP;
  exp3_stats[1].n_accesses =
    NUM_STRESS_ITERATIONS_INTERFERER * (WS_L1_FIT / L1_LINE_SIZE);

  rtems_semaphore_create(rtems_build_name('D', 'N', '3', '0'), 0,
                         RTEMS_COUNTING_SEMAPHORE, 0, &sem_id);

  exp3_args[0].array = worker_arr;
  exp3_args[0].size = WS_L1_FIT;
  exp3_args[0].done_sem = sem_id;
  exp3_args[0].stats = &exp3_stats[0];
  exp3_args[0].expected_cpu = worker_cpu;
  exp3_args[0].task_name_idx = 0;

  exp3_args[1].array = intf_arr;
  exp3_args[1].size = WS_L1_FIT;
  exp3_args[1].done_sem = sem_id;
  exp3_args[1].stats = &exp3_stats[1];
  exp3_args[1].expected_cpu = intf_cpu;
  exp3_args[1].task_name_idx = 1;

  rtems_task_create(rtems_build_name('W', 'K', '3', '0'), TASK_PRIORITY_WORKER,
                    TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT,
                    &task_worker);
  SET_AFFINITY(task_worker, worker_cpu);

  rtems_task_create(rtems_build_name('I', 'F', '3', '0'),
                    TASK_PRIORITY_INTERFERER, TASK_STACK_SIZE,
                    RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT,
                    &task_intf);
  SET_AFFINITY(task_intf, intf_cpu);

  rtems_task_start(task_worker, exp3_periodic_task,
                   (rtems_task_argument)(uintptr_t)&exp3_args[0]);
  rtems_task_start(task_intf, exp3_periodic_task,
                   (rtems_task_argument)(uintptr_t)&exp3_args[1]);

  rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
  rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

  print_task_stats(&exp3_stats[0], label);
  print_task_stats(&exp3_stats[1], label);
  rtems_semaphore_delete(sem_id);
  printf("--- %s: Done ---\n", label);
}

static void run_solo_consumer(volatile uint8_t * arr, int cpu,
                              const char * label)
{
  rtems_id sem_id, task_id;

  printf("--- %s ---\n", label);
  memset((void *)arr, 0, WS_L1_FIT);
  memset(&exp3_stats[0], 0, sizeof(task_stats_t));

  exp3_stats[0].role = TASK_ROLE_WORKER;
  exp3_stats[0].cpu_id = cpu;
  exp3_stats[0].task_idx = 0;
  exp3_stats[0].op_type = LOAD_OP;
  exp3_stats[0].n_accesses =
    NUM_STRESS_ITERATIONS_WORKER * (WS_L1_FIT / L1_LINE_SIZE);

  rtems_semaphore_create(rtems_build_name('D', 'N', '3', '3'), 0,
                         RTEMS_SIMPLE_BINARY_SEMAPHORE, 0, &sem_id);

  exp3_args[0].array = arr;
  exp3_args[0].size = WS_L1_FIT;
  exp3_args[0].done_sem = sem_id;
  exp3_args[0].stats = &exp3_stats[0];
  exp3_args[0].expected_cpu = cpu;
  exp3_args[0].task_name_idx = 0;

  rtems_task_create(rtems_build_name('W', 'K', '3', '3'), TASK_PRIORITY_WORKER,
                    TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT, &task_id);
  SET_AFFINITY(task_id, cpu);
  rtems_task_start(task_id, exp3_periodic_task,
                   (rtems_task_argument)(uintptr_t)&exp3_args[0]);

  rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

  print_task_stats(&exp3_stats[0], label);
  rtems_semaphore_delete(sem_id);
  printf("--- %s: Done ---\n", label);
}

/** -----------------------------------------------------------------
 * T-1: True Sharing — cross-core write-invalidation
 *
 * Configuration:
 *   Worker      : shared_buf[WS_L1_FIT], Core 0, LOAD
 *   Interferer  : shared_buf[WS_L1_FIT], Core 1, STORE  ← same array
 *   Iterations  : NUM_TASK_ITERATIONS = 10000, Period = 100 ticks
 *
 * Mechanism:
 *   Producer (Core 1) stores to every cache line of shared_buf.
 *   L1 is write-through, so each store immediately updates L2 and
 *   invalidates the corresponding line in Core 0's L1. Consumer
 *   (Core 0) must re-fetch from L2 on every load, paying full L2
 *   latency instead of an L1 hit despite the WS fitting in L1.
 *
 * Expected result:
 *   Observe: min/avg/max TAT (us), time_per_access (ns) for consumer.
 *   Consumer TAT elevated vs T-3 (solo LOAD); delta isolates the
 *   write-invalidation penalty per cache line access.
 *
 * CAAS limitation:
 *   Consumer's CA_i == T-2 consumer's CA_i (same WS, same op).
 *   CAAS cannot detect that the producer writes to the same addresses.
 *----------------------------------------------------------------- **/
void run_exp3_T1(void)
{
  run_consumer_producer(shared_buf, shared_buf, 0, 1,
                        "EXP3 T-1: True sharing (Core 0 LOAD + Core 1 STORE, same array)");
}

/** -----------------------------------------------------------------
 * T-2: No Sharing — independent arrays, bus bandwidth only
 *
 * Configuration:
 *   Worker      : ts_worker_buf[WS_L1_FIT], Core 0, LOAD
 *   Interferer  : ts_intf_buf[WS_L1_FIT],   Core 1, STORE
 *   Iterations  : NUM_TASK_ITERATIONS = 10000, Period = 100 ticks
 *
 * Mechanism:
 *   Consumer and producer access completely independent buffers.
 *   No cache line is shared, so there is no write-invalidation
 *   traffic. The only shared resource is the AHB bus and L2
 *   controller. Serves as the control group for T-1.
 *
 * Expected result:
 *   Observe: min/avg/max TAT (us), time_per_access (ns) for consumer.
 *   Consumer TAT higher than T-3 (solo) due to bus contention only,
 *   but lower than T-1. Delta (T-1 − T-2) = pure true sharing cost.
 *
 * CAAS limitation:
 *   Same CA_i as T-1 consumer. CAAS cannot distinguish T-1 from T-2.
 *----------------------------------------------------------------- **/
void run_exp3_T2(void)
{
  run_consumer_producer(ts_worker_buf, ts_intf_buf, 0, 1,
                        "EXP3 T-2: No sharing (Core 0 LOAD + Core 1 STORE, independent arrays)");
}

/** -----------------------------------------------------------------
 * T-3: Solo Baseline — consumer only, no interference
 *
 * Configuration:
 *   Worker      : shared_buf[WS_L1_FIT], Core 1, LOAD
 *   Interferer  : none
 *   Iterations  : NUM_TASK_ITERATIONS = 10000, Period = 100 ticks
 *
 * Mechanism:
 *   Single consumer task loads shared_buf in isolation. Working-set
 *   fits in L1 (WS_L1_FIT < L1_CACHE_SIZE), so every access is an
 *   L1 hit after the first sweep. No invalidation or bus contention.
 *
 * Expected result:
 *   Observe: min/avg/max TAT (us), time_per_access (ns).
 *   Lowest TAT of the T-series; equivalent to exp1 A-1 for LOAD.
 *   Use as the TAT reference for T-1 and T-2.
 *----------------------------------------------------------------- **/
void run_exp3_T3(void)
{
  run_solo_consumer(shared_buf, 1,
                    "EXP3 T-3: Solo baseline (Core 1, LOAD only)");
}

void run_exp3_true_sharing(void)
{
  printf("=== EXP3: True Sharing (Producer-Consumer) ===\n\n");
  (void)shared_buf[0];
  (void)ts_worker_buf[0];
  (void)ts_intf_buf[0];

#ifdef RUN_T1
  run_exp3_T1();
#endif
#ifdef RUN_T2
  run_exp3_T2();
#endif
#ifdef RUN_T3
  run_exp3_T3();
#endif

  printf("\n=== EXP3: Complete ===\n\n");
}
