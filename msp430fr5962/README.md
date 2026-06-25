# MSP430FR5962

Keeps a power-cycle-persistent counter in internal FRAM and sends each value to
the nRF52 over the C2C SPI link for BLE advertising via the Bonito protocol.

The C2C handoff is fire-and-forget: `c2c_send_payload(buf, len)` sends an opaque
blob and waits only for the nRF52 SPIS ISR to latch it (~100 ms). The nRF52
Bonito loop then broadcasts it asynchronously at the Bonito connection interval.

This decoupling is intentional: the MSP430 (or a DNN inference loop in another
repo) can produce payloads at its own cadence without blocking on the radio.

See [main.c](main.c) and the shared [protocol.h](../protocol.h).

## Build and flash

```bash
make release   # highest optimisation
make debug     # no optimisation, debug symbols
make clean
make flash     # SBW program via riotee-probe, then power on
```

`CCS_ROOT` defaults to the newest `~/ti/ccs*/ccs` install found automatically.
Override if needed: `make CCS_ROOT=~/ti/ccs2051/ccs release`.

## Resetting the counter

Reflash — `#pragma PERSISTENT` variables are written to FRAM at programming
time, so every flash resets the counter to 0. You'll see 30 rapid blinks on the
next boot confirming the reset.

## Flash quirks

- Use `bypass --on` for SBW programming, **not** `target-power --on`. With only
  `target-power`, the SBW connection fails (error 255).
- Don't enable `bypass` and `target-power` simultaneously — causes error 255.
  The Makefile handles the correct sequence automatically.
- If a flash fails mid-way, the probe gets stuck. Unplug and replug the USB
  cable, then retry.
