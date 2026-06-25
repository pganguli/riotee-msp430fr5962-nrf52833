# MSP430FR5962 + nRF52833 (Riotee board) — Bonito BLE protocol

The MSP430 keeps a counter in non-volatile FRAM. On each iteration it increments
the counter and hands it to the nRF52 over the C2C SPI link. The nRF52 runs the
**Bonito** connection protocol (NSDI 2022): it models its energy-harvesting
charging times online (via SGD on a Normal distribution) and uses the model to
compute a *connection interval* — how long to wait before the next BLE
transmission. Each advertisement carries the sequence number, the computed CI,
and the application payload.

The MSP430 is the SPI master; the nRF52 is the slave. The wire format is in
[protocol.h](protocol.h). The Bonito math lives entirely on the nRF52;
the MSP430 only ships an opaque blob via `c2c_send_payload(buf, len)`.

## Build and flash

```bash
make release        # build both firmwares (sim mode, default)
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

The LED (PJ.0) is driven by the MSP430 only — the nRF52 never touches it.

| Pattern | Meaning |
|---|---|
| 30 rapid blinks on boot | First boot after programming — counter reset to 0 |
| 3 rapid blinks on boot | Counter restored from FRAM after a power cycle |
| Steady on | Sending payload; waiting for nRF52 ACCEPTED |
| Brief off between steady-on periods | Payload accepted, moving to next value |
| 3 rapid blinks mid-operation | 3 s timeout — nRF52 not responding, retrying |

The nRF52 Bonito loop runs asynchronously. The LED off/on cadence reflects the
MSP430's ~100–200 ms handoff loop, not the Bonito connection interval.

## Scanning

`scan.py` listens for Riotee Bonito advertisements (filtered by Nordic
manufacturer ID), decodes each round's sequence number, connection interval, and
application payload, and verifies the CI against a reference Bonito model running
on the laptop.

```bash
sudo ./scan.py
```

Output (sim mode — CI varies as the model warms up then converges):

```text
seq=    0  ci=  2320 ms [ok]  counter=1  rssi=-54 dBm
seq=    1  ci=  2314 ms [ok]  counter=2  rssi=-55 dBm
  !! missed 1 round(s): seq 3..3
seq=    4  ci=  2280 ms [ok]  counter=5  rssi=-53 dBm
```

`[ok]` means the node's advertised CI matches the laptop's reference Bonito
model. `[MISMATCH]` would indicate a bug in the C port of the Bonito math.

## DNN integration

The C2C contract (`protocol.h`) is generic. In your DNN repo, replace the
counter loop with:

```c
c2c_send_payload((uint8_t *)&inference_result, sizeof(inference_result));
```

The nRF52 and `scan.py` are agnostic to the payload content (up to 16 bytes).
The laptop monitor prints raw hex for payloads that aren't 4 bytes.
