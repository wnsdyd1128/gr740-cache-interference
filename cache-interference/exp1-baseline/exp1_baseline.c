#include <assert.h>
#include <rtems/extension.h>
#include <rtems/rtems/attr.h>
#include <rtems/rtems/cache.h>
#include <rtems/rtems/status.h>
#include <rtems/rtems/tasks.h>

#include "experiment_common.h"
#include "expr_configure.h"

/*=====================================================================
 * Experiment 1: Solo vs Interference — L1 / L2 / L1+L2 separation
 *
 * Purpose: isolate each cache interference layer independently and
 * quantify its TAT overhead relative to the solo baseline (A-1).
 *
 * GR740 cache topology (shared across all scenarios):
 *   L1 per core : 16 KiB, 4-way, 32 B line, write-through
 *   L2 shared   :  2 MiB, 4-way, 32 B line
 *   Cores       : 4 (LEON4-FT, EDF-SMP scheduler)
 *
 * Working-set size constants:
 *   WS_L1_FIT     = L1 * 70%  ~  11.2 KiB  (fits comfortably in L1)
 *   WS_L1_EXCEED  = L1 * 150% ~  24   KiB  (overflows L1, hits L2)
 *   WS_L2_PRESSURE= L2 * 40%  ~ 819   KiB  (heavy L2 footprint)
 *
 * Scenario matrix:
 *   A-1  Solo         WS_L1_FIT       Core 0           no interference
 *   A-2  L1 thrash    WS_L1_FIT       Core 0 + Core 0  same-core RR
 *   A-3  L2 contend   WS_L1_EXCEED    Core 0 + Core 1  cross-core L2
 *   A-4  L1+L2 sat    WS_L1_FIT       Core 0-3         4-core L2 flood
 *   A-5  L2 baseline  WS_L2_PRESSURE  Core 0           no interference
 *   A-6  L2 pressure  WS_L2_PRESSURE  Core 0 + Core 1  cross-core L2
 *
 * CAAS limitation demonstrated across all scenarios:
 *   CA_i is derived from a single task's reuse-distance analysis.
 *   It cannot capture: (a) combined WS of co-scheduled tasks,
 *   (b) cross-core L2 bandwidth contention, or (c) aggregate L2
 *   pressure from N concurrent tasks.  The CPI delta observed here
 *   is invisible to the CAAS scheduler.
 *=====================================================================*/

static volatile uint8_t exp1_worker_buf[WS_FULL_SIZE]
  __attribute__((aligned(4096)));

static volatile uint8_t exp1_intf1_buf[WS_FULL_SIZE]
  __attribute__((aligned(4096)));

static volatile uint8_t exp1_intf2_buf[WS_FULL_SIZE]
  __attribute__((aligned(4096)));

static volatile uint8_t exp1_intf3_buf[WS_FULL_SIZE]
  __attribute__((aligned(4096)));

typedef struct
{
  volatile uint8_t * array;
  int size;
  rtems_id done_sem;
  task_stats_t * stats; /* NULL = skip TAT recording */
  int expected_cpu;     /* CPU this task is pinned to (for runtime check) */
  int task_name_idx;    /* unique index for rate monotonic object name     */
} exp1_task_arg_t;

/* Stats storage: index 0 = worker (victim), 1..4 = interferers (aggressors).
 * Allocated as a flat array so any experiment variant can access all roles. */
static task_stats_t exp1_stats[5];
static exp1_task_arg_t exp1_args[5];

