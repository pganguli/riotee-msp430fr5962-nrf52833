/*
 * Bonito connection-interval engine.
 *
 * See bonito.h for the design rationale and per-round usage pattern.
 */
#include "bonito.h"

float bonito_connection_interval(const bonito_dist_t* dist, float p) {
  /*
   * One-way / always-on peer: CI = dist.ppf(p).
   *
   * To port symmetric Bonito (two harvesting nodes), replace this with a
   * bisection search over f(x) = dist1.cdf(x) * dist2.cdf(x) - p,
   * ported from nrf52833/bonito/neslab/bonito/bisection.py:16-57.
   * The call site in main.c does not need to change.
   */
  return bonito_dist_ppf(dist, p);
}
