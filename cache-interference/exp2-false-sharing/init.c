#include "experiment_common.h"

void run_exp2_false_sharing(void);

rtems_task Init(rtems_task_argument arg) {
    (void)arg;

    printf("\n");
    printf("============================================================\n");
    printf("  EXP2: False Sharing — L1 / L2 separation\n");
    printf("  GR740 SMP | L1: 16KiB 4-way WT | L2: 2MiB 4-way CB\n");
    printf("  Periodic tasks: rate monotonic (period=%d ticks)\n",
           TASK_PERIOD_TICKS);
    printf("============================================================\n\n");

    run_exp2_false_sharing();

    printf("\n=== EXP2 finished. ===\n");
    rtems_task_exit();
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS            8
#define CONFIGURE_MAXIMUM_SEMAPHORES       4
#define CONFIGURE_MAXIMUM_PERIODS          8
#define CONFIGURE_MAXIMUM_PROCESSORS       4

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_STACK_SIZE     (128 * 1024)

#define CONFIGURE_SCHEDULER_EDF_SMP
#define CONFIGURE_MAXIMUM_PRIORITY         255

#define CONFIGURE_INIT
#include <rtems/confdefs.h>