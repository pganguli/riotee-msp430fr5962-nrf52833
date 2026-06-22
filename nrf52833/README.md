# nRF52833

Application firmware: acts as a commandable BLE radio for the MSP430. It receives
counter values from the MSP430 over the C2C SPI link (SPI slave, `SPIS2`),
advertises each value over BLE for ~1 s, and reports completion back through a
status register (see [src/main.c](src/main.c) and the shared
[protocol.h](../protocol.h)). It never touches the shared LED.

## Build (Makefile)

```bash
make GNU_INSTALL_ROOT=<gcc-arm-none-eabi>/bin/ release   # high optimization
make GNU_INSTALL_ROOT=<gcc-arm-none-eabi>/bin/ debug     # no optimization
make clean
make flash                                               # program via riotee-probe
```

`GNU_INSTALL_ROOT` may be omitted if `arm-none-eabi-gcc` is on your `PATH`. The
Makefile always builds with `-DDISABLE_CAP_MONITOR` (see below) and builds the
app in [src/](src/) against the `Riotee_SDK` submodule. The sections below
document the underlying SDK build the Makefile wraps.

## Toolchain variables

```bash
GNU_INSTALL_ROOT=<path to gcc-arm-none-eabi>/bin/
RIOTEE_SDK_ROOT=<path to Riotee_SDK>
```

## Build

Run from the example directory (e.g. `Riotee_SDK/examples/blinky`):

```bash
make USER_DEFINES=-DDISABLE_CAP_MONITOR
```

`DISABLE_CAP_MONITOR` is required whenever there is no energy harvester
attached. Without it, `riotee_wait_cap_charged()` blocks forever on stable
USB power, and the nRF52 startup hangs in `wait_for_high()` before the
FreeRTOS scheduler and application code ever run. This flag applies to all
builds meant to run under constant power.

## Flash

```bash
riotee-probe program -d nrf52 -f _build/build.hex
```

## Power on

```bash
riotee-probe target-power --on
```

## Idle firmware

When running MSP430 LED code, flash the nRF52 with idle firmware that never
touches the LED pin (see [LED sharing quirk](../README.md#led-sharing-quirk)):

```c
#include "riotee.h"

void lateinit(void) {}
void suspend(void) {}

int main(void) {
  for (;;) {}
}
```

Build this idle firmware **with** `-DDISABLE_CAP_MONITOR` so the nRF52 startup
completes (initializing the max20361 power management) — otherwise the MSP430
may not get stable power.
