#include "experiment_common.h"

/*=====================================================================
 * Experiment 2: False Sharing — L1 / L2 separation
 *
 * F-1: false_sharing_t, Core 0+1  — L2 coherence
 * F-2: no_sharing_t, Core 0+1     — no interference
 * F-3: false_sharing_t, Core 0 RR — L1 only
 * F-4: solo counter++, Core 0     — baseline
 *=====================================================================*/

typedef struct {
    volatile uint32_t counter_A;    /* offset 0~3 */
    volatile uint32_t counter_B;    /* offset 4~7 */
} __attribute__((aligned(L1_LINE_SIZE))) false_sharing_t;

typedef struct {
    volatile uint32_t counter_A;
    uint8_t pad[L1_LINE_SIZE - sizeof(uint32_t)];
    volatile uint32_t counter_B;
} __attribute__((aligned(L1_LINE_SIZE))) no_sharing_t;

static false_sharing_t fs_data;
static no_sharing_t    ns_data;

typedef struct {
    volatile uint32_t *counter;
    rtems_id           done_sem;
} exp2_task_arg_t;

static exp2_task_arg_t exp2_args[2];

static rtems_task exp2_periodic_task(rtems_task_argument arg)
{
    exp2_task_arg_t *ta = (exp2_task_arg_t *)(uintptr_t)arg;
    rtems_id period_id;

    rtems_rate_monotonic_create(
        rtems_build_name('E', '2', 'P', '0'), &period_id);

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        rtems_rate_monotonic_period(period_id, TASK_PERIOD_TICKS);

        for (int i = 0; i < 10000; i++) {
            *(ta->counter) = *(ta->counter) + 1;
        }
    }

    rtems_rate_monotonic_cancel(period_id);
    rtems_rate_monotonic_delete(period_id);
    rtems_semaphore_release(ta->done_sem);
    rtems_task_exit();
}

static void run_two_counter_tasks(volatile uint32_t *cnt_a,
                                   volatile uint32_t *cnt_b,
                                   int cpu_a, int cpu_b,
                                   const char *label)
{
    rtems_id sem_id, task_a, task_b;

    printf("--- %s ---\n", label);

    *cnt_a = 0;
    *cnt_b = 0;

    rtems_semaphore_create(
        rtems_build_name('D', 'N', '2', '0'), 0,
        RTEMS_COUNTING_SEMAPHORE, 0, &sem_id);

    exp2_args[0].counter  = cnt_a;
    exp2_args[0].done_sem = sem_id;
    exp2_args[1].counter  = cnt_b;
    exp2_args[1].done_sem = sem_id;

    rtems_task_create(
        rtems_build_name('F', 'A', '2', '0'),
        TASK_PRIORITY_WORKER, TASK_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &task_a);
    SET_AFFINITY(task_a, cpu_a);

    rtems_task_create(
        rtems_build_name('F', 'B', '2', '0'),
        TASK_PRIORITY_WORKER, TASK_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &task_b);
    SET_AFFINITY(task_b, cpu_b);

    rtems_task_start(task_a, exp2_periodic_task,
                     (rtems_task_argument)(uintptr_t)&exp2_args[0]);
    rtems_task_start(task_b, exp2_periodic_task,
                     (rtems_task_argument)(uintptr_t)&exp2_args[1]);

    rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
    rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

    rtems_task_delete(task_a);
    rtems_task_delete(task_b);
    rtems_semaphore_delete(sem_id);
    printf("--- %s: Done ---\n", label);
}

/* F-1: false_sharing_t, Core 0 + Core 1 → L2 coherence */
void run_exp2_F1(void)
{
    run_two_counter_tasks(
        &fs_data.counter_A, &fs_data.counter_B, 0, 1,
        "EXP2 F-1: False sharing (Core 0+1, same cache line)");
}

/* F-2: no_sharing_t, Core 0 + Core 1 → no interference */
void run_exp2_F2(void)
{
    run_two_counter_tasks(
        &ns_data.counter_A, &ns_data.counter_B, 0, 1,
        "EXP2 F-2: No sharing (Core 0+1, padded)");
}

/* F-3: false_sharing_t, Core 0 time-sharing → L1 only */
void run_exp2_F3(void)
{
    run_two_counter_tasks(
        &fs_data.counter_A, &fs_data.counter_B, 0, 0,
        "EXP2 F-3: False sharing (Core 0 RR, same cache line)");
}

/* F-4: solo counter++, Core 0 → baseline */
void run_exp2_F4(void)
{
    rtems_id sem_id, task_id;
    static volatile uint32_t solo_counter;

    printf("--- EXP2 F-4: Solo counter (Core 0, baseline) ---\n");

    solo_counter = 0;

    rtems_semaphore_create(
        rtems_build_name('D', 'N', '2', '4'), 0,
        RTEMS_SIMPLE_BINARY_SEMAPHORE, 0, &sem_id);

    exp2_args[0].counter  = &solo_counter;
    exp2_args[0].done_sem = sem_id;

    rtems_task_create(
        rtems_build_name('F', 'S', '2', '4'),
        TASK_PRIORITY_WORKER, TASK_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &task_id);
    SET_AFFINITY(task_id, 0);
    rtems_task_start(task_id, exp2_periodic_task,
                     (rtems_task_argument)(uintptr_t)&exp2_args[0]);

    rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

    rtems_task_delete(task_id);
    rtems_semaphore_delete(sem_id);
    printf("--- EXP2 F-4: Done ---\n");
}

void run_exp2_false_sharing(void)
{
    printf("=== EXP2: False Sharing ===\n\n");

    run_exp2_F1();
    run_exp2_F2();
    run_exp2_F3();
    run_exp2_F4();

    printf("\n=== EXP2: Complete ===\n\n");
}