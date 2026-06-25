#ifndef CHARGE_SOURCE_H
#define CHARGE_SOURCE_H

/*
 * Charging-time source abstraction.
 *
 * Returns the charging time (seconds) for the current Bonito round.
 *
 * Two implementations are provided:
 *
 *   charge_source_sim.c  (default, BONITO_SOURCE=sim)
 *     Replays a deterministic pre-generated trace from charge_trace.h, wrapping
 *     at CHARGE_TRACE_LEN. Used when running on constant USB power with
 *     DISABLE_CAP_MONITOR. The trace is shared with the laptop monitor so it
 * can independently verify the Bonito CI.
 *
 *   charge_source_real.c  (BONITO_SOURCE=real)
 *     Measures the actual capacitor charging time by calling
 *     riotee_wait_cap_charged() and bracketing it with the AM1805 persistent
 * RTC (riotee_am1805_set_alarm / riotee_timing_now). Implement this file when
 *     transitioning to a real energy-harvesting deployment.
 *
 * To switch: set BONITO_SOURCE=real in the make invocation and implement the
 * stub in charge_source_real.c. No other files need to change.
 */

/* Returns the next charging-time observation in seconds. */
float charge_source_next(void);

#endif /* CHARGE_SOURCE_H */
