# MSP430FR5962

Keeps a power-cycle-persistent counter in internal FRAM and sends each new value
to the nRF52 over the C2C SPI link for BLE advertising via the Bonito protocol.

The counter is the default test payload. In the deployed DNN repo, replace the
counter loop with inference results — the C2C contract is identical.

## State

| Variable | Storage | Survives power cut? | Survives reflash? |
|---|---|---|---|
| `g_counter` | FRAM (`#pragma PERSISTENT`) | **Yes** | **No** — reset to 0 by linker |
| `g_initialized` | FRAM (`#pragma PERSISTENT`) | **Yes** | **No** — triggers 30-blink first-boot |
| `working` (in-flight value) | RAM | **No** | N/A |

On boot: `working = g_counter`, then `working++` before the first send. If power
is cut after `ACCEPTED` but before `working++`, `g_counter` already holds the
committed value and the next boot sends `g_counter + 1`. If cut before `ACCEPTED`,
the same value is retried. The counter never goes backward.

## C2C handoff

`c2c_send_payload(buf, len)` frames the payload as `[CMD=0x01][len][bytes…]` and
sends it to the nRF52 SPIS ISR, which latches it immediately (latest-wins) and
replies `ACCEPTED` on the next poll. The nRF52 Bonito loop then broadcasts it
asynchronously at the next connection interval — the MSP430 never waits for the
radio.

If no `ACCEPTED` arrives within 30 polls (~3 s), the MSP430 blinks 3 times and
retries the same value. This normally means the nRF52 is sleeping through a
connection interval; it clears on the next nRF52 wakeup.

See [main.c](main.c) and the shared [protocol.h](../protocol.h).

## Build and flash

```bash
make release   # highest optimisation
make debug     # no optimisation, debug symbols
make clean
make flash     # SBW program via riotee-probe, then power on
```

`CCS_ROOT` defaults to the newest `~/ti/ccs*/ccs` install found automatically.
Override if needed:

```bash
make CCS_ROOT=~/ti/ccs2051/ccs release
```

## Resetting the counter

Reflash — `#pragma PERSISTENT` variables are written to FRAM at programming time,
so every flash resets the counter to 0. You will see 30 rapid blinks on the next
boot confirming the reset.

## Flash quirks

- Use `bypass --on` for SBW programming, **not** `target-power --on`. With only
  `target-power`, the SBW connection fails (error 255).
- Do not enable `bypass` and `target-power` simultaneously — causes error 255.
  The Makefile handles the correct sequence automatically.
- If a flash fails mid-way, the probe gets stuck. Unplug and replug the USB
  cable, then retry.
