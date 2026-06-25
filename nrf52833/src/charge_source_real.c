/*
 * Real energy-harvesting charging-time source (BONITO_SOURCE=real).
 *
 * ── STUB — NOT YET IMPLEMENTED ──────────────────────────────────────────────
 *
 * This file replaces charge_source_sim.c when transitioning to a real
 * energy-harvesting deployment (DISABLE_CAP_MONITOR removed, real solar/RF
 * energy source attached).
 *
 * Implementation sketch
 * ─────────────────────
 * The charging time is the wall-clock duration from the end of the previous
 * round's rendezvous_wait() to when the capacitor is charged enough to run the
 * radio.  Measure it using the AM1805 persistent RTC (survives power loss):
 *
 *   1. At the end of each round, record the current RTC time (t_start) via
 *      riotee_am1805_read() before powering down.
 *
 *   2. On the next reset/resume, read the RTC again (t_now).
 *
 *   3. charge_source_next() returns (t_now - t_start) in seconds.
 *
 * Alternatively, if the AM1805 alarm already wakes the device, the time since
 * the alarm fire is the residual over the connection interval; the total
 * elapsed time is CI + residual.
 *
 * Relevant SDK entry points:
 *   riotee_wait_cap_charged()       runtime.c:258  — blocks until PWRGD
 *   riotee_am1805_set_alarm()       am1805.c       — set absolute RTC alarm
 *   riotee_timing_now()             riotee_timing.h — 32 kHz RTC0 ticks
 * (VOLATILE)
 *
 * NOTE: RTC0 (riotee_timing_now) is volatile and resets on power loss; the
 * AM1805 external RTC is persistent.  Use the AM1805 for cross-power-cycle
 * timing.
 */
#include "charge_source.h"

float charge_source_next(void) {
  /* TODO: implement real harvesting measurement (see comment above). */
  return 1.0f; /* placeholder — 1 s constant */
}
