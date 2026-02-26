/*
 * Hello world example
 */
#include <rtems.h>
#include <rtems/rtems/cache.h>
#include <rtems/test-info.h>
#include <rtems/test.h>
#include <stdio.h>
#include <stdlib.h>

const char rtems_test_name[] = "GR740 TEST";

rtems_task Init(rtems_task_argument ignored) {
  rtems_test_begin(rtems_test_name, TEST_STATE);
  printf("Target: GR740 SMP\n");
  printf("Available CPUs: %d\n", rtems_scheduler_get_processor_maximum());
  printf("L1 cache size %u bytes and instruction size %u "
         "bytes\n",
         rtems_cache_get_data_cache_size(1),
         rtems_cache_get_instruction_cache_size(1));
  printf("L2 cache size %u bytes and instruction size %u "
         "bytes\n",
         rtems_cache_get_data_cache_size(2),
         rtems_cache_get_instruction_cache_size(2));
  rtems_test_end(rtems_test_name);
  exit(0);
}