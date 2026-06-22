# Riotee SDK project description for the nRF52833 application.
# Invoked by ./Makefile, which passes OPT, USER_DEFINES and GNU_INSTALL_ROOT.
RIOTEE_SDK_ROOT ?= Riotee_SDK

PRJ_ROOT := .
OUTPUT_DIR := _build

SRC_FILES = \
  $(PRJ_ROOT)/src/main.c

# Include the project dir (for headers) and the repo root (for protocol.h).
INC_DIRS = \
  $(PRJ_ROOT) \
  $(PRJ_ROOT)/..

include $(RIOTEE_SDK_ROOT)/Makefile
