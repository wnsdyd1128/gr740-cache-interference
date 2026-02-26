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
#include <stdatomic.h>

/* Atomic spinlock - no task switching */
static atomic_flag print_lock = ATOMIC_FLAG_INIT;

static int locked_printf_plugin(void *context, const char *fmt, va_list ap)
{
  (void) context;
  return locked_vprintf(fmt, ap);
}

void locked_print_initialize(void)
{
  static bool initted = false;

  if ( initted )
    return;

  initted = true;

  /*
   * Set up the printer to use the locked printf printer.
   */
  rtems_test_printer.context = NULL;
  rtems_test_printer.printer = locked_printf_plugin;
}

int locked_vprintf(const char *fmt, va_list ap)
{
  int rv;

  locked_print_initialize();

  /* 
   * Acquire spinlock - busy wait, no task switching
   * This preserves scheduling behavior for performance tests
   */
  while (atomic_flag_test_and_set_explicit(&print_lock, memory_order_acquire)) {
    /* Busy wait - no CPU yielding */
    /* Optional: add a small delay to reduce contention */
    for (volatile int i = 0; i < 100; i++) {
      /* Small delay loop */
    }
  }

  rv = vprintk(fmt, ap);

  /* Release the spinlock */
  atomic_flag_clear_explicit(&print_lock, memory_order_release);

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