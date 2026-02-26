#include "experiment_common.h"

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

static volatile uint8_t exp4_task_e_buf[WS_L2_PRESSURE]
    __attribute__((aligned(4096)));

static volatile uint8_t exp4_task_f_buf[WS_L2_PRESSURE]
    __attribute__((aligned(4096)));

typedef struct {
    volatile uint8_t *array;
    int               size;
    rtems_id          done_sem;
} exp4_task_arg_t;

static exp4_task_arg_t exp4_args[2];

static rtems_task exp4_periodic_task(rtems_task_argument arg)
{
    exp4_task_arg_t *ta = (exp4_task_arg_t *)(uintptr_t)arg;
    rtems_id period_id;

    rtems_rate_monotonic_create(
        rtems_build_name('E', '4', 'P', '0'), &period_id);

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        rtems_rate_monotonic_period(period_id, TASK_PERIOD_TICKS);
        cache_workload(ta->array, ta->size, 1);
    }

    rtems_rate_monotonic_cancel(period_id);
    rtems_rate_monotonic_delete(period_id);
    rtems_semaphore_release(ta->done_sem);
    rtems_task_exit();
}

static void run_single(volatile uint8_t *buf, int size,
                        int cpu, const char *label)
{
    rtems_id sem_id, task_id;

    printf("--- %s ---\n", label);
    memset((void *)buf, 0, (size_t)size);

    rtems_semaphore_create(
        rtems_build_name('D', 'N', '4', '0'), 0,
        RTEMS_SIMPLE_BINARY_SEMAPHORE, 0, &sem_id);

    exp4_args[0].array    = buf;
    exp4_args[0].size     = size;
    exp4_args[0].done_sem = sem_id;

    rtems_task_create(
        rtems_build_name('W', 'K', '4', '0'),
        TASK_PRIORITY_WORKER, TASK_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &task_id);
    SET_AFFINITY(task_id, cpu);
    rtems_task_start(task_id, exp4_periodic_task,
                     (rtems_task_argument)(uintptr_t)&exp4_args[0]);

    rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

    rtems_task_delete(task_id);
    rtems_semaphore_delete(sem_id);
    printf("--- %s: Done ---\n", label);
}

static void run_pair(volatile uint8_t *buf_e, int size_e, int cpu_e,
                      volatile uint8_t *buf_f, int size_f, int cpu_f,
                      const char *label)
{
    rtems_id sem_id, task_e, task_f;

    printf("--- %s ---\n", label);
    memset((void *)buf_e, 0, (size_t)size_e);
    memset((void *)buf_f, 0, (size_t)size_f);

    rtems_semaphore_create(
        rtems_build_name('D', 'N', '4', '1'), 0,
        RTEMS_COUNTING_SEMAPHORE, 0, &sem_id);

    exp4_args[0].array    = buf_e;
    exp4_args[0].size     = size_e;
    exp4_args[0].done_sem = sem_id;

    exp4_args[1].array    = buf_f;
    exp4_args[1].size     = size_f;
    exp4_args[1].done_sem = sem_id;

    rtems_task_create(
        rtems_build_name('T', 'E', '4', '0'),
        TASK_PRIORITY_WORKER, TASK_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &task_e);
    SET_AFFINITY(task_e, cpu_e);

    rtems_task_create(
        rtems_build_name('T', 'F', '4', '0'),
        TASK_PRIORITY_WORKER, TASK_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &task_f);
    SET_AFFINITY(task_f, cpu_f);

    rtems_task_start(task_e, exp4_periodic_task,
                     (rtems_task_argument)(uintptr_t)&exp4_args[0]);
    rtems_task_start(task_f, exp4_periodic_task,
                     (rtems_task_argument)(uintptr_t)&exp4_args[1]);

    rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
    rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

    rtems_task_delete(task_e);
    rtems_task_delete(task_f);
    rtems_semaphore_delete(sem_id);
    printf("--- %s: Done ---\n", label);
}

/* W-1: Solo, WS_L1_FIT, Core 0 */
void run_exp4_W1(void)
{
    run_single(exp4_task_e_buf, WS_L1_FIT, 0,
               "EXP4 W-1: Solo (L1 fit, Core 0)");
}

/* W-2: L1 thrashing, same core */
void run_exp4_W2(void)
{
    run_pair(exp4_task_e_buf, WS_L1_FIT, 0,
             exp4_task_f_buf, WS_L1_FIT, 0,
             "EXP4 W-2: L1 thrashing (Core 0 RR)");
}

/* W-3: L1 independent, different cores */
void run_exp4_W3(void)
{
    run_pair(exp4_task_e_buf, WS_L1_FIT, 0,
             exp4_task_f_buf, WS_L1_FIT, 1,
             "EXP4 W-3: L1 independent (Core 0+1)");
}

/* W-4: L2 competition, different cores */
void run_exp4_W4(void)
{
    run_pair(exp4_task_e_buf, WS_L2_QUARTER, 0,
             exp4_task_f_buf, WS_L2_QUARTER, 1,
             "EXP4 W-4: L2 competition (Core 0+1)");
}

/* W-5: L2 heavy pressure, different cores */
void run_exp4_W5(void)
{
    run_pair(exp4_task_e_buf, WS_L2_PRESSURE, 0,
             exp4_task_f_buf, WS_L2_PRESSURE, 1,
             "EXP4 W-5: L2 heavy pressure (Core 0+1)");
}

/* W-6: L1 safe, combined fits in L1 */
void run_exp4_W6(void)
{
    run_pair(exp4_task_e_buf, WS_L1_FIT / 2, 0,
             exp4_task_f_buf, WS_L1_FIT / 2, 0,
             "EXP4 W-6: L1 safe (half each, Core 0 RR)");
}

void run_exp4_workingset(void)
{
    printf("=== EXP4: Working-set Overlap ===\n\n");

    run_exp4_W1();
    run_exp4_W2();
    run_exp4_W3();
    run_exp4_W4();
    run_exp4_W5();
    run_exp4_W6();

    printf("\n=== EXP4: Complete ===\n\n");
}