#ifndef __EXPERIMENT_COMMON_H__
#define __EXPERIMENT_COMMON_H__

#include <inttypes.h>
#include <rtems.h>
#include <rtems/rtems/ratemon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define WS_L1_FIT ((L1_CACHE_SIZE * 70) / 100)      /* ~11.2 KiB */
#define WS_L1_EXCEED ((L1_CACHE_SIZE * 150) / 100)  /* ~24 KiB  */
#define WS_L2_QUARTER (L2_CACHE_SIZE / 4)           /* 512 KiB  */
#define WS_L2_PRESSURE ((L2_CACHE_SIZE * 40) / 100) /* ~819 KiB */

/*=====================================================================
 * Periodic task parameters
 *=====================================================================*/
#define NUM_TASK_ITERATIONS 50
// #define NUM_STRESS_ITERATIONS 10
#define TASK_PERIOD_TICKS 100

#define TASK_PRIORITY_WORKER 10
#define TASK_PRIORITY_INTERFERER 10 /* same priority for RR time-sharing */

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

typedef struct
{
  task_role_t role;
  int task_idx; /* 0 = victim/worker, 1..N = aggressor */
  uint64_t min_ns;
  uint64_t max_ns;
  uint64_t sum_ns;
  int n_samples;
} task_stats_t;

/* Record one TAT sample into stats (safe to call with zeroed struct). */
static inline void record_tat(task_stats_t * s, uint64_t ns)
{
  if (s->n_samples == 0 || ns < s->min_ns) s->min_ns = ns;
  if (ns > s->max_ns) s->max_ns = ns;
  s->sum_ns += ns;
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
  printf(
    "  [TAT] %s | %s[%d] | n=%d | "
    "min=%" PRIu64 " avg=%" PRIu64 " max=%" PRIu64 " (ns)\n",
    label, s->role == TASK_ROLE_WORKER ? "WORKER" : "INTERFERER", s->task_idx,
    s->n_samples, s->min_ns, avg, s->max_ns);
}

// typedef struct
// {
//   task_role_t role;
//   int task_idx; /* 0 = victim/worker, 1..N = aggressor */
//   uint64_t tat_ns[NUM_TASK_ITERATIONS]; /* per-period TAT in nanoseconds */
//   int n_samples;                        /* number of valid entries in tat_ns
//   */
// } task_stats_t;

// /* Print min/avg/max TAT summary for one task. */
// static inline void print_task_stats(const task_stats_t * s, const char *
// label)
// {
//   if (s == NULL || s->n_samples <= 0)
//   {
//     return;
//   }
//   uint64_t mn = s->tat_ns[0];
//   uint64_t mx = s->tat_ns[0];
//   uint64_t sum = 0;
//   for (int i = 0; i < s->n_samples; i++)
//   {
//     if (s->tat_ns[i] < mn) mn = s->tat_ns[i];
//     if (s->tat_ns[i] > mx) mx = s->tat_ns[i];
//     sum += s->tat_ns[i];
//   }
//   uint64_t avg = sum / (uint64_t)s->n_samples;
//   printf(
//     "  [TAT] %s | %s[%d] | n=%d | "
//     "min=%" PRIu64 " avg=%" PRIu64 " max=%" PRIu64 " (ns)\n",
//     label, s->role == TASK_ROLE_WORKER ? "WORKER" : "INTERFERER",
//     s->task_idx, s->n_samples, mn, avg, mx);
// }

/*=====================================================================
 * Common cache workload: read-modify-write
 * L1 is write-through with allocate on load only,
 * so read triggers L1 allocate, write goes through to L2.
 *=====================================================================*/
static inline void cache_workload(volatile uint8_t * array, int size)
{
  for (int i = 0; i < size; i += L1_LINE_SIZE)
  {
    (void)array[i];
    // array[i] = i;
    __asm__ __volatile__("stbar" : : : "memory");
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
