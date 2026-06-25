#ifndef BONITO_H
#define BONITO_H

/*
 * Bonito connection-interval engine — C port of neslab/bonito protocols.py.
 *
 * One-way (always-on peer) variant:
 *   Because the laptop is always-on, its charging-time CDF is identically 1.
 *   The inverse JOINT CDF of (node_dist, always_on) at probability p therefore
 *   reduces to node_dist.ppf(p).  No back-channel is needed.
 *
 * Per-round usage (mirrors protocols.py:20-28 order — critical):
 *
 *   float c  = charge_source_next();          // get this round's charging time
 *   float ci = bonito_connection_interval(&dist, p);  // use CURRENT model
 *   bool  ok = (c <= ci);                     // success check
 *   build_and_send_advert(seq, ci, payload);  // announce ci
 *   bonito_dist_sgd_update(&dist, c);         // update model AFTER using it
 *   rendezvous_wait(ci);                      // sleep until next round
 *
 * Extension point: to support a real two-distribution joint CDF (symmetric
 * Bonito with a second harvesting node), replace the body of
 * bonito_connection_interval() with a bisection root-finder over
 *   f(x) = dist1.cdf(x) * dist2.cdf(x) - p
 * ported from bisection.py:16-57.  The caller signature is unchanged.
 */

#include "bonito_dist.h"

/* Default target success probability (matches neslab.bonito default). */
#define BONITO_TARGET_PROB 0.99f

/*
 * Compute the connection interval (seconds) from the current distribution
 * at target probability p.
 *
 * For the one-way / always-on peer case this is simply dist->ppf(p).
 */
float bonito_connection_interval(const bonito_dist_t* dist, float p);

#endif /* BONITO_H */
