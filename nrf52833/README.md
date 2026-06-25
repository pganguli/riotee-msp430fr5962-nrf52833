# nRF52833 — Bonito BLE radio

Runs the one-way Bonito connection protocol (NSDI 2022) as a BLE radio for the
MSP430. The MSP430 pushes an opaque payload over the C2C SPI link; this firmware
latches it, computes the Bonito connection interval from an online
Normal-distribution model of its (simulated) charging time, advertises the
payload with the CI and a round sequence number, updates the model, then sleeps
for the CI.

The Bonito engine is in [src/bonito/](src/bonito/). The charging-time source and
rendezvous wait are behind clean abstractions ([src/charge_source.h](src/charge_source.h),
[src/rendezvous.h](src/rendezvous.h)) so swapping in real energy harvesting is a
file swap. See [src/main.c](src/main.c) and the shared [protocol.h](../protocol.h).

## Build and flash

```bash
make release              # sim mode, highest optimisation
make debug                # sim mode, no optimisation, debug symbols
make BONITO_SOURCE=real release   # real harvesting (stubs — implement first)
make clean
make flash                # program via riotee-probe
```

`arm-none-eabi-gcc` is picked up from `PATH` automatically. Override with
`make GNU_INSTALL_ROOT=/path/to/gcc-arm-none-eabi/bin/ release`.

`BONITO_SOURCE=sim` (default) compiles `charge_source_sim.c` /
`rendezvous_sim.c` and sets `-DDISABLE_CAP_MONITOR`. The `DISABLE_CAP_MONITOR`
flag is required when running on USB power — without it startup blocks forever
waiting for a capacitor that isn't there.

## Idle firmware

If you need the MSP430 to control the LED while the nRF52 is running a
different image, flash this first so the nRF52 never drives the shared LED pin:

```c
#include "riotee.h"
void lateinit(void) {}
int main(void) { for (;;) {} }
```

Build with `-DDISABLE_CAP_MONITOR` so the power management IC initialises
correctly, otherwise the MSP430 may not get stable power.
