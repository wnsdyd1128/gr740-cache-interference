#include "experiment_common.h"

void run_exp4_workingset(void);

rtems_task Init(rtems_task_argument arg)
{
  (void)arg;

  printf("\n");
  printf("============================================================\n");
  printf("  EXP4: Working-set Overlap — L1 / L2 separation\n");
  printf("  GR740 SMP | L1: 16KiB 4-way WT | L2: 2MiB 4-way CB\n");
  printf("  Periodic tasks: rate monotonic (period=%d ticks)\n",
         TASK_PERIOD_TICKS);
  printf("============================================================\n\n");

  run_exp4_workingset();
}

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS 8
#define CONFIGURE_MAXIMUM_SEMAPHORES 4
#define CONFIGURE_MAXIMUM_PERIODS 8
#define CONFIGURE_MAXIMUM_PROCESSORS 4

#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT
#define CONFIGURE_ENABLE_FLOATING_POINT

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT_TASK_STACK_SIZE TASK_STACK_SIZE

#define CONFIGURE_MICROSECONDS_PER_TICK MICROSECONDS_PER_TICK
#define CONFIGURE_SCHEDULER_EDF_SMP
#define CONFIGURE_MAXIMUM_PRIORITY 255

#define CONFIGURE_INIT
#include <rtems/confdefs.h>