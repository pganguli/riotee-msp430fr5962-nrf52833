/*
 * Real energy-harvesting rendezvous wait (BONITO_SOURCE=real).
 *
 * ── STUB — NOT YET IMPLEMENTED ──────────────────────────────────────────────
 *
 * Implementation sketch
 * ─────────────────────
 * At the end of each Bonito round, arm the AM1805 persistent RTC alarm
 * ci_s seconds from now, then power the system down.  The alarm fires across
 * power loss and wakes the device at the scheduled connection interval.
 *
 * Relevant SDK entry points:
 *   riotee_am1805_set_alarm()   am1805.c — set an absolute alarm on the RTC
 *   riotee_am1805_read()        am1805.c — read current RTC time
 *
 * The AM1805 is trickle-charged from the capacitor and maintains time across
 * power cycles.  After the alarm fires the system boots, and the Riotee runtime
 * calls lateinit() / main() (which resumes from the checkpoint).
 *
 * Notes:
 *  - The alarm time should be (now + ci_s), computed in AM1805 seconds.
 *  - If ci_s is very small (< a few ms), the system may not have time to power
 *    down and wake cleanly; clamp ci_s to a safe minimum (e.g., 100 ms).
 *  - After waking, charge_source_real.c measures the actual elapsed time and
 *    returns it as the next charging-time observation for the Bonito SGD.
 */
#include "rendezvous.h"

void rendezvous_wait(float ci_s) {
  (void)ci_s;
  /* TODO: arm AM1805 alarm and power down (see comment above). */
}
