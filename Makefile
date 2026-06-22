# Top-level orchestrator for the Riotee board firmware (MSP430FR5962 + nRF52833).
#
# Targets:
#   debug         build both firmwares with no optimization
#   release       build both firmwares with high optimization (default)
#   clean         clean both firmwares
#   flash-msp430  flash only the MSP430
#   flash-nrf52   flash only the nRF52
#   flash         flash both: nRF52 first (it never drives the shared LED),
#                 then the MSP430
#
# Per-board toolchain variables pass straight through, e.g.:
#   make CCS_ROOT=~/ti/ccs2051/ccs GNU_INSTALL_ROOT=/opt/gcc-arm-none-eabi/bin/ release

MSP_DIR := msp430fr5962
NRF_DIR := nrf52833

.PHONY: all debug release clean flash flash-msp430 flash-nrf52

all: release

debug:
	$(MAKE) -C $(MSP_DIR) debug
	$(MAKE) -C $(NRF_DIR) debug

release:
	$(MAKE) -C $(MSP_DIR) release
	$(MAKE) -C $(NRF_DIR) release

clean:
	$(MAKE) -C $(MSP_DIR) clean
	$(MAKE) -C $(NRF_DIR) clean

flash-msp430:
	$(MAKE) -C $(MSP_DIR) flash

flash-nrf52:
	$(MAKE) -C $(NRF_DIR) flash

# Combined flash: program the nRF52 first so the shared LED stays free for the
# MSP430, which is flashed (and powered on) last.
flash: flash-nrf52 flash-msp430
