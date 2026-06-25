/*
 * Bonito connection-interval engine.
 *
 * See bonito.h for the design rationale and per-round usage pattern.
 */
#include "bonito.h"

#include <math.h>

float bonito_connection_interval(const bonito_dist_t *self,
                                 const bonito_dist_t *peer_dist,
                                 float p) {
  if (peer_dist == NULL) {
    /* Always-on peer: CI = self.ppf(p).  Paper §7 degenerate case. */
    return bonito_dist_ppf(self, p);
  }

  /*
   * Two-node Bonito: bisect for t where F_self(t) * F_peer(t) = p.
   *
   * Bracket from paper §3.4 equations 10-11:
   *   lo = max(ppf_self(p),    ppf_peer(p))    → joint CDF(lo) ≤ p
   *   hi = max(ppf_self(q),    ppf_peer(q))    → joint CDF(hi) ≥ p
   *   where q = sqrt(p)
   *
   * 32 bisection steps give precision of (hi−lo)/2^32 ≈ sub-nanosecond on
   * any plausible bracket, far below any timing granularity that matters.
   */
  float q  = sqrtf(p);
  float lo = fmaxf(bonito_dist_ppf(self, p),    bonito_dist_ppf(peer_dist, p));
  float hi = fmaxf(bonito_dist_ppf(self, q),    bonito_dist_ppf(peer_dist, q));

  for (int i = 0; i < 32; i++) {
    float mid = (lo + hi) / 2.0f;
    if (bonito_dist_cdf(self, mid) * bonito_dist_cdf(peer_dist, mid) < p)
      lo = mid;
    else
      hi = mid;
  }
  return (lo + hi) / 2.0f;
}
