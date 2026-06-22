# MSP430FR5962

Application firmware: keeps a power-cycle-persistent counter in FRAM and hands
each value to the nRF52 over the C2C SPI link for BLE advertising (see
[main.c](main.c) and the shared [protocol.h](../protocol.h)).

## Build (Makefile)

```bash
make CCS_ROOT=<path to CCS install> release   # highest optimization
make CCS_ROOT=<path to CCS install> debug     # no optimization + symbols
make clean
make flash                                    # SBW program via riotee-probe
```

`CCS_ROOT` defaults to `~/ti/ccs2051/ccs`. The sections below document the
underlying `cl430`/`lnk430`/`hex430` invocations the Makefile wraps.

## Toolchain variables

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
PRJ=$(pwd)   # run from this directory
```

## Compile

```bash
$CL -vmsp --abi=eabi -O2 \
  --include_path=$INC --include_path=$CINC \
  --define=__MSP430FR5962__ \
  --silicon_errata=CPU21 --silicon_errata=CPU22 --silicon_errata=CPU40 \
  --advice:power=none --gen_func_subsections=on \
  --obj_directory=$PRJ/Debug $PRJ/main.c
```

## Link

```bash
$LNK --abi=eabi --heap_size=160 --stack_size=160 \
  -i$LIB -i$INC \
  -o $PRJ/Debug/msp430fr5962.out \
  -m $PRJ/Debug/msp430fr5962.map \
  $PRJ/Debug/main.obj $PRJ/lnk_msp430fr5962.cmd \
  $INC/msp430fr5962.cmd -lrts430_eabi.lib --rom_model
```

## Generate Intel hex

**Must** use `--romwidth=8 --memwidth=8 --fill=0xFF`. Without these flags,
`hex430` outputs only the low byte of each 2-byte interrupt vector, leaving
high bytes as 0xFF. This corrupts the RESET vector (e.g. 0xFF36 instead of
0x4036) and the MSP430 crashes immediately on startup.

```bash
$HEX --intel --romwidth=8 --memwidth=8 --fill=0xFF \
  -o $PRJ/Debug/msp430fr5962.hex $PRJ/Debug/msp430fr5962.out
```

## Flash

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

## Quirks

- **USB replug required after any SBW failure.** If `program -d msp430` fails
  mid-way (error 255 or verification failure), the probe firmware gets stuck.
  The only recovery is to unplug and replug the USB cable, then redo
  `bypass --on` before the next flash attempt.
- `bypass --on` and `target-power --on` conflict: enabling both simultaneously
  causes SBW error 255. Turn target-power off before using bypass for
  programming, then turn target-power on afterwards to run code.
