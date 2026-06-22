# nRF52833 firmware build (Riotee SDK wrapper).
#
# Set GNU_INSTALL_ROOT to your arm-none-eabi-gcc bin/ if it is not on PATH:
#   make GNU_INSTALL_ROOT=/opt/gcc-arm-none-eabi/bin/ release
#
# DISABLE_CAP_MONITOR is always defined: under constant USB power (no harvester)
# the startup would otherwise block forever waiting for the capacitor.
#
# Targets:
#   debug    build with no optimization
#   release  build with high optimization (default)
#   clean    remove the _build/ output directory
#   flash    program the nRF52 via riotee-probe

RIOTEE_SDK_ROOT ?= Riotee_SDK
GNU_INSTALL_ROOT ?=

SDK_MAKE = $(MAKE) -f build.mk \
  RIOTEE_SDK_ROOT=$(RIOTEE_SDK_ROOT) \
  GNU_INSTALL_ROOT=$(GNU_INSTALL_ROOT) \
  USER_DEFINES=-DDISABLE_CAP_MONITOR

.PHONY: all debug release clean flash

all: release

debug:
	$(SDK_MAKE) OPT="-O0 -g3"

release:
	$(SDK_MAKE) OPT="-O3 -g3"

clean:
	$(SDK_MAKE) clean

flash:
	$(SDK_MAKE) flash
