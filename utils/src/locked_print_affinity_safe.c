/*
 *  COPYRIGHT (c) 1989-2011.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#include <rtems/rtems/attr.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "test_support.h"
#include "tmacros.h"

#include <unistd.h>
#include <rtems/bspIo.h>
#include <rtems/counter.h>

static rtems_id locked_print_semaphore;      /* synchronisation semaphore */

static int locked_printf_plugin(void *context, const char *fmt, va_list ap)
{
  (void) context;
  return locked_vprintf(fmt, ap);
}

void locked_print_initialize(void)
{
  rtems_status_code sc;
  static            bool initted = false;

  if ( initted )
    return;

  initted = true;

  /* Create/verify synchronisation semaphore - SMP safe version */
  sc = rtems_semaphore_create(
    rtems_build_name ('S', 'E', 'M', '1'),
    1,                            /* Initial count = 1 (available) */
    RTEMS_GLOBAL |                /* SMP-aware */
    RTEMS_SIMPLE_BINARY_SEMAPHORE |  /* Simple binary, no priority inheritance */
    RTEMS_FIFO,                   /* FIFO ordering instead of priority */
    0,                            /* No priority ceiling */
    &locked_print_semaphore
  );
  directive_failed( sc, "rtems_semaphore_create" );

  /*
   * Set up the printer to use the locked printf printer.
   */
  rtems_test_printer.context = NULL;
  rtems_test_printer.printer = locked_printf_plugin;
}

int locked_vprintf(const char *fmt, va_list ap)
{
  int rv;
  rtems_status_code sc;

  locked_print_initialize();

  /* 
   * Use blocking wait instead of NO_WAIT with backoff
   * This is more SMP-friendly and avoids CPU spinning
   */
  sc = rtems_semaphore_obtain( locked_print_semaphore, RTEMS_WAIT, RTEMS_NO_TIMEOUT );
  
  if ( sc != RTEMS_SUCCESSFUL ) {
    /* Fallback: try a few times with short delays */
    int retries = 5;
    while (retries-- > 0 && sc != RTEMS_SUCCESSFUL) {
      rtems_task_wake_after(1);  /* Yield CPU to other tasks */
      sc = rtems_semaphore_obtain( locked_print_semaphore, RTEMS_NO_WAIT, 0 );
    }
    
    if (sc != RTEMS_SUCCESSFUL) {
      /* Emergency fallback - print without synchronization */
      return vprintk(fmt, ap);
    }
  }

  rv = vprintk(fmt, ap);

  /* Release the semaphore  */
  rtems_semaphore_release( locked_print_semaphore );

  return rv;
}

int locked_printf(const char *fmt, ...)
{
  int               rv;
  va_list           ap;       /* points to each unnamed argument in turn */

  va_start(ap, fmt); /* make ap point to 1st unnamed arg */
  rv = locked_vprintf(fmt, ap);
  va_end(ap);        /* clean up when done */

  return rv;
}