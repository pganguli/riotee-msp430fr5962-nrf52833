/*
 * NormalDistribution — C port of neslab/bonito distributions.py:58-73.
 *
 * See bonito_dist.h for the full description.
 */
#include "bonito_dist.h"

#include <math.h>

/* ── Acklam's rational approximation for the standard normal quantile ────── */

/* Coefficients from Peter J. Acklam's algorithm (2010).
 * Maximum absolute error < 4.5e-4, adequate for single-precision at p = 0.99.
 * Reference:
 * https://web.archive.org/web/20151030215612/http://home.online.no/~pjacklam/notes/invnorm/
 */
static const float A[6] = {-3.969683028665376e+01f, 2.209460984245205e+02f,
                           -2.759285104469687e+02f, 1.383577518672690e+02f,
                           -3.066479806614716e+01f, 2.506628277459239e+00f};
static const float B[5] = {-5.447609879822406e+01f, 1.615858368580409e+02f,
                           -1.556989798598866e+02f, 6.680131188771972e+01f,
                           -1.328068155288572e+01f};
static const float C[6] = {-7.784894002430293e-03f, -3.223964580411365e-01f,
                           -2.400758277161838e+00f, -2.549732539343734e+00f,
                           4.374664141464968e+00f,  2.938163982698783e+00f};
static const float D[4] = {7.784695709041462e-03f, 3.223907438521585e-01f,
                           2.445134137142996e+00f, 3.754408661907416e+00f};

/* Probit function: Phi^{-1}(p) — inverse standard normal CDF. */
static float probit(float p) {
  const float P_LO = 0.02425f;
  const float P_HI = 1.0f - P_LO;
  float q, r;

  if (p <= 0.0f) return -3.402823466e+38f; /* -FLT_MAX */
  if (p >= 1.0f) return 3.402823466e+38f;  /*  FLT_MAX */

  if (p < P_LO) {
    /* Lower tail. */
    q = sqrtf(-2.0f * logf(p));
    return (((((C[0] * q + C[1]) * q + C[2]) * q + C[3]) * q + C[4]) * q +
            C[5]) /
           ((((D[0] * q + D[1]) * q + D[2]) * q + D[3]) * q + 1.0f);
  }

  if (p <= P_HI) {
    /* Central region. */
    q = p - 0.5f;
    r = q * q;
    return (((((A[0] * r + A[1]) * r + A[2]) * r + A[3]) * r + A[4]) * r +
            A[5]) *
           q /
           (((((B[0] * r + B[1]) * r + B[2]) * r + B[3]) * r + B[4]) * r +
            1.0f);
  }

  /* Upper tail by symmetry. */
  return -probit(1.0f - p);
}

/* ── Public API ───────────────────────────────────────────────────────────────
 */

void bonito_dist_init(bonito_dist_t* d, float mean, float var, float eta) {
  d->mean = mean;
  d->var = var;
  d->eta = eta;
}

void bonito_dist_sgd_update(bonito_dist_t* d, float x) {
  /* Compute both gradients from the OLD mean before updating either.
   * Matches the numpy vector update in distributions.py:31. */
  float diff = x - d->mean;
  d->mean += d->eta * diff;
  d->var += d->eta * (diff * diff - d->var);
}

float bonito_dist_cdf(const bonito_dist_t* d, float x) {
  /* Phi((x - mean) / sigma) using the erff relation:
   *   Phi(z) = 0.5 * (1 + erf(z / sqrt(2))) */
  float sigma = sqrtf(d->var);
  return 0.5f * (1.0f + erff((x - d->mean) / (1.41421356f * sigma)));
}

float bonito_dist_ppf(const bonito_dist_t* d, float p) {
  return d->mean + sqrtf(d->var) * probit(p);
}
