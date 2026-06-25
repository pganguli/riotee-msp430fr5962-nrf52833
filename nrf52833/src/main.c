/*
 * nRF52833 Bonito firmware (Riotee board).
 *
 * Implements the one-way Bonito connection protocol (NSDI 2022) for an
 * energy-harvesting node talking to an always-on laptop receiver:
 *
 *   1. The MSP430 (SPI master) hands over an opaque payload via the inter-chip
 *      (C2C) SPI link using the SET_PAYLOAD command. This firmware latches it
 *      immediately (ACCEPTED) and decouples the Bonito timing from the MSP430.
 *
 *   2. Each Bonito round:
 *        a. Get the current-round charging time c (sim: trace; real: cap
 * meter). b. Compute the connection interval CI = dist.ppf(0.99) from the
 *           CURRENT model — this is what the node announces this round.
 *        c. Build the BLE advert payload {seq, ci_ms, app_len, app[]} and
 *           transmit it (3× ADV_CH_ALL for reliability).
 *        d. Update the distribution model: dist.sgd_update(c).
 *        e. Sleep for CI (sim: riotee_sleep_ms; real: AM1805 alarm +
 * powerdown).
 *
 * The order (compute-CI → advertise → sgd_update) matches protocols.py:20-28
 * exactly — critical for the laptop-side CI verifier in scan.py to agree.
 *
 * Always-on peer simplification:
 *   The laptop's charging-time CDF is identically 1, so the joint CDF
 *   degenerates and CI = node.ppf(p). No back-channel is needed. The node
 *   announces its own CI in every advertisement.
 *
 * This firmware NEVER touches PIN_LED_CTRL (P0.03): the LED is shared with the
 * MSP430, which owns it.
 *
 * Simulation vs. real harvesting:
 *   Build with BONITO_SOURCE=sim  (default): uses charge_source_sim.c and
 *   rendezvous_sim.c, compiled with -DDISABLE_CAP_MONITOR for constant power.
 *   Build with BONITO_SOURCE=real: uses charge_source_real.c and
 *   rendezvous_real.c (stubs — implement before deploying).
 *
 * DNN integration:
 *   The MSP430 c2c_send_payload(buf, len) call is the only integration point.
 *   Replace the counter firmware on the MSP430 with your DNN result producer;
 *   the nRF52 and the C2C protocol are entirely agnostic to the payload
 * content.
 *
 * C2C pin mapping (SPIS2):
 *   P0.17 MOSI, P0.14 MISO, P0.18 SCK, P0.22 CS
 */
#include <stdbool.h>
#include <string.h>

#include "bonito/bonito.h"
#include "bonito/bonito_dist.h"
#include "bonito_payload.h"
#include "charge_source.h"
#include "nrf.h"
#include "protocol.h"
#include "rendezvous.h"
#include "riotee.h"
#include "riotee_ble.h"
#include "riotee_gpio.h"
#include "riotee_timing.h"

/* ── BLE identity ─────────────────────────────────────────────────────────────
 */

/* Stable random static address (unchanged from the counter firmware). */
static const uint8_t adv_address[] = {0x01, 0xEE, 0xC0, 0xFF, 0x03, 0x02};
static const char adv_name[] = "RIOTEE";

/* ── Shared state (SPIS ISR <-> main) ────────────────────────────────────────
 */

static uint8_t spis_rx[C2C_FRAME_LEN];
static uint8_t spis_tx[C2C_FRAME_LEN];

/* Latest payload latched from the MSP430 master. latest-wins: a new SET_PAYLOAD
 * overwrites the previous value if the Bonito loop has not consumed it yet. */
static volatile uint8_t g_app_buf[C2C_MAX_PAYLOAD];
static volatile uint8_t g_app_len;
static volatile uint8_t g_status = C2C_STATUS_IDLE;
static volatile bool g_new_payload = false;

/* ── SPIS2 driver ─────────────────────────────────────────────────────────────
 */

