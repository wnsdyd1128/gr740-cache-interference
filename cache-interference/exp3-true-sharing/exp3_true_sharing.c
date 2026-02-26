#include "experiment_common.h"

/*=====================================================================
 * Experiment 3: True Sharing (Producer-Consumer)
 *
 * T-1: Producer Core 0, Consumer Core 1 — L2 coherence
 * T-2: Producer Core 0, Consumer Core 0 — L1 only (time-sharing)
 * T-3: Producer Core 0, Consumer Core 2 — L2 coherence
 * T-4: Producer only, no Consumer       — baseline
 *=====================================================================*/

#define BUFFER_ENTRIES  1024

typedef struct {
    volatile uint32_t buffer[BUFFER_ENTRIES];
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
} __attribute__((aligned(L1_LINE_SIZE))) shared_buffer_t;

static shared_buffer_t exp3_shared;

typedef struct {
    shared_buffer_t *buf;
    rtems_id         done_sem;
} exp3_task_arg_t;

static exp3_task_arg_t exp3_producer_arg;
static exp3_task_arg_t exp3_consumer_arg;

static rtems_task exp3_producer_task(rtems_task_argument arg)
{
    exp3_task_arg_t *ta = (exp3_task_arg_t *)(uintptr_t)arg;
    shared_buffer_t *sb = ta->buf;
    rtems_id period_id;

    rtems_rate_monotonic_create(
        rtems_build_name('E', '3', 'P', 'P'), &period_id);

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        rtems_rate_monotonic_period(period_id, TASK_PERIOD_TICKS);

        for (int i = 0; i < BUFFER_ENTRIES; i++) {
            uint32_t widx = sb->write_idx;
            sb->buffer[widx % BUFFER_ENTRIES] = widx + 1;
            sb->write_idx = widx + 1;
        }
    }

    rtems_rate_monotonic_cancel(period_id);
    rtems_rate_monotonic_delete(period_id);
    rtems_semaphore_release(ta->done_sem);
    rtems_task_exit();
}

static rtems_task exp3_consumer_task(rtems_task_argument arg)
{
    exp3_task_arg_t *ta = (exp3_task_arg_t *)(uintptr_t)arg;
    shared_buffer_t *sb = ta->buf;
    rtems_id period_id;
    volatile uint32_t sink;

    rtems_rate_monotonic_create(
        rtems_build_name('E', '3', 'P', 'C'), &period_id);

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        rtems_rate_monotonic_period(period_id, TASK_PERIOD_TICKS);

        for (int i = 0; i < BUFFER_ENTRIES; i++) {
            uint32_t ridx = sb->read_idx;
            sink = sb->buffer[ridx % BUFFER_ENTRIES];
            sb->read_idx = ridx + 1;
        }
    }

    (void)sink;
    rtems_rate_monotonic_cancel(period_id);
    rtems_rate_monotonic_delete(period_id);
    rtems_semaphore_release(ta->done_sem);
    rtems_task_exit();
}

static void run_producer_consumer(int prod_cpu, int cons_cpu,
                                   int has_consumer, const char *label)
{
    rtems_id sem_id, prod_id, cons_id;
    int wait_count = has_consumer ? 2 : 1;

    printf("--- %s ---\n", label);

    memset((void *)&exp3_shared, 0, sizeof(exp3_shared));

    rtems_semaphore_create(
        rtems_build_name('D', 'N', '3', '0'), 0,
        RTEMS_COUNTING_SEMAPHORE, 0, &sem_id);

    exp3_producer_arg.buf      = &exp3_shared;
    exp3_producer_arg.done_sem = sem_id;

    rtems_task_create(
        rtems_build_name('P', 'R', '3', '0'),
        TASK_PRIORITY_WORKER, TASK_STACK_SIZE,
        RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &prod_id);
    SET_AFFINITY(prod_id, prod_cpu);

    if (has_consumer) {
        exp3_consumer_arg.buf      = &exp3_shared;
        exp3_consumer_arg.done_sem = sem_id;

        rtems_task_create(
            rtems_build_name('C', 'N', '3', '0'),
            TASK_PRIORITY_WORKER, TASK_STACK_SIZE,
            RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
            &cons_id);
        SET_AFFINITY(cons_id, cons_cpu);
        rtems_task_start(cons_id, exp3_consumer_task,
                         (rtems_task_argument)(uintptr_t)&exp3_consumer_arg);
    }

    rtems_task_start(prod_id, exp3_producer_task,
                     (rtems_task_argument)(uintptr_t)&exp3_producer_arg);

    for (int i = 0; i < wait_count; i++)
        rtems_semaphore_obtain(sem_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);

    rtems_task_delete(prod_id);
    if (has_consumer)
        rtems_task_delete(cons_id);
    rtems_semaphore_delete(sem_id);
    printf("--- %s: Done ---\n", label);
}

/* T-1: Producer Core 0, Consumer Core 1 */
void run_exp3_T1(void)
{
    run_producer_consumer(0, 1, 1,
        "EXP3 T-1: True sharing (Producer C0, Consumer C1)");
}

/* T-2: Producer Core 0, Consumer Core 0 (time-sharing) */
void run_exp3_T2(void)
{
    run_producer_consumer(0, 0, 1,
        "EXP3 T-2: True sharing (Producer C0, Consumer C0 RR)");
}

/* T-3: Producer Core 0, Consumer Core 2 */
void run_exp3_T3(void)
{
    run_producer_consumer(0, 2, 1,
        "EXP3 T-3: True sharing (Producer C0, Consumer C2)");
}

/* T-4: Producer only, no Consumer (baseline) */
void run_exp3_T4(void)
{
    run_producer_consumer(0, 0, 0,
        "EXP3 T-4: Producer only (baseline)");
}

void run_exp3_true_sharing(void)
{
    printf("=== EXP3: True Sharing (Producer-Consumer) ===\n\n");

    run_exp3_T1();
    run_exp3_T2();
    run_exp3_T3();
    run_exp3_T4();

    printf("\n=== EXP3: Complete ===\n\n");
}