static rtems_task exp1_periodic_task(rtems_task_argument arg)
{
  exp1_task_arg_t * ta = (exp1_task_arg_t *)arg;
  rtems_id period_id;
  rtems_status_code status;

  /* Each task gets a unique period name via task_name_idx to avoid confusion
   * in debuggers/laysim.  Note: RTEMS rate monotonic names are non-unique
   * labels only; the returned period_id is the actual unique handle.       */
  char idx = (char)('0' + ta->task_name_idx);

  // Warm-up
  cache_workload(ta->array, ta->size);
  rtems_rate_monotonic_create(rtems_build_name('E', '1', 'P', idx), &period_id);
  for (int iter = 0; iter < NUM_TASK_ITERATIONS; iter++)
  {
    if (rtems_rate_monotonic_period(period_id, TASK_PERIOD_TICKS) ==
        RTEMS_TIMEOUT)
    {
      break;
    }
    // Verify affinity: task must always run on the core it was pinned to.
    // printf("Running %s task %d on CPU %d\n",
    //        ta->stats->role == TASK_ROLE_WORKER ? "WORKER" : "INTERFERER",
    //        ta->stats->task_idx, rtems_scheduler_get_processor());
    assert((int)rtems_scheduler_get_processor() == ta->expected_cpu);
    /* TAT start: job release point (period boundary) */
    uint64_t t0 = rtems_clock_get_uptime_nanoseconds();
    cache_workload(ta->array, ta->size);
    /* TAT end: workload (job body) complete */
    uint64_t t1 = rtems_clock_get_uptime_nanoseconds();
    if (ta->stats != NULL)
    {
      // ta->stats->tat_ns[iter] = t1 - t0;
      record_tat(ta->stats, t1 - t0);
    }
  }
  // if (ta->stats != NULL)
  // {
  //   ta->stats->n_samples = NUM_TASK_ITERATIONS;
  // }

  if ((status = rtems_rate_monotonic_delete(period_id)) != 0)
  {
    exit(1);
  }
  rtems_semaphore_release(ta->done_sem);
  rtems_task_exit();
}

static void run_single_worker(volatile uint8_t * buf, int size, int cpu,
                              const char * label)
{
  rtems_id sem_id, task_id;

  printf("--- %s ---\n", label);
  memset((void *)buf, 0, (size_t)size);

  /* Initialise stats slot for the victim (worker) task */
  memset(&exp1_stats[0], 0, sizeof(task_stats_t));
  exp1_stats[0].role = TASK_ROLE_WORKER;
  exp1_stats[0].task_idx = 0;

  rtems_semaphore_create(rtems_build_name('D', 'N', '1', '0'), 0,
                         RTEMS_SIMPLE_BINARY_SEMAPHORE, 0, &sem_id);

  exp1_args[0].array = buf;
  exp1_args[0].size = size;
  exp1_args[0].done_sem = sem_id;
  exp1_args[0].stats = &exp1_stats[0];
  exp1_args[0].expected_cpu = cpu;
  exp1_args[0].task_name_idx = 0;

  rtems_task_create(rtems_build_name('W', 'K', '1', '0'), TASK_PRIORITY_WORKER,
                    TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT, &task_id);
  SET_AFFINITY(task_id, cpu);
  rtems_task_start(task_id, exp1_periodic_task,
                   (rtems_task_argument)(uintptr_t)&exp1_args[0]);

  rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

  print_task_stats(&exp1_stats[0], label);

  rtems_task_delete(task_id);
  rtems_semaphore_delete(sem_id);
  printf("--- %s: Done ---\n", label);
}

