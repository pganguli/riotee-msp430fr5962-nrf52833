#ifndef RENDEZVOUS_H
#define RENDEZVOUS_H

/*
 * Rendezvous wait abstraction.
 *
 * Suspends execution for approximately ci_s seconds, the Bonito-computed
 * connection interval, before the next round.
 *
 * Two implementations are provided:
 *
 *   rendezvous_sim.c  (default, BONITO_SOURCE=sim)
 *     Calls riotee_sleep_ms(ci_s * 1000). Works correctly under constant power
 *     with DISABLE_CAP_MONITOR since the SDK's sleep is just a timer wait.
 *
 *   rendezvous_real.c  (BONITO_SOURCE=real)
 *     Arms the AM1805 persistent RTC alarm ci_s seconds from now and powers
 *     down the system. The AM1805 alarm fires across power loss and wakes the
 *     device at the scheduled connection interval.
 *
 * To switch: set BONITO_SOURCE=real and implement rendezvous_real.c.
 * No other files need to change.
 */

/* Sleep for approximately ci_s seconds (the current Bonito connection
 * interval). */
void rendezvous_wait(float ci_s);

#endif /* RENDEZVOUS_H */
