#ifndef BONITO_PAYLOAD_H
#define BONITO_PAYLOAD_H

/*
 * BLE advertisement manufacturer payload layout for the Bonito protocol.
 *
 * Fields are little-endian, packed, placed in the Nordic manufacturer-specific
 * AD element (company ID 0x0059).  Definition is mirrored in scan.py.
 *
 * Layout (16 bytes; fits with name="RIOTEE" and data_len ≤ 16):
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *      0      2   seq          — round index; wraps at 2^16. Loss detection.
 *      2      1   model_type   — distribution type (BONITO_MODEL_NORMAL = 0x01)
 *      3      1   app_len      — valid bytes in app[]; 0..BONITO_ADV_APP_MAX
 *      4      4   mean         — Normal distribution mean (seconds, float32)
 *      8      4   var          — Normal distribution variance (seconds², float32)
 *     12      4   app[]        — opaque app payload (u32 counter today)
 *
 * Total: 16 bytes.
 *
 * The CI is NOT transmitted.  Each peer computes it independently from the
 * received model parameters:
 *   Always-on peer:   CI = mean + sqrt(var) * Phi^{-1}(p)           (paper §7)
 *   Two-node Bonito:  CI = bisect(F_self(t)*F_peer(t) = p)         (paper §3.4)
 *
 * This mirrors the paper's Fig. 12 packet format: nodes exchange model type +
 * parameters so each side can compute the joint CI independently.
 *
 * Budget: ble.c payload[31] = 3(flags)+2(name-hdr)+6("RIOTEE")+4(mfr-hdr)+data
 * → name_len + data_len ≤ 22.  With name_len=6: data_len ≤ 16.
 * BONITO_ADV_APP_MAX = data_len − BONITO_ADV_HDR_LEN = 16 − 12 = 4.
 */

#include <stdint.h>

/* Distribution type codes (paper Fig. 12 model_type byte). */
#define BONITO_MODEL_NORMAL 0x01u

/* Fixed header: seq(2) + model_type(1) + app_len(1) + mean(4) + var(4). */
#define BONITO_ADV_HDR_LEN 12u

/*
 * Maximum opaque application payload per advertisement.
 * 4 bytes is sufficient for a u32 counter (the default test payload).
 */
#define BONITO_ADV_APP_MAX 4u

/* Full structure as written into the BLE manufacturer data buffer.
 * All 16 bytes are always transmitted.
 * mean and var land at naturally-aligned offsets (4 and 8) despite packed. */
typedef struct {
  uint16_t seq;                     /* round index                    */
  uint8_t  model_type;              /* distribution type              */
  uint8_t  app_len;                 /* valid bytes in app[]           */
  float    mean;                    /* charging-time mean (s)         */
  float    var;                     /* charging-time variance (s²)    */
  uint8_t  app[BONITO_ADV_APP_MAX]; /* opaque app payload             */
} __attribute__((packed)) bonito_adv_payload_t;

#endif /* BONITO_PAYLOAD_H */
