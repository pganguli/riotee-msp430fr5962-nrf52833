# MSP430FR5962 + nRF52833 (Riotee board)

This repo contains firmware for both MCUs on the Riotee board.

| MCU          | Directory                        | README                                           |
|--------------|----------------------------------|--------------------------------------------------|
| MSP430FR5962 | [`msp430fr5962/`](msp430fr5962/) | [msp430fr5962/README.md](msp430fr5962/README.md) |
| nRF52833     | [`nrf52833/`](nrf52833/)         | [nrf52833/README.md](nrf52833/README.md)         |

## What it does

The MSP430 keeps a counter in non-volatile FRAM that survives power cycles. Each
iteration it increments the counter and sends it to the nRF52 over the inter-chip
(C2C) SPI link; the nRF52 advertises the value over BLE for ~1 s and signals
completion; only then does the MSP430 commit the new value to FRAM. The MSP430 is
the SPI master and the nRF52 is the SPI slave. The wire format shared by both is
in [protocol.h](protocol.h).

## Build

A top-level `Makefile` orchestrates both sub-builds:

```bash
make release        # build both firmwares, highest optimization
make debug          # build both firmwares, no optimization
make clean          # clean both
make flash          # flash both: nRF52 first, then MSP430 (powers target on)
make flash-msp430   # flash only the MSP430
make flash-nrf52    # flash only the nRF52
```

Pass the per-board toolchain locations through, e.g.
`make CCS_ROOT=~/ti/ccs2051/ccs GNU_INSTALL_ROOT=/opt/gcc-arm-none-eabi/bin/ release`.
See each board's README for details.

## LED sharing quirk

The LED (**PJ.0** on MSP430, **P0.03** on nRF52) is shared between both MCUs.
If the nRF52 configures that pin as output and drives it high, it wins the bus
and the MSP430 cannot blink the LED regardless of what it does.

When running MSP430 LED code, flash the nRF52 with idle firmware — see
[nrf52833/README.md](nrf52833/README.md#idle-firmware) for details.
