# nRF52833

Acts as a commandable BLE radio for the MSP430. Receives counter values over
the C2C SPI link, advertises each one over BLE for ~1 s, then signals
completion back to the MSP430. Never touches the shared LED pin. See
[src/main.c](src/main.c) and the shared [protocol.h](../protocol.h).

## Build and flash

```bash
make release   # highest optimisation
make debug     # no optimisation, debug symbols
make clean
make flash     # program via riotee-probe
```

`arm-none-eabi-gcc` is picked up from `PATH` automatically. Override with
`make GNU_INSTALL_ROOT=/path/to/gcc-arm-none-eabi/bin/ release`.

The build always defines `DISABLE_CAP_MONITOR`. Without it, startup blocks
forever waiting for a capacitor that isn't there when running on USB power.

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
