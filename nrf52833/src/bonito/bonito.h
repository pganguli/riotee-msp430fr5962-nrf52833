#ifndef BONITO_H
#define BONITO_H

/*
 * Bonito connection-interval engine — C port of neslab/bonito protocols.py.
 *
 * Per-round usage (mirrors protocols.py:20-28 order — critical):
 *
 *   float c  = charge_source_next();
 *   float ci = bonito_connection_interval(&dist, NULL, p);  // always-on peer
 *   build_advert(seq, BONITO_MODEL_NORMAL, dist.mean, dist.var, app);
 *   bonito_dist_sgd_update(&dist, c);
 *   rendezvous_wait(ci);
 *
 * Always-on peer (current single-node setup):
 *   Pass peer_dist = NULL.  CI = self.ppf(p).  Paper §7 confirms this is the
 *   correct degenerate case: when the peer's charging-time CDF ≡ 1, the inverse
 *   joint CDF reduces to the node's own ppf(p).
 *
 * Two-node symmetric Bonito (future, >= 2 intermittent nodes):
 *   Pass peer_dist = &received_model (populated from the peer's advertisement).
 *   CI is found by bisecting f(t) = F_self(t) * F_peer(t) − p = 0.
 *   Both nodes exchange model_type + mean + var in each BLE advertisement
 *   (paper Fig. 12) and independently arrive at the same CI.
 *
 * Wire format note: the CI itself is NOT advertised.  Each peer recomputes it
 * locally from the received model parameters.  See bonito_payload.h.
 */

#include "bonito_dist.h"

/* Default target success probability (matches neslab.bonito default). */
#define BONITO_TARGET_PROB 0.99f

/*
 * Compute the connection interval (seconds).
 *
 *   self      — node's own current charging-time distribution
 *   peer_dist — peer's distribution as received from the last advertisement.
 *               Pass NULL when the peer is always-on (one-way Bonito).
 *   p         — target success probability (typically BONITO_TARGET_PROB)
 *
 * Returns: CI in seconds (before the caller's min/max clamp).
 */
float bonito_connection_interval(const bonito_dist_t *self,
                                 const bonito_dist_t *peer_dist,
                                 float p);

#endif /* BONITO_H */
