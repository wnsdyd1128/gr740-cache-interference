#include "experiment_common.h"

/*=====================================================================
 * Experiment 5: CAAS Limitation — direct reproduction
 *
 * C-1: T1(8KB)+T2(8KB) same core       — safe (sum 16KB = L1)
 * C-2: T3(12KB)+T4(12KB) same core     — thrashing (sum 24KB > L1)
 * C-3: T3(12KB)+T4(12KB) diff cores    — recovery (L1 independent)
 * C-4: T5(256KB)+T6(256KB) diff cores   — L2 safe (sum < L2/2)
 * C-5: T7(768KB)+T8(768KB) diff cores   — L2 pressure
 * C-6: T9(8KB)+T10(8KB) same core      — conflict miss (4KB stride)
 *=====================================================================*/

#define C1_WS_SIZE      (8 * 1024)
#define C2_WS_SIZE      (12 * 1024)
#define C4_WS_SIZE      (256 * 1024)
#define C5_WS_SIZE      (768 * 1024)

#define CONFLICT_STRIDE         (4 * 1024)
#define CONFLICT_ARRAY_SIZE     (CONFLICT_STRIDE * 8)

static volatile uint8_t exp5_buf_a[C5_WS_SIZE]
    __attribute__((aligned(4096)));

static volatile uint8_t exp5_buf_b[C5_WS_SIZE]
    __attribute__((aligned(4096)));

static volatile uint8_t exp5_conflict_a[CONFLICT_ARRAY_SIZE]
    __attribute__((aligned(4096)));

static volatile uint8_t exp5_conflict_b[CONFLICT_ARRAY_SIZE]
    __attribute__((aligned(4096)));

typedef struct {
    volatile uint8_t *array;
    int               size;
    int               stride;  /* 0 = use L1_LINE_SIZE (normal) */
    rtems_id          done_sem;
} exp5_task_arg_t;

static exp5_task_arg_t exp5_args[2];

static rtems_task exp5_periodic_task(rtems_task_argument arg)
{
    exp5_task_arg_t *ta = (exp5_task_arg_t *)(uintptr_t)arg;
    rtems_id period_id;
    int stride = ta->stride > 0 ? ta->stride : L1_LINE_SIZE;

    rtems_rate_monotonic_create(
        rtems_build_name('E', '5', 'P', '0'), &period_id);

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        rtems_rate_monotonic_period(period_id, TASK_PERIOD_TICKS);

        for (int i = 0; i < ta->size; i += stride) {
            ta->array[i] = ta->array[i] + 1;
        }
    }

    rtems_rate_monotonic_cancel(period_id);
    rtems_rate_monotonic_delete(period_id);
    rtems_semaphore_release(ta->done_sem);
    rtems_task_exit();
}

static void run_pair(volatile uint8_t *buf_a, int size_a, int stride_a, int cpu_a,
                      volatile uint8_t *buf_b, int size_b, int stride_b, int cpu_b,
                      const char *label)
{
    rtems_id sem_id, task_a, task_b;

    printf("--- %s ---\n", label);
    memset((void *)buf_a, 0, (size_t)size_a);
    memset((void *)buf_b, 0, (size_t)size_b);

    rtems_semaphore_create(
        rtems_build_name('D', 'N', '5', '0'), 0,
        RTEMS_COUNTING_SEMAPHORE, 0, &sem_id);

    exp5_args[0].array    = buf_a;
    exp5_args[0].size     = size_a;
    exp5_args[0].stride   = stride_a;
    exp5_args[0].done_sem = sem_id;

    exp5_args[1].array    = buf_b;
    exp5_args[1].size     = size_b;
    exp5_args[1].stride   = stride_b;
    exp5_args[1].done_sem = sem_id;

    rtems_task_create(
        rtems_build_name('C', 'A', '5', '0'),
        TASK_PRIORITY_WORKER, TASK_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &task_a);
    SET_AFFINITY(task_a, cpu_a);

    rtems_task_create(
        rtems_build_name('C', 'B', '5', '0'),
        TASK_PRIORITY_WORKER, TASK_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &task_b);
    SET_AFFINITY(task_b, cpu_b);

    rtems_task_start(task_a, exp5_periodic_task,
                     (rtems_task_argument)(uintptr_t)&exp5_args[0]);
    rtems_task_start(task_b, exp5_periodic_task,
                     (rtems_task_argument)(uintptr_t)&exp5_args[1]);

    rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
    rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

    rtems_task_delete(task_a);
    rtems_task_delete(task_b);
    rtems_semaphore_delete(sem_id);
    printf("--- %s: Done ---\n", label);
}

/* C-1: 8KB + 8KB, same core → safe (sum = L1) */
void run_exp5_C1(void)
{
    run_pair(exp5_buf_a, C1_WS_SIZE, 0, 0,
             exp5_buf_b, C1_WS_SIZE, 0, 0,
             "EXP5 C-1: L1 safe pair (8KB+8KB, Core 0)");
}

/* C-2: 12KB + 12KB, same core → thrashing (sum > L1) */
void run_exp5_C2(void)
{
    run_pair(exp5_buf_a, C2_WS_SIZE, 0, 0,
             exp5_buf_b, C2_WS_SIZE, 0, 0,
             "EXP5 C-2: L1 danger pair (12KB+12KB, Core 0)");
}

/* C-3: 12KB + 12KB, different cores → recovery */
void run_exp5_C3(void)
{
    run_pair(exp5_buf_a, C2_WS_SIZE, 0, 0,
             exp5_buf_b, C2_WS_SIZE, 0, 1,
             "EXP5 C-3: L1 danger separated (12KB+12KB, Core 0+1)");
}

/* C-4: 256KB + 256KB, different cores → L2 safe */
void run_exp5_C4(void)
{
    run_pair(exp5_buf_a, C4_WS_SIZE, 0, 0,
             exp5_buf_b, C4_WS_SIZE, 0, 1,
             "EXP5 C-4: L2 safe pair (256KB+256KB, Core 0+1)");
}

/* C-5: 768KB + 768KB, different cores → L2 pressure */
void run_exp5_C5(void)
{
    run_pair(exp5_buf_a, C5_WS_SIZE, 0, 0,
             exp5_buf_b, C5_WS_SIZE, 0, 1,
             "EXP5 C-5: L2 pressure pair (768KB+768KB, Core 0+1)");
}

/* C-6: 8KB + 8KB, same core, conflict miss (4KB stride) */
void run_exp5_C6(void)
{
    run_pair(exp5_conflict_a, CONFLICT_ARRAY_SIZE, CONFLICT_STRIDE, 0,
             exp5_conflict_b, CONFLICT_ARRAY_SIZE, CONFLICT_STRIDE, 0,
             "EXP5 C-6: Conflict miss (4KB stride, Core 0)");
}

void run_exp5_caas_limitation(void)
{
    printf("=== EXP5: CAAS Limitation ===\n\n");

    run_exp5_C1();
    run_exp5_C2();
    run_exp5_C3();
    run_exp5_C4();
    run_exp5_C5();
    run_exp5_C6();

    printf("\n=== EXP5: Complete ===\n\n");
}