# MSP430FR5962 + nRF52833 (Riotee board)

The MSP430 keeps a counter in non-volatile FRAM. Each iteration it increments
the counter, sends it to the nRF52 over the C2C SPI link, and waits. The nRF52
advertises the value over BLE for ~1 s and signals completion. Only then does
the MSP430 commit the new value to FRAM and move on.

The MSP430 is the SPI master; the nRF52 is the slave. The wire format is in
[protocol.h](protocol.h).

## Build and flash

```bash
make release        # build both firmwares
make flash          # flash both (nRF52 first, then MSP430)
make flash-msp430   # flash MSP430 only
make flash-nrf52    # flash nRF52 only
make debug          # build with no optimisation
make clean
```

Toolchain paths are auto-detected. Override if needed:

```bash
make CCS_ROOT=~/ti/ccs2051/ccs GNU_INSTALL_ROOT=/opt/gcc-arm-none-eabi/bin/ release
```

See [msp430fr5962/README.md](msp430fr5962/README.md) and
[nrf52833/README.md](nrf52833/README.md) for per-chip details.

## LED feedback

The LED (PJ.0) is driven by the MSP430 only.

| Pattern | Meaning |
|---|---|
| 30 rapid blinks on boot | First boot after programming — counter reset to 0 |
| 3 rapid blinks on boot | Counter restored from FRAM after a power cycle |
| Steady on | Waiting for the nRF52 to finish advertising |
| Brief off, then on | 3 s timeout — retrying the same counter value |
| Brief off between steady-on periods | Counter committed, moving to next value |

## Scanning

`scan.py` listens for Riotee advertisements and prints the counter. It uses
BlueZ's `SetDiscoveryFilter` with `DuplicateData=True` and sets a 10 ms HCI
scan interval for continuous coverage — requires `sudo` for the raw HCI socket.

```bash
sudo ./scan.py
```

Output:

```text
counter=42  rssi=-54 dBm
counter=43  rssi=-55 dBm
  !! missed 1 value(s): 44–44
counter=45  rssi=-53 dBm
```