static void spis_init(void) {
  /* Release the C2C pins from SPIM0 (configured by the SDK's nvm_init). */
  NRF_SPIM0->ENABLE = (SPIM_ENABLE_ENABLE_Disabled << SPIM_ENABLE_ENABLE_Pos);
  NRF_SPIM0->PSEL.SCK = 0xFFFFFFFF;
  NRF_SPIM0->PSEL.MOSI = 0xFFFFFFFF;
  NRF_SPIM0->PSEL.MISO = 0xFFFFFFFF;
  NVIC_DisableIRQ(SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn);

  riotee_gpio_cfg_input(PIN_C2C_CLK, RIOTEE_GPIO_IN_NOPULL);
  riotee_gpio_cfg_input(PIN_C2C_MOSI, RIOTEE_GPIO_IN_NOPULL);
  riotee_gpio_cfg_input(PIN_C2C_MISO, RIOTEE_GPIO_IN_NOPULL);
  riotee_gpio_cfg_input(PIN_C2C_CS, RIOTEE_GPIO_IN_PULLUP);

  NRF_SPIS2->PSEL.SCK = PIN_C2C_CLK;
  NRF_SPIS2->PSEL.MOSI = PIN_C2C_MOSI;
  NRF_SPIS2->PSEL.MISO = PIN_C2C_MISO;
  NRF_SPIS2->PSEL.CSN = PIN_C2C_CS;

  /* Mode 0, MSB first — matches the MSP430 master. */
  NRF_SPIS2->CONFIG = (SPIS_CONFIG_CPHA_Leading << SPIS_CONFIG_CPHA_Pos) |
                      (SPIS_CONFIG_CPOL_ActiveHigh << SPIS_CONFIG_CPOL_Pos) |
                      (SPIS_CONFIG_ORDER_MsbFirst << SPIS_CONFIG_ORDER_Pos);

  NRF_SPIS2->DEF = C2C_STATUS_IDLE;
  NRF_SPIS2->ORC = 0x00;

  spis_tx[0] = C2C_STATUS_IDLE;
  NRF_SPIS2->RXD.PTR = (uint32_t)spis_rx;
  NRF_SPIS2->RXD.MAXCNT = C2C_FRAME_LEN;
  NRF_SPIS2->TXD.PTR = (uint32_t)spis_tx;
  NRF_SPIS2->TXD.MAXCNT = C2C_FRAME_LEN;

  NRF_SPIS2->SHORTS = SPIS_SHORTS_END_ACQUIRE_Msk;
  NRF_SPIS2->EVENTS_END = 0;
  NRF_SPIS2->EVENTS_ACQUIRED = 0;
  NRF_SPIS2->INTENSET = SPIS_INTENSET_END_Msk | SPIS_INTENSET_ACQUIRED_Msk;
  NRF_SPIS2->ENABLE = (SPIS_ENABLE_ENABLE_Enabled << SPIS_ENABLE_ENABLE_Pos);

  NVIC_SetPriority(SPIM2_SPIS2_SPI2_IRQn, 6);
  NVIC_EnableIRQ(SPIM2_SPIS2_SPI2_IRQn);

  /* Acquire the semaphore once to arm the first transfer. */
  NRF_SPIS2->TASKS_ACQUIRE = 1;
}

void SPIM2_SPIS2_SPI2_IRQHandler(void) {
  uint8_t i, len;

  if (NRF_SPIS2->EVENTS_END) {
    NRF_SPIS2->EVENTS_END = 0;

    if (spis_rx[0] == C2C_CMD_SET_PAYLOAD) {
      /* Latch the opaque payload (latest-wins if Bonito loop is slow). */
      len = spis_rx[1];
      if (len > C2C_MAX_PAYLOAD) len = C2C_MAX_PAYLOAD;
      g_app_len = len;
      for (i = 0; i < len; i++) g_app_buf[i] = spis_rx[2 + i];
      g_status = C2C_STATUS_ACCEPTED;
      g_new_payload = true;
    }
    /* GET_STATUS: no action needed; updated status is already in spis_tx[0]
     * from the ACQUIRED ISR of the previous transaction. */
  }

  if (NRF_SPIS2->EVENTS_ACQUIRED) {
    NRF_SPIS2->EVENTS_ACQUIRED = 0;
    /* Expose current status for the NEXT transaction. */
    spis_tx[0] = g_status;
    NRF_SPIS2->DEF = g_status;
    NRF_SPIS2->RXD.PTR = (uint32_t)spis_rx;
    NRF_SPIS2->RXD.MAXCNT = C2C_FRAME_LEN;
    NRF_SPIS2->TXD.PTR = (uint32_t)spis_tx;
    NRF_SPIS2->TXD.MAXCNT = C2C_FRAME_LEN;
    NRF_SPIS2->TASKS_RELEASE = 1;
  }
}

