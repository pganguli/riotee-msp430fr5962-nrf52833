/*
 * nRF52833 application firmware (Riotee board).
 *
 * Acts as a commandable BLE radio for the MSP430FR5962. The MSP430 (SPI master)
 * pushes a counter value over the inter-chip (C2C) SPI link; this firmware
 * (SPI slave) advertises that value over BLE for ~1 s, then reports completion
 * back through a status register the master polls.
 *
 * Flow:
 *   - The SPI-slave ISR latches a counter on SET_COUNTER and sets status =
 * BUSY.
 *   - main() advertises the latched value for ~1 s (4 rounds, 250 ms apart),
 *     then sets status = DONE.
 *   - Every transaction returns the current status in response byte 0, so the
 *     master learns when advertising has finished.
 *
 * This firmware NEVER touches PIN_LED_CTRL (P0.03): that LED is shared with the
 * MSP430, which owns it.
 *
 * SPI slave uses the SPIS2 instance. SPIS0 (nvm), SPIS1 (i2c) and SPIM3 (spic)
 * are already used by the SDK; SPIS2's IRQ vector is free. The C2C pins are
 * configured by the SDK's nvm_init() for SPIM0 (master) - we release them from
 * SPIM0 and re-route them to SPIS2 here. Under constant power with
 * DISABLE_CAP_MONITOR the checkpoint/nvm path never runs, so the link is free.
 */
#include <string.h>

#include "nrf.h"
#include "protocol.h"
#include "riotee.h"
#include "riotee_ble.h"
#include "riotee_gpio.h"
#include "riotee_timing.h"

/* BLE advertising identity. */
static const uint8_t adv_address[] = {0x01, 0xEE, 0xC0, 0xFF, 0x03, 0x02};
static const char adv_name[] = "RIOTEE";

/* Payload advertised over BLE; riotee_ble_advertise() re-reads this each call.
 */
static counter_t adv_payload;

/* SPIS DMA buffers. */
static uint8_t spis_rx[C2C_FRAME_LEN];
static uint8_t spis_tx[C2C_FRAME_LEN];

/* Shared with the SPIS ISR. */
static volatile uint8_t g_status = C2C_STATUS_IDLE;
static volatile counter_t g_latched;
static volatile bool g_new_counter = false;

static void spis_init(void) {
  /* Release the C2C pins from SPIM0 (configured by the SDK's nvm_init). */
  NRF_SPIM0->ENABLE = (SPIM_ENABLE_ENABLE_Disabled << SPIM_ENABLE_ENABLE_Pos);
  NRF_SPIM0->PSEL.SCK = 0xFFFFFFFF;
  NRF_SPIM0->PSEL.MOSI = 0xFFFFFFFF;
  NRF_SPIM0->PSEL.MISO = 0xFFFFFFFF;
  NVIC_DisableIRQ(SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn);

  /* Inputs driven by the MSP430 master; MISO is driven by the slave. */
  riotee_gpio_cfg_input(PIN_C2C_CLK, RIOTEE_GPIO_IN_NOPULL);
  riotee_gpio_cfg_input(PIN_C2C_MOSI, RIOTEE_GPIO_IN_NOPULL);
  riotee_gpio_cfg_input(PIN_C2C_MISO, RIOTEE_GPIO_IN_NOPULL);
  riotee_gpio_cfg_input(PIN_C2C_CS, RIOTEE_GPIO_IN_PULLUP);

  NRF_SPIS2->PSEL.SCK = PIN_C2C_CLK;
  NRF_SPIS2->PSEL.MOSI = PIN_C2C_MOSI;
  NRF_SPIS2->PSEL.MISO = PIN_C2C_MISO;
  NRF_SPIS2->PSEL.CSN = PIN_C2C_CS;

  /* Mode 0, MSB first - matches the MSP430 master. */
  NRF_SPIS2->CONFIG = (SPIS_CONFIG_CPHA_Leading << SPIS_CONFIG_CPHA_Pos) |
                      (SPIS_CONFIG_CPOL_ActiveHigh << SPIS_CONFIG_CPOL_Pos) |
                      (SPIS_CONFIG_ORDER_MsbFirst << SPIS_CONFIG_ORDER_Pos);

  NRF_SPIS2->DEF = C2C_STATUS_IDLE; /* clocked out if master over-reads */
  NRF_SPIS2->ORC = 0x00;

  spis_tx[0] = g_status;
  NRF_SPIS2->RXD.PTR = (uint32_t)spis_rx;
  NRF_SPIS2->RXD.MAXCNT = C2C_FRAME_LEN;
  NRF_SPIS2->TXD.PTR = (uint32_t)spis_tx;
  NRF_SPIS2->TXD.MAXCNT = C2C_FRAME_LEN;

  /* Automatically re-acquire the semaphore after each transaction. */
  NRF_SPIS2->SHORTS = SPIS_SHORTS_END_ACQUIRE_Msk;
  NRF_SPIS2->EVENTS_END = 0;
  NRF_SPIS2->EVENTS_ACQUIRED = 0;
  NRF_SPIS2->INTENSET = SPIS_INTENSET_END_Msk | SPIS_INTENSET_ACQUIRED_Msk;
  NRF_SPIS2->ENABLE = (SPIS_ENABLE_ENABLE_Enabled << SPIS_ENABLE_ENABLE_Pos);

  /* ISR only touches plain variables, so its priority is unconstrained by
   * FreeRTOS. */
  NVIC_SetPriority(SPIM2_SPIS2_SPI2_IRQn, 6);
  NVIC_EnableIRQ(SPIM2_SPIS2_SPI2_IRQn);

  /* Acquire the semaphore once to arm the first transfer (ACQUIRED ISR releases
   * it). */
  NRF_SPIS2->TASKS_ACQUIRE = 1;
}