static void run_worker_with_interferers(volatile uint8_t * w_buf, int w_size,
                                        int w_cpu, volatile uint8_t ** i_bufs,
                                        int * i_sizes, int * i_cpus, int n_intf,
                                        const char * label)
{
  rtems_id sem_id, task_ids[5];
  rtems_status_code status;
  int total_tasks = 1 + n_intf;

  printf("--- %s ---\n", label);

  memset((void *)w_buf, 0, (size_t)w_size);
  for (int i = 0; i < n_intf; i++)
    memset((void *)i_bufs[i], 0, (size_t)i_sizes[i]);

  /* Initialise stats slots: index 0 = victim worker, 1..n = aggressors */
  memset(exp1_stats, 0, sizeof(task_stats_t) * (size_t)total_tasks);
  exp1_stats[0].role = TASK_ROLE_WORKER;
  exp1_stats[0].task_idx = 0;
  for (int i = 0; i < n_intf; i++)
  {
    exp1_stats[1 + i].role = TASK_ROLE_INTERFERER;
    exp1_stats[1 + i].task_idx = i;
  }

  rtems_semaphore_create(rtems_build_name('D', 'N', '1', '1'), 0,
                         RTEMS_COUNTING_SEMAPHORE, 0, &sem_id);

  /* Worker (victim) */
  exp1_args[0].array = w_buf;
  exp1_args[0].size = w_size;
  exp1_args[0].done_sem = sem_id;
  exp1_args[0].stats = &exp1_stats[0];
  exp1_args[0].expected_cpu = w_cpu;
  exp1_args[0].task_name_idx = 0;

  rtems_task_create(rtems_build_name('W', 'K', '1', '1'), TASK_PRIORITY_WORKER,
                    TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
                    RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT,
                    &task_ids[0]);
  SET_AFFINITY(task_ids[0], w_cpu);

  /* Interferers (aggressors) */
  for (int i = 0; i < n_intf; i++)
  {
    exp1_args[1 + i].array = i_bufs[i];
    exp1_args[1 + i].size = i_sizes[i];
    exp1_args[1 + i].done_sem = sem_id;
    exp1_args[1 + i].stats = &exp1_stats[1 + i];
    exp1_args[1 + i].expected_cpu = i_cpus[i];
    exp1_args[1 + i].task_name_idx = 1 + i;

    char idx = (char)('0' + i);
    status = rtems_task_create(
      rtems_build_name('I', 'F', '1', idx), TASK_PRIORITY_INTERFERER,
      TASK_STACK_SIZE, RTEMS_DEFAULT_MODES,
      RTEMS_DEFAULT_ATTRIBUTES | RTEMS_FLOATING_POINT, &task_ids[1 + i]);
    if (status != 0)
    {
      printf("Failed to create interferer task %d with %d.\n", task_ids[i + 1],
             status);
    }
    SET_AFFINITY(task_ids[1 + i], i_cpus[i]);
  }

  rtems_cache_flush_entire_data();

  /* Start all tasks */
  status = rtems_task_start(task_ids[0], exp1_periodic_task,
                            (rtems_task_argument)(uintptr_t)&exp1_args[0]);
  if (status != 0)
  {
    printf("Failed to start victim task %d.\n", task_ids[0]);
  }
  for (int i = 0; i < n_intf; i++)
  {
    status =
      rtems_task_start(task_ids[1 + i], exp1_periodic_task,
                       (rtems_task_argument)(uintptr_t)&exp1_args[1 + i]);
    if (status != 0)
    {
      printf("Failed to start interferer task %d with %d.\n", task_ids[i + 1],
             status);
    }
  }

  /* Wait for all tasks */
  printf("Wait for all task to be done...\n");
  for (int i = 0; i < total_tasks; i++)
  {
    rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
  }

  /* Print victim TAT first, then each aggressor */
  print_task_stats(&exp1_stats[0], label);
  for (int i = 0; i < n_intf; i++) print_task_stats(&exp1_stats[1 + i], label);

  // for (int i = 0; i < total_tasks; i++) rtems_task_delete(task_ids[i]);
  rtems_semaphore_delete(sem_id);
  printf("--- %s: Done ---\n", label);
}

/*-----------------------------------------------------------------
 * A-1: Solo Baseline — no interference (reference measurement)
 *
 * Configuration:
 *   Worker      : WS_L1_FIT (~11.2 KiB), Core 0, Priority 10
 *   Interferer  : none
 *   Semaphore   : SIMPLE_BINARY (single waiter)
 *   Iterations  : NUM_TASK_ITERATIONS = 100, Period = 10 ticks
 *
 * Mechanism:
 *   Single task runs alone on Core 0.  Working-set fits within L1
 *   (11.2 KiB < 16 KiB), so every access is an L1 hit after the
 *   first iteration (warm-up).  No context switch, no eviction,
 *   no coherence traffic on the L2 bus.
 *
 * Expected result:
 *   Lowest TAT of all A-series scenarios; near-constant across
 *   iterations once cache is warm.  Use as the TAT reference
 *   to compute overhead percentage in A-2 through A-6.
 *
 * CAAS relevance:
 *   CA_i for this task is low (reuse distance << L1 capacity).
 *   CAAS would classify it as "cache-friendly" — consistent with
 *   the measured result.  No limitation exposed here; A-1 is the
 *   control condition.
 *-----------------------------------------------------------------*/
