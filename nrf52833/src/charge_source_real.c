/*
 * Real energy-harvesting charging-time source (BONITO_SOURCE=real).
 *
 * Measures the actual elapsed time from when the board powered off at the end
 * of the previous Bonito round (set by rendezvous_real.c) to when it woke up
 * and cap-charged enough to reach this call.
 *
 * The AM1805 external RTC persists across power loss; g_sleep_epoch (stored by
 * rendezvous_real.c and included in the Riotee checkpoint) holds the POSIX
 * epoch second at which the board last powered off.  charge_source_next() reads
 * the RTC again and returns the difference.
 *
 * First-boot edge case: g_sleep_valid is 0 (initialised to 0, never set), so
 * we return BONITO_INIT_MEAN — the same prior used to seed the SGD model.
 *
 * One-round lag: the checkpoint is taken at the START of each round (before
 * rendezvous_wait updates g_sleep_epoch).  On a power-off restore, g_sleep_epoch
 * reflects the sleep-start from one round earlier; the error is bounded to one
 * round's active duration (~100 ms) and the SGD is robust to it.
 */
#include <time.h>

#include "charge_source.h"
#include "riotee_am1805.h"

/* Matches bonito_dist_init() prior in main.c. */
#define BONITO_INIT_MEAN 1.0f

/* Set by rendezvous_real.c; retained globals → included in checkpoint. */
extern time_t g_sleep_epoch;
extern int    g_sleep_valid;

float charge_source_next(void) {
  struct tm now;
  time_t t_now;
  double elapsed;

  if (!g_sleep_valid) return BONITO_INIT_MEAN;

  riotee_am1805_get_datetime(&now);
  t_now   = mktime(&now);
  elapsed = difftime(t_now, g_sleep_epoch);

  if (elapsed < 0.01) elapsed = BONITO_INIT_MEAN; /* RTC glitch guard */
  return (float)elapsed;
}
