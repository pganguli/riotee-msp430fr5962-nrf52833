# MSP430FR5962 + nRF52833 (Riotee board) — Bonito BLE protocol

The MSP430 keeps a counter in non-volatile FRAM. On each iteration it increments
the counter and hands it to the nRF52 over the C2C SPI link. The nRF52 runs the
**Bonito** connection protocol (NSDI 2022): it models its energy-harvesting
charging times online (SGD on a Normal distribution) and uses the model to
compute a *connection interval* (CI) — how long to wait before the next BLE
transmission. Each advertisement carries the round sequence number, the raw model
parameters (`mean`, `var`), and the application payload. The receiver recomputes
the CI from the model parameters locally (per the paper's Fig. 12 design).

The MSP430 is the SPI master; the nRF52 is the slave. The wire format is in
[protocol.h](protocol.h). The Bonito math lives entirely on the nRF52;
the MSP430 only ships an opaque blob via `c2c_send_payload(buf, len)`.

See [ARCHITECTURE.md](ARCHITECTURE.md) for a full description of the protocol,
power scenarios, state inventory, and known deviations from the paper.

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

## Sim vs. real mode

`BONITO_SOURCE=real` (default) uses the AM1805 RTC to measure actual elapsed
time between wakeups and arms a scheduled power-down alarm before each sleep.

`BONITO_SOURCE=sim` replays a deterministic synthetic charging-time trace —
useful on a bench supply where the board never actually powers down.
`-DDISABLE_CAP_MONITOR` is set automatically in sim mode (required for bench
power; without it startup blocks waiting for a capacitor that never charges).

```bash
make release                    # real mode (default)
make flash                      # real mode (default)
make BONITO_SOURCE=sim release  # sim mode — no extra flags needed
sudo ./scan.py                  # real mode (default); add --sim for sim mode
```

## LED feedback

The LED (PJ.0) is driven by the MSP430 only — the nRF52 never touches it.

| Pattern | Meaning |
|---|---|
| 30 rapid blinks on boot | First boot after programming — counter reset to 0 |
| 3 rapid blinks on boot | Counter restored from FRAM after a power cycle |
| Steady on | Sending payload; waiting for nRF52 ACCEPTED |
| Brief off between steady-on periods | Payload accepted, moving to next value |
| 3 rapid blinks mid-operation | 3 s timeout — nRF52 not responding, retrying |

The nRF52 Bonito loop runs asynchronously. The LED cadence reflects the MSP430's
~100–200 ms handoff loop, not the Bonito connection interval.

## Scanning

`scan.py` listens for Riotee Bonito advertisements (filtered by Nordic
manufacturer ID `0x0059`), decodes the model parameters from each round,
recomputes the CI locally, and prints the sequence number, derived CI, and
application payload. In sim mode it also verifies the derived CI against a
reference Bonito model running the same synthetic trace on the laptop.

```bash
sudo ./scan.py          # real mode (default)
sudo ./scan.py --sim    # sim mode — enables CI verification against trace
```

Example output (real mode):

```text
seq=    0  ci=  2163 ms ↑ [real]  loss= 0.0%  counter=1  rssi=-54 dBm
       session #1 round    1  next TX ~14:22:03 (+2163 ms)  node µ=1.000s σ=0.500s  laptop µ=0.995s σ=0.498s  joint CI=2163 ms
seq=    1  ci=  2152 ms ↓ [real]  loss= 0.0%  counter=2  rssi=-55 dBm
  !! missed 1 round(s): seq 3..3
seq=    4  ci=  2090 ms ↓ [real]  loss= 0.0%  counter=5  rssi=-53 dBm
```

- `ci` — CI derived from received `mean`/`var` (matches what the node computed).
- `node µ/σ` — charging-time model parameters received directly from the node.
- `joint CI` — CI from the joint CDF of laptop + node models (converges to node
  CI because the laptop is always-on).
- `[real]` / `[ok]` / `[MISMATCH]` — real mode skips trace verification; sim mode
  checks the derived CI against the reference model.

## DNN integration

The C2C contract (`protocol.h`) is generic. In your DNN repo, replace the
counter loop with:

```c
c2c_send_payload((uint8_t *)&inference_result, sizeof(inference_result));
```

The nRF52 and `scan.py` are agnostic to the payload content (up to 4 bytes in the
current BLE advertisement budget; `scan.py` prints raw hex for non-4-byte payloads).