void SPIM2_SPIS2_SPI2_IRQHandler(void) {
  if (NRF_SPIS2->EVENTS_END) {
    NRF_SPIS2->EVENTS_END = 0;
    if (spis_rx[0] == C2C_CMD_SET_COUNTER) {
      g_latched = (counter_t)spis_rx[1] | ((counter_t)spis_rx[2] << 8) |
                  ((counter_t)spis_rx[3] << 16) | ((counter_t)spis_rx[4] << 24);
      g_status = C2C_STATUS_BUSY;
      g_new_counter = true;
    }
  }
  if (NRF_SPIS2->EVENTS_ACQUIRED) {
    NRF_SPIS2->EVENTS_ACQUIRED = 0;
    /* Re-arm the buffers with the current status for the next transaction. */
    spis_tx[0] = g_status;
    NRF_SPIS2->DEF = g_status;
    NRF_SPIS2->RXD.PTR = (uint32_t)spis_rx;
    NRF_SPIS2->RXD.MAXCNT = C2C_FRAME_LEN;
    NRF_SPIS2->TXD.PTR = (uint32_t)spis_tx;
    NRF_SPIS2->TXD.MAXCNT = C2C_FRAME_LEN;
    NRF_SPIS2->TASKS_RELEASE = 1;
  }
}

void lateinit(void) {
  riotee_ble_init();
  spis_init();
  /* Deliberately do NOT configure PIN_LED_CTRL - the MSP430 owns the LED. */
}

int main(void) {
  riotee_ble_adv_cfg_t adv_cfg = {.addr = adv_address,
                                  .name = adv_name,
                                  .name_len = 6,
                                  .data = &adv_payload,
                                  .data_len = sizeof(adv_payload),
                                  .manufacturer_id = RIOTEE_BLE_ADV_MNF_NORDIC};
  riotee_ble_adv_cfg(&adv_cfg);

  for (;;) {
    /* Sleep until the master pushes a new counter (status already set BUSY). */
    while (!g_new_counter) enter_low_power();
    g_new_counter = false;

    adv_payload = g_latched;

    /* Advertise for ~1 s: 4 rounds on all channels, 250 ms apart. */
    for (int i = 0; i < 4; i++) {
      riotee_ble_advertise(ADV_CH_ALL);
      riotee_sleep_ms(250);
    }

    g_status = C2C_STATUS_DONE;
    spis_tx[0] = C2C_STATUS_DONE; /* expose DONE on the next read without
                                     waiting for re-arm */
  }
}