/* ── Riotee lifecycle hooks ───────────────────────────────────────────────────
 */

void lateinit(void) {
  riotee_ble_init();
  spis_init();
  /* Deliberately do NOT configure PIN_LED_CTRL — the MSP430 owns the LED. */
}

/* ── Bonito scheduler loop ────────────────────────────────────────────────────
 */

int main(void) {
  bonito_adv_payload_t adv_payload;
  bonito_dist_t dist;
  riotee_ble_adv_cfg_t adv_cfg;
  uint8_t local_app[C2C_MAX_PAYLOAD];
  uint8_t local_app_len;
  uint16_t seq = 0;
  float c, ci;
  uint8_t i;

  /* Initialise Bonito distribution with priors that match the synthetic trace:
   * mean ~1.0 s, variance 0.25 s^2 (sigma = 0.5 s), learning rate = 0.01. */
  bonito_dist_init(&dist, 1.0f, 0.25f, 0.01f);

  /* Configure BLE advertising once.
   *
   * ble.c payload budget: 31 bytes total = 3 (flags) + 2 (name header) +
   * name_len + 4 (manufacturer header) + data_len.  Real limit:
   *   name_len + data_len ≤ 22
   * With name "RIOTEE" (6 bytes): data_len ≤ 16 = BONITO_ADV_HDR_LEN(7) +
   * BONITO_ADV_APP_MAX(9).
   *
   * Filter on the laptop by address + Nordic manufacturer ID (0x0059). */
  memset(&adv_payload, 0, sizeof(adv_payload));
  adv_cfg.addr            = adv_address;
  adv_cfg.name            = adv_name;
  adv_cfg.name_len        = sizeof(adv_name) - 1; /* 6, excludes NUL */
  adv_cfg.data            = &adv_payload;
  adv_cfg.data_len        = sizeof(adv_payload);  /* BONITO_ADV_HDR_LEN + BONITO_ADV_APP_MAX = 16 */
  adv_cfg.manufacturer_id = RIOTEE_BLE_ADV_MNF_NORDIC;
  riotee_ble_adv_cfg(&adv_cfg);

  for (;;) {
    /* ── Wait for a payload from the MSP430 ─────────────────────────────── */
    while (!g_new_payload) enter_low_power();
    g_new_payload = false;

    /* Snapshot payload atomically (ISR may overwrite any time). */
    local_app_len = g_app_len;
    if (local_app_len > C2C_MAX_PAYLOAD) local_app_len = C2C_MAX_PAYLOAD;
    for (i = 0; i < local_app_len; i++) local_app[i] = g_app_buf[i];

    /* ── Bonito round (order mirrors protocols.py:20-28) ──────────────── */

    /* a) Charging time observation for this round. */
    c = charge_source_next();

    /* b) Connection interval from the CURRENT model (before SGD update). */
    ci = bonito_connection_interval(&dist, BONITO_TARGET_PROB);

    /* Clamp CI to a sane range: at least 100 ms, at most 30 s. */
    if (ci < 0.1f) ci = 0.1f;
    if (ci > 30.0f) ci = 30.0f;

    /* c) Build and transmit the advertisement.
     * Clip app payload to the advert budget; C2C can carry more (16 B) than
     * fits in the BLE manufacturer-data field (9 B). */
    adv_payload.seq    = seq;
    adv_payload.ci_ms  = (uint32_t)(ci * 1000.0f);
    adv_payload.app_len = (local_app_len <= BONITO_ADV_APP_MAX)
                              ? local_app_len : (uint8_t)BONITO_ADV_APP_MAX;
    memset(adv_payload.app, 0, sizeof(adv_payload.app));
    memcpy(adv_payload.app, local_app, adv_payload.app_len);

    /* 16 bursts on all channels → 48 packets total. */
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);
    riotee_ble_advertise(ADV_CH_ALL);

    /* d) Update model with this round's charging time. */
    bonito_dist_sgd_update(&dist, c);

    seq++;

    /* e) Sleep until the next connection interval. */
    rendezvous_wait(ci);
  }
}