void run_exp1_A1(void)
{
  run_single_worker(exp1_worker_buf, WS_L1_FIT, 0,
                    "EXP1 A-1: Solo (L1 fit, Core 0)");
}

/*-----------------------------------------------------------------
 * A-2: L1 Thrashing — same-core Round-Robin time-sharing
 *
 * Configuration:
 *   Worker      : WS_L1_FIT (~11.2 KiB), Core 0, Priority 10
 *   Interferer 1: WS_L1_FIT (~11.2 KiB), Core 0, Priority 10
 *   Semaphore   : COUNTING (2 waiters)
 *   Iterations  : NUM_TASK_ITERATIONS = 100, Period = 10 ticks
 *
 * Mechanism:
 *   Two tasks share Core 0 with equal priority → RTEMS EDF-SMP
 *   applies Round-Robin within the same period.  Each task's WS
 *   individually fits in L1, but their combined footprint is:
 *     2 × 11.2 KiB = 22.4 KiB  >  L1 capacity (16 KiB)
 *   Every context switch evicts the other task's L1 lines, forcing
 *   a cold-start reload on each scheduling quantum → L1 thrashing.
 *
 * Expected result:
 *   TAT for Worker significantly higher than A-1 (+30–80%),
 *   despite identical per-task WS.  Interferer shows symmetric
 *   overhead (it is also a victim of the Worker's evictions).
 *
 * CAAS limitation (primary target):
 *   CAAS computes CA_i per task in isolation: WS_L1_FIT / L1 ≈ 0.7
 *   → both tasks appear cache-friendly individually.  CAAS has no
 *   combined-WS metric, so it cannot detect that co-scheduling them
 *   on the same core causes thrashing.  This is the core counter-
 *   example against CAAS's same-core placement decision.
 *-----------------------------------------------------------------*/
void run_exp1_A2(void)
{
  volatile uint8_t * i_bufs[] = {exp1_intf1_buf};
  int i_sizes[] = {WS_L1_FIT};
  int i_cpus[] = {1};

  run_worker_with_interferers(exp1_worker_buf, WS_L1_FIT, 1, i_bufs, i_sizes,
                              i_cpus, 1,
                              "EXP1 A-2: L1 contention (same core RR)");
}

/*-----------------------------------------------------------------
 * A-3: L2 Contention — cross-core, L1-overflow working-sets
 *
 * Configuration:
 *   Worker      : WS_L1_EXCEED (~24 KiB), Core 0, Priority 10
 *   Interferer 1: WS_L1_EXCEED (~24 KiB), Core 1, Priority 10
 *   Semaphore   : COUNTING (2 waiters)
 *   Iterations  : NUM_TASK_ITERATIONS = 100, Period = 10 ticks
 *
 * Mechanism:
 *   Each task's WS overflows L1 (24 KiB > 16 KiB), so every period
 *   generates L2 fill traffic regardless of interference.  Two cores
 *   run concurrently, both hammering the shared L2:
 *     Combined L2 demand = 2 × 24 KiB = 48 KiB of L2 bandwidth/period
 *   Unlike A-2, there is no L1 time-sharing; the bottleneck is
 *   shared L2 bus bandwidth and MSHR (miss-status holding register)
 *   saturation under simultaneous cache misses from two cores.
 *
 * Expected result:
 *   TAT higher than A-1 solo-large equivalent; L2 access latency
 *   inflated by cross-core L2 contention.  Overhead is less bursty
 *   than A-2 (no eviction storms) but shows as increased avg TAT.
 *
 * CAAS limitation:
 *   CA_i is derived from single-task reuse distance; it captures
 *   that WS_L1_EXCEED exceeds L1 (high CA).  However, CA carries
 *   no information about concurrent L2 demand from other cores.
 *   CAAS may place both high-CA tasks on separate cores (correct
 *   per-task policy), yet that placement itself causes this L2
 *   bandwidth collision — which CAAS cannot predict.
 *-----------------------------------------------------------------*/
