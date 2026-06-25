/*
 * Shared wire protocol for the MSP430FR5962 <-> nRF52833 C2C SPI link.
 *
 * The MSP430 is the SPI master; the nRF52 is the SPI slave. Every transaction
 * is a fixed-length, little-endian frame initiated by the master:
 *
 *   byte 0        : command
 *   byte 1        : payload length (only meaningful for SET_PAYLOAD)
 *   byte 2..17    : opaque payload bytes (SET_PAYLOAD) or ignored (GET_STATUS)
 *
 * Full-duplex: the nRF52 slave always keeps byte 0 of its TX buffer equal to
 * the current status, so the first byte the master receives in any transaction
 * is the latest status.
 *
 * Handoff semantics (fire-and-forget):
 *   SET_PAYLOAD latches the payload immediately in the SPIS ISR and replies
 *   ACCEPTED on the NEXT transaction. The nRF52 Bonito loop picks it up
 *   asynchronously — the master does NOT wait for it to be transmitted over BLE.
 *   This keeps the MSP430 inference/counter loop decoupled from the BLE cadence.
 *
 * Integration contract for the DNN repo:
 *   Call c2c_send_payload(buf, len) with up to C2C_MAX_PAYLOAD bytes.
 *   The nRF52 will broadcast the blob at the next Bonito connection interval.
 *   The counter firmware in this repo uses a 4-byte uint32_t as the default
 *   test payload.
 */
#ifndef C2C_PROTOCOL_H
#define C2C_PROTOCOL_H

#include <stdint.h>

/* Commands (master -> slave), frame byte 0. */
#define C2C_CMD_SET_PAYLOAD 0x01u /* latch opaque payload (bytes 1+len+data) */
#define C2C_CMD_GET_STATUS  0x02u /* read status only; payload bytes ignored  */

/* Status (slave -> master), response byte 0. */
#define C2C_STATUS_IDLE     0x00u /* no payload pending                       */
#define C2C_STATUS_ACCEPTED 0x01u /* payload latched (fast ack, not yet sent) */

/* Maximum opaque payload size in bytes. Limited by BLE advert budget:
 *   31 bytes PDU - 4 bytes manufacturer-specific AD header
 *   - 7 bytes Bonito header (seq u16 + ci_ms u32 + app_len u8) = 20 bytes max.
 * We round down to 16 for a clean power-of-two and future margin. */
#define C2C_MAX_PAYLOAD 16u

/* Fixed transaction length: command + length + payload. */
#define C2C_FRAME_LEN (2u + C2C_MAX_PAYLOAD)

#endif /* C2C_PROTOCOL_H */
