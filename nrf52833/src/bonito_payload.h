#ifndef BONITO_PAYLOAD_H
#define BONITO_PAYLOAD_H

/*
 * BLE advertisement manufacturer payload layout for the Bonito protocol.
 *
 * This structure is placed in the BLE PDU's manufacturer-specific AD element
 * (Nordic company ID 0x0059) and has the same definition on the laptop side
 * (scan.py).  Fields are little-endian, packed.
 *
 * Budget: 31 bytes PDU - 4 bytes AD header (len + type + mfr_id) = 27 bytes
 * available.  With name_len = 0 (name AD element omitted) the full 27 bytes
 * are available for this structure.  We use 23 bytes to leave a small margin.
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *      0      2   seq      — round index; wraps at 2^16. Used for loss
 *                           detection and for the laptop to index into the
 *                           shared charging-time trace for CI verification.
 *      2      4   ci_ms    — Bonito connection interval in milliseconds
 *                           (node's ppf(0.99) result, as announced this round).
 *      6      1   app_len  — number of valid bytes in app[];
 * 0..BONITO_ADV_APP_MAX 7    app_len  app[] — opaque application payload (u32
 * counter today, DNN inference result in the deployed version).
 *
 * Total: 7 + app_len bytes (≤ 23).
 *
 * The BLE adv_cfg.data_len should be set to (BONITO_ADV_HDR_LEN + app_len)
 * before each riotee_ble_advertise() call.
 */

#include <stdint.h>

/* Byte length of the fixed header (seq + ci_ms + app_len). */
#define BONITO_ADV_HDR_LEN 7u

/*
 * Maximum opaque application payload per advertisement.
 *
 * Budget (ble.c): payload[31] = 3 (flags AD) + 2 (name header) + name_len +
 *   4 (manufacturer AD header) + data_len.  Real limit: name_len+data_len ≤ 22.
 * With name="RIOTEE" (6 bytes): data_len ≤ 16.
 * BONITO_ADV_APP_MAX = data_len - BONITO_ADV_HDR_LEN = 16 - 7 = 9.
 */
#define BONITO_ADV_APP_MAX 9u

/* Full structure as written into the BLE manufacturer data buffer.
 * Only the first (BONITO_ADV_HDR_LEN + app_len) bytes are transmitted. */
typedef struct {
  uint16_t seq;                    /* round index                  */
  uint32_t ci_ms;                  /* connection interval (ms)     */
  uint8_t app_len;                 /* valid bytes in app[]         */
  uint8_t app[BONITO_ADV_APP_MAX]; /* opaque app payload           */
} __attribute__((packed)) bonito_adv_payload_t;

#endif /* BONITO_PAYLOAD_H */
