# Riotee SDK project description for the nRF52833 Bonito application.
# Invoked by ./Makefile, which passes OPT, USER_DEFINES and GNU_INSTALL_ROOT.
RIOTEE_SDK_ROOT ?= Riotee_SDK

PRJ_ROOT   := .
OUTPUT_DIR := _build

# Select the charging-time source and rendezvous implementation:
#   real (default) — measure real capacitor charging time via the AM1805 RTC.
#   sim            — replay the deterministic trace from charge_trace.h.
#                    -DDISABLE_CAP_MONITOR is added automatically in sim mode
#                    (required for constant USB/bench power; without it startup
#                    blocks waiting for a capacitor voltage that never rises).
BONITO_SOURCE ?= real

ifeq ($(BONITO_SOURCE),sim)
  CHARGE_SOURCE_FILE := $(PRJ_ROOT)/src/charge_source_sim.c
  RENDEZVOUS_FILE    := $(PRJ_ROOT)/src/rendezvous_sim.c
  USER_DEFINES       += -DBONITO_SIM -DDISABLE_CAP_MONITOR
else ifeq ($(BONITO_SOURCE),real)
  CHARGE_SOURCE_FILE := $(PRJ_ROOT)/src/charge_source_real.c
  RENDEZVOUS_FILE    := $(PRJ_ROOT)/src/rendezvous_real.c
  USER_DEFINES       += -DBONITO_REAL
else
  $(error BONITO_SOURCE must be 'sim' or 'real', got '$(BONITO_SOURCE)')
endif

SRC_FILES = \
  $(PRJ_ROOT)/src/main.c                  \
  $(PRJ_ROOT)/src/bonito/bonito_dist.c    \
  $(PRJ_ROOT)/src/bonito/bonito.c         \
  $(CHARGE_SOURCE_FILE)                   \
  $(RENDEZVOUS_FILE)

# Include the project src dir (for local headers), bonito subdir, and the repo
# root (for protocol.h).
INC_DIRS = \
  $(PRJ_ROOT)/src           \
  $(PRJ_ROOT)/src/bonito    \
  $(PRJ_ROOT)/..

include $(RIOTEE_SDK_ROOT)/Makefile
