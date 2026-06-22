# MSP430FR5962 firmware build (TI MSP430 code generation tools).
#
# Override CCS_ROOT to match your Code Composer Studio installation, e.g.:
#   make CCS_ROOT=~/ti/ccs2051/ccs release
#
# Targets:
#   debug    build with no optimization + symbolic debug
#   release  build with highest optimization (default)
#   clean    remove the Debug/ output directory
#   flash    program the device over SBW via riotee-probe

# Auto-detect the newest CCS install under ~/ti/ then the newest MSP430 compiler
# within it. Both can be overridden on the command line or via environment:
#   make CCS_ROOT=~/ti/ccs1200/ccs release
CCS_ROOT  ?= $(lastword $(sort $(wildcard $(HOME)/ti/ccs*/ccs)))
COMPILER  ?= $(lastword $(sort $(wildcard $(CCS_ROOT)/tools/compiler/ti-cgt-msp430_*)))

ifeq ($(CCS_ROOT),)
  $(error CCS_ROOT not found under $(HOME)/ti/ccs*/ccs. Set CCS_ROOT explicitly: make CCS_ROOT=<path> ...)
endif
ifeq ($(COMPILER),)
  $(error No ti-cgt-msp430_* compiler found under $(CCS_ROOT)/tools/compiler. Set COMPILER explicitly.)
endif

CL   := $(COMPILER)/bin/cl430
LNK  := $(COMPILER)/bin/lnk430
HEX  := $(COMPILER)/bin/hex430
LIB  := $(COMPILER)/lib
INC  := $(CCS_ROOT)/ccs_base/msp430/include
CINC := $(COMPILER)/include

PRJ    := $(CURDIR)
ROOT   := $(abspath $(CURDIR)/..)
OUT    := $(PRJ)/Debug
TARGET := msp430fr5962

# Optimization level; overridden by the debug/release targets below.
OPT ?= --opt_level=2

CFLAGS = -vmsp --abi=eabi $(OPT) \
  --include_path=$(INC) --include_path=$(CINC) --include_path=$(ROOT) \
  --define=__MSP430FR5962__ \
  --silicon_errata=CPU21 --silicon_errata=CPU22 --silicon_errata=CPU40 \
  --advice:power=none --gen_func_subsections=on

.PHONY: all debug release build clean flash

all: release

debug:   OPT := --opt_level=off --symdebug:dwarf
debug:   build

release: OPT := --opt_level=4
release: build

build: $(OUT)/$(TARGET).hex

$(OUT)/$(TARGET).hex: $(PRJ)/main.c $(PRJ)/lnk_$(TARGET).cmd $(ROOT)/protocol.h | $(OUT)
	$(CL) $(CFLAGS) --obj_directory=$(OUT) $(PRJ)/main.c
	$(LNK) --abi=eabi --heap_size=160 --stack_size=160 \
	  -i$(LIB) -i$(INC) \
	  -o $(OUT)/$(TARGET).out -m $(OUT)/$(TARGET).map \
	  $(OUT)/main.obj $(PRJ)/lnk_$(TARGET).cmd \
	  $(INC)/$(TARGET).cmd -lrts430_eabi.lib --rom_model
	# --romwidth=8 --memwidth=8 --fill=0xFF are mandatory: without them hex430
	# emits only the low byte of each interrupt vector and corrupts RESET.
	$(HEX) --intel --romwidth=8 --memwidth=8 --fill=0xFF \
	  -o $(OUT)/$(TARGET).hex $(OUT)/$(TARGET).out

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)

# SBW programming needs bypass --on (not target-power --on); power on afterwards.
flash: $(OUT)/$(TARGET).hex
	riotee-probe bypass --on
	sleep 1
	riotee-probe program -d msp430 -f $(OUT)/$(TARGET).hex
	riotee-probe target-power --on
