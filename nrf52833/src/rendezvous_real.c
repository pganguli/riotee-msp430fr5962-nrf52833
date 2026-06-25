/*
 * Real energy-harvesting rendezvous wait (BONITO_SOURCE=real).
 *
 * Arms the AM1805 persistent RTC alarm ci_s seconds from now, records the
 * sleep epoch in a retained global for charge_source_real.c, then powers the
 * entire board off via riotee_am1805_disable_power().  The AM1805 survives
 * power loss and fires the alarm at the scheduled time, waking the board.
 *
 * On the next boot the Riotee runtime loads the checkpoint (which includes
 * g_sleep_epoch / g_sleep_valid written by this function one round earlier)
 * and resumes user code just after riotee_checkpoint() in main.c.
 * charge_source_next() then reads the current RTC time and computes the
 * actual elapsed "charging time" = t_now - g_sleep_epoch.
 *
 * Note: the alarm granularity is 1 second (AM1805 calendar alarm).  Sub-
 * second CI values are clamped to RENDEZVOUS_MIN_S (100 ms) and rounded up
 * to 1 s for the alarm.
 */
#include <time.h>

#include "rendezvous.h"
#include "riotee.h"
#include "riotee_am1805.h"
#include "riotee_timing.h"

#define RENDEZVOUS_MIN_S 0.1f

/* Written here; read by charge_source_real.c.  Retained → checkpointed. */
time_t g_sleep_epoch  = 0;
int    g_sleep_valid  = 0;

void rendezvous_wait(float ci_s) {
  struct tm now, alarm_time;
  int alarm_sec;

  if (ci_s < RENDEZVOUS_MIN_S) ci_s = RENDEZVOUS_MIN_S;

  riotee_am1805_get_datetime(&now);

  /* Save sleep start for charge_source_next() on the following boot. */
  g_sleep_epoch = mktime(&now);
  g_sleep_valid = 1;

  /* Compute alarm = now + ci_s (rounded up to whole seconds). */
  alarm_time = now;
  alarm_sec  = (int)(ci_s + 0.999f);
  if (alarm_sec < 1) alarm_sec = 1;
  alarm_time.tm_sec += alarm_sec;
  mktime(&alarm_time); /* normalize carries */

  riotee_am1805_clear_alarm();
  riotee_am1805_set_alarm(&alarm_time);
  riotee_am1805_disable_power(); /* board goes dark; wakes at alarm */

  /* Reached only if disable_power returns (e.g., bench supply always-on). */
  riotee_sleep_ms((uint32_t)(ci_s * 1000.0f));
}