void run_exp1_A3(void)
{
  volatile uint8_t * i_bufs[] = {exp1_intf1_buf};
  int i_sizes[] = {WS_L1_FIT};
  int i_cpus[] = {1};

  run_worker_with_interferers(exp1_worker_buf, WS_L1_FIT, 1, i_bufs, i_sizes,
                              i_cpus, 1, "EXP1 A-3: L2 contention (Core 1+2)");
}

/*-----------------------------------------------------------------
 * A-4: L1 + L2 Saturation — 4-core simultaneous flood
 *
 * Configuration:
 *   Worker      : WS_L1_FIT (~11.2 KiB), Core 0, Priority 10
 *   Interferer 1: WS_L1_FIT (~11.2 KiB), Core 1, Priority 10
 *   Interferer 2: WS_L1_FIT (~11.2 KiB), Core 2, Priority 10
 *   Interferer 3: WS_L1_FIT (~11.2 KiB), Core 3, Priority 10
 *   Semaphore   : COUNTING (4 waiters)
 *   Iterations  : NUM_TASK_ITERATIONS = 100, Period = 10 ticks
 *
 * Mechanism:
 *   Four tasks run in parallel, one per core.  Each task's WS fits
 *   in its private L1 (11.2 KiB < 16 KiB), so there is no same-core
 *   L1 thrashing (unlike A-2).  However, all four cores issue L2
 *   write-through stores simultaneously (L1 is write-through on
 *   GR740), flooding the shared L2 write bus:
 *     Aggregate store bandwidth = 4 × (11.2 KiB / 32 B) stores/period
 *   L2 bus arbitration latency and write-buffer back-pressure inflate
 *   the Worker's TAT even though its own WS fits in L1.
 *
 * Expected result:
 *   TAT higher than A-1 (solo L1-fit) and likely higher than A-2
 *   (L1 thrash only); represents the worst-case combination of
 *   per-core L1 write-through pressure on a shared L2.
 *
 * CAAS limitation:
 *   CAAS computes CA_i ≈ 0.7 for each task individually (fit-in-L1)
 *   and may recommend partitioned placement on separate cores —
 *   exactly this configuration.  But the aggregate write-through
 *   store pressure on the shared L2 is invisible to CA, which is
 *   a single-task, read-centric reuse-distance metric.
 *-----------------------------------------------------------------*/
void run_exp1_A4(void)
{
  volatile uint8_t * i_bufs[] = {exp1_intf1_buf, exp1_intf2_buf,
                                 exp1_intf3_buf};
  int i_sizes[] = {WS_L1_FIT, WS_L1_FIT, WS_L1_FIT};
  int i_cpus[] = {1, 2, 3};

  run_worker_with_interferers(exp1_worker_buf, WS_L1_FIT, 0, i_bufs, i_sizes,
                              i_cpus, 3,
                              "EXP1 A-4: L1+L2 contention (4 cores)");
}

/*-----------------------------------------------------------------
 * A-5: Solo L2-pressure Baseline — large WS, no interference
 *
 * Configuration:
 *   Worker      : WS_L2_PRESSURE (~819 KiB = L2 × 40%), Core 0, Priority 10
 *   Interferer  : none
 *   Semaphore   : SIMPLE_BINARY (single waiter)
 *   Iterations  : NUM_TASK_ITERATIONS = 100, Period = 10 ticks
 *
 * Mechanism:
 *   WS_L2_PRESSURE >> L1 (819 KiB vs 16 KiB), so every period
 *   causes a complete L1 eviction and a large L2 fill sequence.
 *   The task runs alone; L2 is not shared with any other core.
 *   This measures the inherent TAT cost of an L2-heavy task in
 *   isolation — the "high-CA solo" reference point.
 *
 * Expected result:
 *   TAT substantially higher than A-1 (reflecting L2 fill latency),
 *   but consistent across iterations (L2 stays warm within the period
 *   for the same 819 KiB region).  Serves as baseline for A-6.
 *
 * CAAS relevance:
 *   CA_i for this task is high (WS >> L1 → long reuse distance).
 *   CAAS correctly identifies it as L2-intensive and would place it
 *   on a dedicated core with low-CA neighbours.  No limitation here;
 *   A-5 validates that CAAS's high-CA classification is correct for
 *   a task in isolation before cross-core pressure is introduced.
 *-----------------------------------------------------------------*/
