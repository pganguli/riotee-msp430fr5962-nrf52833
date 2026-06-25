# nRF52833 — Bonito BLE radio

Runs the Bonito connection protocol (NSDI 2022) as a BLE radio for the MSP430.
The MSP430 pushes an opaque payload over the C2C SPI link; this firmware latches
it, runs a Bonito round, then sleeps until the next connection interval.

Each Bonito round (order mirrors `protocols.py:20-28`):

1. **Observe** — get this round's charging time `c` (sim: synthetic trace; real:
   elapsed time since last wakeup via AM1805 RTC).
2. **Compute CI** — `bonito_connection_interval(&dist, NULL, 0.99)` = `dist.ppf(0.99)`
   for the always-on laptop peer (paper §7). With two intermittent nodes, pass the
   peer's received model instead and the bisection path activates.
3. **Advertise** — broadcast `{seq, model_type, app_len, mean, var, app[]}` × 3
   bursts (paper Fig. 12: model parameters, not the CI). The laptop recomputes CI
   from `mean`/`var` locally.
4. **Update** — `bonito_dist_sgd_update(&dist, c)`.
5. **Sleep** — `rendezvous_wait(ci)`.

The charging-time source and rendezvous wait are behind clean abstractions
([src/charge_source.h](src/charge_source.h), [src/rendezvous.h](src/rendezvous.h))
so swapping in real energy harvesting is a file swap with no changes to the engine.
See [src/main.c](src/main.c), [src/bonito/](src/bonito/), and
[../ARCHITECTURE.md](../ARCHITECTURE.md).

## Build and flash

```bash
make release                      # real mode, highest optimisation (default)
make debug                        # real mode, no optimisation, debug symbols
make BONITO_SOURCE=sim release    # sim mode (synthetic trace, bench power)
make clean
make flash                        # chip-erase then program via riotee-probe
```

`arm-none-eabi-gcc` is picked up from `PATH` automatically. Override with:

```bash
make GNU_INSTALL_ROOT=/path/to/gcc-arm-none-eabi/bin/ release
```

`BONITO_SOURCE=real` (default) compiles `charge_source_real.c` /
`rendezvous_real.c` and sets `-DBONITO_REAL`. The AM1805 RTC is used to arm a
scheduled wake alarm before each power-down.

`BONITO_SOURCE=sim` compiles `charge_source_sim.c` / `rendezvous_sim.c` and
automatically sets both `-DBONITO_SIM` and `-DDISABLE_CAP_MONITOR`. The
`DISABLE_CAP_MONITOR` flag is required when running on USB/bench power; without
it startup blocks forever waiting for a capacitor voltage that never rises.
You do not need to pass it manually.

## BLE advertisement payload

16 bytes, Nordic manufacturer ID `0x0059` (mirrored in `scan.py`):

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 2 B | `seq` | Round index; resets to 0 on every nRF52 reboot. |
| 2 | 1 B | `model_type` | Distribution type (`0x01` = Normal). |
| 3 | 1 B | `app_len` | Valid bytes in `app[]` (0–4). |
| 4 | 4 B | `mean` | Charging-time distribution mean (seconds, float32). |
| 8 | 4 B | `var` | Charging-time distribution variance (seconds², float32). |
| 12 | 4 B | `app[]` | Opaque app payload (4-byte counter today). |

The CI is **not** transmitted. Each peer recomputes it from `mean`/`var`.

## Known limitations

- `riotee_checkpoint()` is a no-op: the Riotee SDK's FRAM uses the same SPI
  pins as the C2C link (SPIS2). Every nRF52 boot starts fresh with prior
  `mean=1.0s, var=0.25s²`; the model re-converges in ~100 rounds (~2 min).
- `riotee_am1805_disable_power()` does not cut power on a bench supply. The
  board falls through to `riotee_sleep_ms()` and the AM1805 alarm is unused.
  Both behave correctly with a real harvester.
- **200 ms post-advertising sleep (hack):** `main.c` sleeps 200 ms after
  advertising and before `rendezvous_wait()` to guarantee the MSP430 can poll
  `ACCEPTED` and commit `g_counter` to FRAM before `disable_power` fires. This
  is a fixed-delay workaround; the correct fix is an explicit C2C handshake
  (`C2C_CMD_COMMIT_DONE`) so the nRF52 waits for the MSP430's signal rather
  than a deadline. Side effect: the advertised counter jumps by 2–3 per power
  cycle because the MSP430 commits additional values during the sleep window
  (latest-wins means they are not re-advertised in the current round).

See [../ARCHITECTURE.md](../ARCHITECTURE.md) for full details on these and all
other deviations from the paper.

## Idle firmware

If you need the MSP430 to control the LED while the nRF52 is running a different
image, flash a minimal idle firmware so the nRF52 never drives the shared LED pin:

```c
#include "riotee.h"
void lateinit(void) {}
int main(void) { for (;;) {} }
```

Build with `-DDISABLE_CAP_MONITOR` so the power management IC initialises
correctly, otherwise the MSP430 may not get stable power.
