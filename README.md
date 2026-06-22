# MSP430FR5962 + nRF52833 (Riotee board)

The LED is on **PJ.0** (MSP430) / **P0.03** (nRF52), shared between both MCUs.

---

## MSP430FR5962

### Toolchain variables

Set these before running any of the commands below. Adjust paths to match your
TI Code Composer Studio installation.

```bash
CCS_ROOT=<path to CCS install>   # e.g. ~/ti/ccs2051/ccs
COMPILER=$CCS_ROOT/tools/compiler/ti-cgt-msp430_21.6.1.LTS

CL=$COMPILER/bin/cl430
LNK=$COMPILER/bin/lnk430
HEX=$COMPILER/bin/hex430
LIB=$COMPILER/lib
INC=$CCS_ROOT/ccs_base/msp430/include
CINC=$COMPILER/include
PRJ=$(pwd)   # run from the project root
```

### Compile

```bash
$CL -vmsp --abi=eabi -O2 \
  --include_path=$INC --include_path=$CINC \
  --define=__MSP430FR5962__ \
  --silicon_errata=CPU21 --silicon_errata=CPU22 --silicon_errata=CPU40 \
  --advice:power=none --gen_func_subsections=on \
  --obj_directory=$PRJ/Debug $PRJ/main.c
```

### Link

```bash
$LNK --abi=eabi --heap_size=160 --stack_size=160 \
  -i$LIB -i$INC \
  -o $PRJ/Debug/msp430fr5962.out \
  -m $PRJ/Debug/msp430fr5962.map \
  $PRJ/Debug/main.obj $PRJ/lnk_msp430fr5962.cmd \
  $INC/msp430fr5962.cmd -lrts430_eabi.lib --rom_model
```

### Generate Intel hex

**Must** use `--romwidth=8 --memwidth=8 --fill=0xFF`. Without these flags,
`hex430` outputs only the low byte of each 2-byte interrupt vector, leaving
high bytes as 0xFF. This corrupts the RESET vector (e.g. 0xFF36 instead of
0x4036) and the MSP430 crashes immediately on startup.

```bash
$HEX --intel --romwidth=8 --memwidth=8 --fill=0xFF \
  -o $PRJ/Debug/msp430fr5962.hex $PRJ/Debug/msp430fr5962.out
```

### Flash

SBW programming requires `bypass --on`, **not** `target-power --on`. With only
`target-power`, the SBW connection fails (error code 255).

```bash
riotee-probe bypass --on
sleep 1
riotee-probe program -d msp430 -f $PRJ/Debug/msp430fr5962.hex
```

Then enable power so the MSP430 actually runs:

```bash
riotee-probe target-power --on
```

### Quirks

- **USB replug required after any SBW failure.** If `program -d msp430` fails
  mid-way (error 255 or verification failure), the probe firmware gets stuck.
  The only recovery is to unplug and replug the USB cable, then redo
  `bypass --on` before the next flash attempt.
- `bypass --on` and `target-power --on` conflict: enabling both simultaneously
  causes SBW error 255. Turn target-power off before using bypass for
  programming, then turn target-power on afterwards to run code.

---

## nRF52833

### Toolchain variables

```bash
GNU_INSTALL_ROOT=<path to gcc-arm-none-eabi>/bin/
RIOTEE_SDK_ROOT=<path to Riotee_SDK>
```

### Build

Run from the example directory (e.g. `Riotee_SDK/examples/blinky`):

```bash
make USER_DEFINES=-DDISABLE_CAP_MONITOR
```

`DISABLE_CAP_MONITOR` is required whenever there is no energy harvester
attached. Without it, `riotee_wait_cap_charged()` blocks forever on stable
USB power, and the nRF52 startup hangs in `wait_for_high()` before the
FreeRTOS scheduler and application code ever run. This flag applies to all
builds meant to run under constant power.

### Flash

```bash
riotee-probe program -d nrf52 -f _build/build.hex
```

### Power on

```bash
riotee-probe target-power --on
```

---

## LED sharing quirk

The LED (PJ.0 on MSP430, P0.03 on nRF52) is shared between both MCUs. If the
nRF52 configures that pin as output and drives it high, it wins the bus and the
MSP430 cannot blink the LED regardless of what it does.

When running MSP430 LED code, flash the nRF52 with idle firmware that never
touches the LED pin:

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
