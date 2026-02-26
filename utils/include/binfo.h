#include <rtems.h>
#include <rtems/rtems/object.h>
#include <rtems/rtems/support.h>
#include <rtems/scheduler.h>
#include <stdio.h>
#include <string.h>

void list_all_schedulers(void) {
    rtems_status_code status;
    rtems_id scheduler_id;
    char scheduler_name[5] = {0};
    
    // 일반적인 RTEMS 스케줄러 이름들을 확인
    const char* common_schedulers[] = {
        "MEDF",
        "MPA ",
        "MPD ",
        "MPS ",
        "UCBS",
        "UEDF",
        "UPD ",
        "UPS ",
        NULL
    };
    
    for (int i = 0; common_schedulers[i] != NULL; i++) {
        rtems_name name;
        memcpy(&name, common_schedulers[i], 4);
        
        status = rtems_scheduler_ident(name, &scheduler_id);
        if (status == RTEMS_SUCCESSFUL) {
            printf("  - %s (ID: 0x%08x)\n", common_schedulers[i], scheduler_id);
        }
    }
}

void print_current_scheduler_info(void) {
    rtems_status_code status;
    rtems_id scheduler_id;
    rtems_name scheduler_name;
    rtems_id task_id = RTEMS_SELF;
    uint32_t cpu_index;
    printf("=== Current Scheduler Information ===\n");
    // 1. 현재 태스크의 스케줄러 확인
    status = rtems_task_get_scheduler(task_id, &scheduler_id);
    if (status == RTEMS_SUCCESSFUL) {
        status = rtems_object_get_classic_name(scheduler_id, &scheduler_name);
        if (status) {
            printf("Current Task Scheduler: %d\n", scheduler_id);
        } else {
            printf("Current Task Scheduler ID: %d (name unavailable %d)\n", scheduler_id, scheduler_name);
        }
    } else {
        printf("Failed to get current task scheduler\n");
    }
    
    // // 2. 현재 CPU의 스케줄러 확인
    cpu_index = rtems_scheduler_get_processor();
    printf("Running on CPU: %u\n", cpu_index);
    
    // 3. 시스템의 모든 스케줄러 나열
    printf("\n=== Available Schedulers ===\n");
    list_all_schedulers();
}