void run_exp1_A5(void)
{
  run_single_worker(exp1_worker_buf, WS_L2_PRESSURE, 0,
                    "EXP1 A-5: Solo large (L2 pressure baseline, Core 0)");
}

/*-----------------------------------------------------------------
 * A-6: L2 Pressure + Cross-core Competition — two high-CA tasks
 *
 * Configuration:
 *   Worker      : WS_L2_PRESSURE (~819 KiB = L2 × 40%), Core 0, Priority 10
 *   Interferer 1: WS_L2_PRESSURE (~819 KiB = L2 × 40%), Core 1, Priority 10
 *   Semaphore   : COUNTING (2 waiters)
 *   Iterations  : NUM_TASK_ITERATIONS = 100, Period = 10 ticks
 *
 * Mechanism:
 *   Both cores simultaneously stream ~819 KiB through their
 *   respective L1 caches (which are evicted entirely each pass) and
 *   into the shared L2.  Combined L2 working-set ≈ 1.64 MiB
 *   (82% of the 2 MiB L2).  L2 set-associativity conflicts become
 *   likely (4-way, 16 384 sets → same-set evictions between cores).
 *   L2 bus arbitration between two continuous streaming cores further
 *   inflates miss penalty.
 *
 * Expected result:
 *   TAT for Worker higher than A-5 baseline (+20–60%), showing
 *   that even a correctly isolated high-CA task suffers when another
 *   high-CA task runs concurrently on a sibling core.
 *
 * CAAS limitation (key result):
 *   CAAS recommends partitioned placement for high-CA tasks (each on
 *   its own core, WFD policy) — exactly what A-6 implements.
 *   Yet the shared L2 still causes significant TAT inflation because:
 *     (a) CAAS has no cross-task L2 footprint overlap metric
 *     (b) CA is per-task; aggregate L2 demand is not modelled
 *   Comparing A-5 vs A-6 with identical per-task CA values directly
 *   falsifies the assumption that per-task CA is sufficient for
 *   placement decisions in a shared-L2 multicore setting.
 *-----------------------------------------------------------------*/
void run_exp1_A6(void)
{
  volatile uint8_t * i_bufs[] = {exp1_intf1_buf};
  int i_sizes[] = {WS_L2_PRESSURE};
  int i_cpus[] = {1};

  run_worker_with_interferers(exp1_worker_buf, WS_L2_PRESSURE, 0, i_bufs,
                              i_sizes, i_cpus, 1,
                              "EXP1 A-6: L2 pressure (Core 0+1)");
}

void run_exp1_baseline(void)
{
  printf("=== EXP1: Solo vs Interference ===\n\n");
  (void)exp1_worker_buf[0];
  (void)exp1_intf1_buf[0];
  (void)exp1_intf2_buf[0];
  (void)exp1_intf3_buf[0];

#ifdef RUN_A1
  run_exp1_A1();
#endif
#ifdef RUN_A2
  run_exp1_A2();
#endif
#ifdef RUN_A3
  run_exp1_A3();
#endif
#ifdef RUN_A4
  run_exp1_A4();
#endif
#ifdef RUN_A5
  run_exp1_A5();
#endif
#ifdef RUN_A6
  run_exp1_A6();
#endif

  printf("\n=== EXP1: Finished ===\n");
}