#ifndef BONITO_DIST_H
#define BONITO_DIST_H

/*
 * C port of neslab/bonito NormalDistribution (distributions.py:58-73).
 *
 * Maintains a Normal(mean, var) model of charging times, updated via online
 * SGD on each new observation x:
 *
 *   dll[0] = x - mean          (mean gradient)
 *   dll[1] = (x-mean)^2 - var  (variance gradient)
 *   mp += eta * dll
 *
 * ppf(p) uses Acklam's rational approximation for Phi^{-1} (inverse standard
 * normal CDF). Maximum absolute error < 4.5e-4, more than adequate for the
 * single-precision Bonito CI computation at p = 0.99.
 *
 * cdf(x) uses erff() from libm (available: Cortex-M4F hard FPU, -lm).
 */

#include <stdint.h>

typedef struct {
  float mean; /* current mean estimate (seconds) */
  float var;  /* current variance estimate (seconds^2) */
  float eta;  /* SGD learning rate (default 0.01) */
} bonito_dist_t;

/* Initialise distribution with given priors and learning rate. */
void bonito_dist_init(bonito_dist_t* d, float mean, float var, float eta);

/* Update model with a new charging-time observation x (seconds). */
void bonito_dist_sgd_update(bonito_dist_t* d, float x);

/* Cumulative distribution function F(x). */
float bonito_dist_cdf(const bonito_dist_t* d, float x);

/* Inverse CDF (percent-point function) F^{-1}(p). */
float bonito_dist_ppf(const bonito_dist_t* d, float p);

#endif /* BONITO_DIST_H */
