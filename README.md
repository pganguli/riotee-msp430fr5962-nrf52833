# MSP430FR5962 + nRF52833 (Riotee board)

This repo contains firmware for both MCUs on the Riotee board.

| MCU          | Directory                        | README                                           |
|--------------|----------------------------------|--------------------------------------------------|
| MSP430FR5962 | [`msp430fr5962/`](msp430fr5962/) | [msp430fr5962/README.md](msp430fr5962/README.md) |
| nRF52833     | [`nrf52833/`](nrf52833/)         | [nrf52833/README.md](nrf52833/README.md)         |

## LED sharing quirk

The LED (**PJ.0** on MSP430, **P0.03** on nRF52) is shared between both MCUs.
If the nRF52 configures that pin as output and drives it high, it wins the bus
and the MSP430 cannot blink the LED regardless of what it does.

When running MSP430 LED code, flash the nRF52 with idle firmware — see
[nrf52833/README.md](nrf52833/README.md#idle-firmware) for details.
