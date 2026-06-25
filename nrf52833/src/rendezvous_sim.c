/*
 * Simulated rendezvous wait (BONITO_SOURCE=sim).
 *
 * Sleeps for the Bonito connection interval using riotee_sleep_ms().
 * Under DISABLE_CAP_MONITOR (constant USB power) this is just a timer delay;
 * no energy management is involved.
 */
#include "rendezvous.h"
#include "riotee_timing.h"

/* Minimum sleep to avoid hammering the radio if Bonito computes a tiny CI. */
#define RENDEZVOUS_MIN_MS 100u

void rendezvous_wait(float ci_s) {
  uint32_t ms = (uint32_t)(ci_s * 1000.0f);
  if (ms < RENDEZVOUS_MIN_MS) {
    ms = RENDEZVOUS_MIN_MS;
  }
  riotee_sleep_ms(ms);
}
