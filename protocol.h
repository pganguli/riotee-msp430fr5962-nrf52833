/*
 * Shared wire protocol for the MSP430FR5962 <-> nRF52833 C2C SPI link.
 *
 * The MSP430 is the SPI master; the nRF52 is the SPI slave exposing a status
 * register. Every transaction is a fixed-length, little-endian frame initiated
 * by the master:
 *
 *     byte 0 : command
 *     byte 1 : counter[7:0]   (only meaningful for SET_COUNTER)
 *     byte 2 : counter[15:8]
 *     byte 3 : counter[23:16]
 *     byte 4 : counter[31:24]
 *     byte 5 : pad
 *
 * Full-duplex: the nRF52 slave always keeps byte 0 of its TX buffer equal to
 * the current status, so the first byte the master receives in any transaction
 * is the latest status.
 */
#ifndef C2C_PROTOCOL_H
#define C2C_PROTOCOL_H

#include <stdint.h>

/* Commands (master -> slave), frame byte 0. */
#define C2C_CMD_SET_COUNTER \
  0x01u /* latch counter (bytes 1..4), start advertising */
#define C2C_CMD_GET_STATUS \
  0x02u /* read status only; counter bytes ignored        */

/* Status (slave -> master), response byte 0. */
#define C2C_STATUS_IDLE 0x00u /* no counter pending                       */
#define C2C_STATUS_BUSY 0x01u /* advertising the latched counter          */
#define C2C_STATUS_DONE 0x02u /* finished the ~1 s advertising burst      */

/* Fixed transaction length in bytes. */
#define C2C_FRAME_LEN 6

typedef uint32_t counter_t;

#endif /* C2C_PROTOCOL_H */
