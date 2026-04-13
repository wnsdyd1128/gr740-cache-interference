#ifndef __EXPERIMENT_COMMON_H__
#define __EXPERIMENT_COMMON_H__

#include <inttypes.h>
#include <rtems.h>
#include <rtems/rtems/ratemon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=====================================================================
 * GCC optimization level for cache workloads
 *=====================================================================*/
#define OPT_LEVEL "O2"

/*=====================================================================
 * RTEMS configure parameters
 *=====================================================================*/
#define MICROSECONDS_PER_TICK 1000

/*=====================================================================
 * GR740 Cache Parameters
 *=====================================================================*/
#define L1_CACHE_SIZE (16 * 1024)
#define L1_LINE_SIZE 32
#define L1_WAYS 4
#define L1_NUM_SETS 128

#define L2_CACHE_SIZE (2 * 1024 * 1024)
#define L2_LINE_SIZE 32
#define L2_WAYS 4
#define L2_NUM_SETS 16384

/*=====================================================================
 * Working-set sizes per interference layer
 *=====================================================================*/
#define WS_FULL_SIZE (L2_CACHE_SIZE)
#define WS_L1_FIT ((L1_CACHE_SIZE)*70 / 100)        /* ~11.5 KiB */
#define WS_L1_EXCEED ((L1_CACHE_SIZE)*200 / 100)    /*  32   KiB */
#define WS_L2_QUARTER (L2_CACHE_SIZE / 4)           /* 512   KiB */
#define WS_L2_PRESSURE ((L2_CACHE_SIZE * 70) / 100) /* ~819  KiB */

/*=====================================================================
 * Safe working-set sizes for interfernce control
 *=====================================================================*/
#define WS_L1_SAFE (L1_CACHE_SIZE / 4)  /* 4 KiB, fits in L1 */
#define WS_L2_SAFE (L2_CACHE_SIZE / 32) /* 64 KiB, fits in L2 */

/*=====================================================================
 * Periodic task parameters
 *=====================================================================*/
#define LOOP_ITER_JUMP_SIZE                         \
  L1_LINE_SIZE /* access every cache line in the WS \
                */

#define NUM_TASK_ITERATIONS 10000
#define NUM_STRESS_ITERATIONS_WORKER 1
#define NUM_STRESS_ITERATIONS_INTERFERER 1
#define TASK_PERIOD_TICKS 100

#define TASK_PRIORITY_WORKER 10
#define TASK_PRIORITY_INTERFERER 10

#define TASK_STACK_SIZE 4096

/*=====================================================================
 * CPU Affinity
 *=====================================================================*/
#define SET_AFFINITY(task_id, cpu)                               \
  do                                                             \
  {                                                              \
    cpu_set_t cpuset;                                            \
    CPU_ZERO(&cpuset);                                           \
    CPU_SET((cpu), &cpuset);                                     \
    rtems_task_set_affinity((task_id), sizeof(cpuset), &cpuset); \
  } while (0)

/*=====================================================================
 * Per-task turnaround time statistics
 *
 * TAT (Turnaround Time) per period:
 *   measured from immediately after rtems_rate_monotonic_period() returns
 *   (= job release point) to completion of cache_workload() (= job end).
 *
 * task_stats_t is designed to be embedded in each experiment's task arg,
 * supporting both victim (WORKER) and aggressor (INTERFERER) roles so
 * that any experiment can selectively enable per-role measurement.
 *=====================================================================*/
typedef enum
{
  TASK_ROLE_WORKER = 0,     /* victim task */
  TASK_ROLE_INTERFERER = 1, /* aggressor task */
} task_role_t;

typedef enum
{
  LOAD_OP = 0,  /* load-only */
  STORE_OP = 1, /* store-only */
  RMW_OP = 2,   /* read-modify-write */
} op_type_t;

typedef struct
{
  task_role_t role;
  op_type_t op_type;
  int task_idx; /* 0 = victim/worker, 1..N = aggressor */
  int cpu_id;   /* CPU this task is pinned to (for runtime check) */
  uint64_t min_ns;
  uint64_t max_ns;
  uint64_t sum_ns;
  uint64_t sum_wo_overhead_ns;
  uint64_t total_exec_ns;
  uint64_t loop_overhead_ns;
  int n_samples;
  int n_accesses;
} task_stats_t;

/* Record one TAT sample into stats (safe to call with zeroed struct). */
static inline void record_tat(task_stats_t * s, uint64_t ns)
{
  if (s->n_samples == 0 || ns < s->min_ns) s->min_ns = ns;
  if (ns > s->max_ns) s->max_ns = ns;
  s->sum_ns += ns;
  s->sum_wo_overhead_ns +=
    (ns > s->loop_overhead_ns) ? (ns - s->loop_overhead_ns) : 0;
  s->n_samples++;
}

