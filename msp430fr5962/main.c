/*
 * MSP430FR5962 application firmware (Riotee board).
 *
 * Maintains a power-cycle-persistent counter in internal FRAM and hands each
 * value to the nRF52833 over the inter-chip (C2C) SPI link for BLE advertising
 * via the Bonito protocol.
 *
 * Flow:
 *   1. On boot, read the last committed counter from FRAM (0 if never set).
 *   2. Increment the counter (in RAM, not yet committed to FRAM).
 *   3. Send the value to the nRF52 with c2c_send_payload() and wait for
 *      C2C_STATUS_ACCEPTED — the nRF52's SPIS ISR latches the payload within
 *      a few hundred microseconds and replies ACCEPTED on the next poll tick
 *      (~100 ms). The nRF52 Bonito loop broadcasts it asynchronously.
 *   4. On ACCEPTED: commit to FRAM, wait ~80 ms (human-visible LED gap),
 * repeat.
 *
 * This design is intentionally decoupled from the Bonito timing: the MSP430
 * increments the counter as fast as the ACCEPTED acknowledgement allows
 * (roughly one value per 100-200 ms), while the nRF52 broadcasts at the Bonito
 * connection interval (roughly 0.7-1.5 s). The nRF52 keeps the latest value
 * (latest-wins), so some counter values may be skipped in the BLE output.
 *
 * DNN integration:
 *   c2c_send_payload(buf, len) is the ONLY function the DNN repo needs to call.
 *   Replace the counter increment below with your inference result and pass it
 *   as buf/len.  The nRF52 and the C2C protocol are entirely agnostic to the
 *   payload content (up to C2C_MAX_PAYLOAD = 16 bytes).
 *
 * Roles: the MSP430 is the SPI master (eUSCI_A0); the nRF52 is the SPI slave
 * exposing a status register. The master pushes the payload, then polls the
 * status (poll cadence paced by a Timer_A interrupt, CPU in LPM3 between
 * polls).
 *
 * LED feedback (PJ.0, shared with the nRF52 — the nRF52 firmware must leave it
 * alone):
 *   - First boot after programming (counter reset to 0):  30 rapid blinks
 *   - Restored value after a power cycle/reset:            3 rapid blinks
 *   - While a value is out for handoff:                    LED on
 *   - ACCEPTED received (nRF52 latched the payload):       LED off
 *   - 3 s timeout without ACCEPTED (nRF52 unresponsive):   3 rapid blinks,
 * retry
 *
 * C2C pin mapping (eUSCI_A0, secondary module function SEL=10):
 *   UCA0SIMO = P2.0, UCA0SOMI = P2.1, UCA0CLK = P1.5, chip-select = P1.4 (GPIO)
 */
#include <msp430.h>
#include <stdint.h>

#include "protocol.h"

#define LED_BIT BIT0 /* PJ.0 */
#define CS_BIT BIT4  /* P1.4, C2C chip-select driven as GPIO */

/* Marks that the FRAM-backed counter has been initialised at least once. */
#define INIT_MAGIC 0xBEEFu

/* Timer_A runs from ACLK = VLOCLK (~9.4 kHz internal low-power oscillator).
 * The exact VLO frequency varies, but only coarse timing is needed here. */
#define POLL_TICKS 940u   /* ~100 ms status-poll cadence                */
#define TIMEOUT_POLLS 30u /* 30 × ~100 ms ~= 3 s without an ACCEPTED   */

/* FRAM-resident, retained across resets and power cycles. */
#pragma PERSISTENT(g_counter)
uint32_t g_counter = 0;
#pragma PERSISTENT(g_initialized)
uint16_t g_initialized = 0;

static volatile uint8_t timer_tick; /* set by the Timer_A ISR  */
static volatile uint8_t spi_done;   /* set by the eUSCI_A0 ISR */
static volatile uint8_t spi_rx;     /* last byte received      */

static void clock_init(void) {
  CSCTL0_H = CSKEY_H; /* unlock CS registers     */
  CSCTL1 = DCOFSEL_0; /* DCO ~1 MHz              */
  CSCTL2 =
      SELA__VLOCLK | SELS__DCOCLK | SELM__DCOCLK; /* ACLK=VLO, S/MCLK=DCO */
  CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;           /* no dividers             */
  CSCTL0_H = 0;                                   /* lock CS registers       */
}

static void led_on(void) { PJOUT |= LED_BIT; }
static void led_off(void) { PJOUT &= ~LED_BIT; }

/* Boot-time visual feedback only — a brief busy delay here is acceptable. */
static void blink_rapid(uint8_t n) {
  uint8_t i;
  for (i = 0; i < n; i++) {
    led_on();
    __delay_cycles(50000); /* ~50 ms at 1 MHz */
    led_off();
    __delay_cycles(50000);
  }
}

static void spi_init(void) {
  /* Chip-select as GPIO output, idle high. */
  P1OUT |= CS_BIT;
  P1DIR |= CS_BIT;

  /* Route eUSCI_A0 onto the C2C pins (secondary module function: SEL1=1,
   * SEL0=0). */
  P2SEL0 &= ~(BIT0 | BIT1);
  P2SEL1 |= (BIT0 | BIT1);
  P1SEL0 &= ~BIT5;
  P1SEL1 |= BIT5;

  UCA0CTLW0 = UCSWRST; /* hold eUSCI in reset */
  /* SPI master, synchronous, MSB first, mode 0 (CPOL=0 → UCCKPL=0,
   * CPHA=0 → UCCKPH=1). */
  UCA0CTLW0 |= UCMST | UCSYNC | UCMSB | UCCKPH | UCSSEL__SMCLK;
  UCA0BRW = 16;          /* ~1 MHz / 16 ~ 62.5 kHz */
  UCA0CTLW0 &= ~UCSWRST; /* release for operation   */
}

