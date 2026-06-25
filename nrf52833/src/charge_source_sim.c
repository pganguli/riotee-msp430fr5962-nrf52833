/*
 * Simulated charging-time source (BONITO_SOURCE=sim).
 *
 * Replays CHARGE_TRACE[] from charge_trace.h — a deterministic synthetic trace
 * that matches what the laptop monitor (scan.py) uses to replay the Bonito SGD
 * and verify the node's advertised connection interval.
 */
#include <stdint.h>

#include "charge_source.h"
#include "charge_trace.h"

static uint32_t g_idx = 0;

float charge_source_next(void) {
  float c = CHARGE_TRACE[g_idx % CHARGE_TRACE_LEN];
  g_idx++;
  return c;
}