/* Print min/avg/max TAT summary for one task. */
static inline void print_task_stats(const task_stats_t * s, const char * label)
{
  if (s == NULL || s->n_samples <= 0)
  {
    return;
  }
  uint64_t avg = s->sum_ns / (uint64_t)s->n_samples;
  uint64_t avg_wo_lo = s->sum_wo_overhead_ns / (uint64_t)s->n_samples;
  uint64_t time_per_access =
    s->n_accesses > 0 ? avg / (uint64_t)s->n_accesses : 0;
  uint64_t time_per_access_wo_lo =
    s->n_accesses > 0 ? avg_wo_lo / (uint64_t)s->n_accesses : 0;
  if (avg >= 1000)
  {
    printf(
      "  [TAT] %s | %s[%d] | cpu=%d | n=%d | "
      "min=%" PRIu64 " avg=%" PRIu64 " avg_w/o_lo=%" PRIu64 " max=%" PRIu64
      " (us) | n_accesses=%d | time_per_access=%" PRIu64
      " (ns) | time_per_access_w/o_lo=%" PRIu64
      " (ns) | accumulated execution time=%" PRIu64
      " (ns) | accumulated execution time w/o loop overhead=%" PRIu64
      " (ns) "
      "\n",
      label, s->role == TASK_ROLE_WORKER ? "WORKER" : "INTERFERER", s->task_idx,
      s->cpu_id, s->n_samples, s->min_ns / 1000, avg / 1000, avg_wo_lo / 1000,
      s->max_ns / 1000, s->n_accesses, time_per_access, time_per_access_wo_lo,
      s->sum_ns, s->sum_wo_overhead_ns);
  }
  else
  {
    printf(
      "  [TAT] %s | %s[%d] | cpu=%d | n=%d | "
      "min=%" PRIu64 " avg=%" PRIu64 " max=%" PRIu64
      " (ns) | time_per_access=%" PRIu64
      " (ns) | time_per_access_w/o_lo=%" PRIu64
      " (ns) | accumulated execution time=%" PRIu64
      " (ns) | accumulated execution time w/o loop overhead=%" PRIu64
      " (ns) "
      "\n",
      label, s->role == TASK_ROLE_WORKER ? "WORKER" : "INTERFERER", s->task_idx,
      s->cpu_id, s->n_samples, s->min_ns, avg, s->max_ns, time_per_access,
      time_per_access_wo_lo, s->sum_ns, s->sum_wo_overhead_ns);
  }
}

/*=====================================================================
 * Common cache workload: read-modify-write
 * L1 is write-through with allocate on load only,
 * so read triggers L1 allocate, write goes through to L2.
 *=====================================================================*/
__attribute__((optimize(OPT_LEVEL))) static inline void
empty_workload(volatile uint8_t * array, int size, int sweeps)
{
  (void)array;
  for (int s = 0; s < sweeps; s++)
  {
    for (int i = 0; i < size; i += LOOP_ITER_JUMP_SIZE)
    {
      // (void)array;
      // __asm__ __volatile__("" : : : "memory");
    }
  }
}

__attribute__((optimize(OPT_LEVEL))) static inline void
load_workload(volatile uint8_t * array, int size, int sweeps)
{
  register uint32_t tmp;
  for (int s = 0; s < sweeps; s++)
  {
    for (int i = 0; i < size; i += LOOP_ITER_JUMP_SIZE)
    {
      tmp += array[i];
    }
  }
  (void)tmp;
}

__attribute__((optimize(OPT_LEVEL))) static inline void
store_workload(volatile uint8_t * array, int size, int sweeps)
{
  for (int s = 0; s < sweeps; s++)
  {
    for (int i = 0; i < size; i += LOOP_ITER_JUMP_SIZE)
    {
      array[i] = (uint8_t)(i + 1);
      // __asm__ __volatile__("stbar" : : : "memory");
    }
  }
}

__attribute__((optimize(OPT_LEVEL))) static inline void
rmw_workload(volatile uint8_t * array, int size, int sweeps)
{
  for (int s = 0; s < sweeps; s++)
  {
    for (int i = 0; i < size; i += LOOP_ITER_JUMP_SIZE)
    {
      array[i] = array[i] + 1;
      // __asm__ __volatile__("stbar" : : : "memory");
    }
  }
}

/*=====================================================================
 * Common RTEMS configuration macros (use in each init.c)
 *=====================================================================*/
#define EXPERIMENT_RTEMS_CONFIG                             \
  _Pragma("GCC diagnostic push")                            \
    _Pragma("GCC diagnostic ignored \"-Wunused-variable\"") \
      _Pragma("GCC diagnostic pop")

#endif /* __EXPERIMENT_COMMON_H__ */