static void timer_init(void) {
  TA0CCR0 = POLL_TICKS - 1;
  TA0CCTL0 = CCIE;
  TA0CTL = TASSEL__ACLK | MC__UP | TACLR;
}

/* Exchange one byte; sleeps in LPM0 (SMCLK stays up) until the eUSCI RX ISR
 * fires. */
static uint8_t spi_xfer(uint8_t tx) {
  spi_done = 0;
  UCA0IFG &= ~UCRXIFG;
  UCA0IE |= UCRXIE;
  UCA0TXBUF = tx;
  while (!spi_done) __bis_SR_register(LPM0_bits | GIE);
  UCA0IE &= ~UCRXIE;
  return spi_rx;
}

/*
 * Send an opaque payload to the nRF52 via C2C and return the response status.
 *
 * Frame layout: [C2C_CMD_SET_PAYLOAD][len][b0..b_{len-1}][pad to C2C_FRAME_LEN]
 *
 * This is the function the DNN repo should call:
 *   c2c_send_payload((uint8_t *)&result, sizeof(result));
 *
 * Returns the status byte clocked back in the first response byte.
 */
static uint8_t c2c_send_payload(const uint8_t* buf, uint8_t len) {
  uint8_t frame[C2C_FRAME_LEN];
  uint8_t status;
  uint8_t i;

  if (len > C2C_MAX_PAYLOAD) len = C2C_MAX_PAYLOAD;

  frame[0] = C2C_CMD_SET_PAYLOAD;
  frame[1] = len;
  for (i = 0; i < len; i++) frame[2 + i] = buf[i];
  for (i = len; i < C2C_MAX_PAYLOAD; i++) frame[2 + i] = 0; /* pad */

  P1OUT &= ~CS_BIT; /* assert chip-select */
  __delay_cycles(20);
  status = spi_xfer(frame[0]);
  for (i = 1; i < C2C_FRAME_LEN; i++) spi_xfer(frame[i]);
  P1OUT |= CS_BIT; /* release chip-select */
  return status;
}

/* Poll the nRF52 status register (GET_STATUS transaction). */
static uint8_t c2c_get_status(void) {
  uint8_t status;
  uint8_t i;

  P1OUT &= ~CS_BIT;
  __delay_cycles(20);
  status = spi_xfer(C2C_CMD_GET_STATUS);
  for (i = 1; i < C2C_FRAME_LEN; i++) spi_xfer(0);
  P1OUT |= CS_BIT;
  return status;
}

int main(void) {
  uint32_t working;
  uint8_t status;
  uint16_t polls;

  WDTCTL = WDTPW | WDTHOLD;

  /* LED on PJ.0. */
  PJOUT &= ~LED_BIT;
  PJDIR |= LED_BIT;

  PM5CTL0 &= ~LOCKLPM5; /* leave high-impedance mode */

  clock_init();

  if (g_initialized != INIT_MAGIC) {
    /* First boot after programming: initialise the counter. */
    g_counter = 0;
    g_initialized = INIT_MAGIC;
    blink_rapid(30);
  } else {
    /* Counter restored from FRAM across a power cycle/reset. */
    blink_rapid(3);
  }

  spi_init();
  timer_init();
  __bis_SR_register(GIE);

  working = g_counter; /* last committed value */

  for (;;) {
    working++; /* next value to advertise (uncommitted, lives in RAM) */

    status = C2C_STATUS_IDLE;
    for (;;) { /* (re)send-and-wait loop */
      led_on();
      c2c_send_payload((const uint8_t*)&working, sizeof(working));

      polls = 0;
      for (;;) { /* poll for ACCEPTED, sleeping between ticks */
        timer_tick = 0;
        while (!timer_tick) __bis_SR_register(LPM3_bits | GIE);

        status = c2c_get_status();
        if (status == C2C_STATUS_ACCEPTED) break;
        if (++polls >= TIMEOUT_POLLS) break; /* ~3 s without ACCEPTED */
      }

      if (status == C2C_STATUS_ACCEPTED) {
        led_off();
        g_counter = working;   /* commit to FRAM (write is immediate) */
        __delay_cycles(80000); /* ~80 ms gap so the off is human-visible */
        break;
      }

      /* Timeout: 3 rapid blinks, then retry the same value. */
      blink_rapid(3);
    }
  }
}

#pragma vector = TIMER0_A0_VECTOR
__interrupt void timer0_a0_isr(void) {
  timer_tick = 1;
  __bic_SR_register_on_exit(LPM3_bits); /* wake the main loop */
}

#pragma vector = USCI_A0_VECTOR
__interrupt void usci_a0_isr(void) {
  switch (__even_in_range(UCA0IV, USCI_SPI_UCTXIFG)) {
    case USCI_SPI_UCRXIFG:
      spi_rx = UCA0RXBUF;
      spi_done = 1;
      __bic_SR_register_on_exit(LPM0_bits); /* wake spi_xfer */
      break;
    default:
      break;
  }
}
