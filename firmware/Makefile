# Pedal firmware. Needs the Arm toolchain and the two Electro-Smith
# libraries in ../lib (see README → "Firmware setup").
#
# Optional flags:
#   SCREEN_PANEL=1  2 toggles (SW1, SW4), no LEDs, OLED in their place (implies OLED=1)
#   OLED=1        draw on a 128x64 SSD1306 on D11/D12 (implies BOOT_SRAM=1)
#   LOG=1         print CPU load over USB serial once a second
#   KNOB_CHECK=1  control-mapping diagnostic instead of audio
#   BOOT_SRAM=1   run via Electro-Smith's bootloader (~480 KB programs);
#                 install it once with `make program-boot`
#
# The flags of the last build are remembered in build/flags:
#   - Building with different flags wipes build/ first (no stale objects).
#   - `make program-dfu` with no flags flashes what was built last, at the
#     right address. libDaisy picks the address from the flags, so flashing a
#     bootloader build without them would overwrite the bootloader.

TARGET = daisy-bass

CPP_SOURCES = main.cpp

LIBDAISY_DIR = ../lib/libDaisy
DAISYSP_DIR = ../lib/DaisySP

# Lets main.cpp include "dsp/…" and "control/…" from the repo root.
C_INCLUDES += -I..

# --- Remember / check build flags ---------------------------------------------
BUILD_DIR ?= build
FLAG_NAMES = SCREEN_PANEL OLED LOG KNOB_CHECK BOOT_SRAM
FLAGS_FILE = $(BUILD_DIR)/flags
GIVEN_FLAGS := $(strip $(foreach f,$(FLAG_NAMES),$(if $($(f)),$(f)=$($(f)))))
SAVED_FLAGS := $(strip $(shell cat $(FLAGS_FILE) 2>/dev/null))
FLASHING := $(filter program-dfu program,$(MAKECMDGOALS))
BUILDING := $(if $(MAKECMDGOALS),$(filter-out program-dfu program program-boot clean,$(MAKECMDGOALS)),all)

$(if $(strip $(BUILD_DIR)),,$(error BUILD_DIR is empty))

ifneq ($(FLASHING),)
  ifeq ($(BUILDING),)
    ifeq ($(wildcard $(FLAGS_FILE)),)
      $(error Nothing built yet: run make (with your flags) first)
    endif
    ifeq ($(GIVEN_FLAGS),)
      # Flash what was built last, with its flags.
      $(foreach f,$(SAVED_FLAGS),$(eval $(f)))
      $(info Flashing the last build: $(if $(SAVED_FLAGS),$(SAVED_FLAGS),stock panel, no flags))
    else ifneq ($(GIVEN_FLAGS),$(SAVED_FLAGS))
      $(error The last build used [$(SAVED_FLAGS)], not [$(GIVEN_FLAGS)]. Build with these flags first, or run 'make program-dfu' alone to flash the last build)
    endif
  endif
endif

ifneq ($(BUILDING),)
  ifneq ($(GIVEN_FLAGS)|$(wildcard $(FLAGS_FILE)),$(SAVED_FLAGS)|$(FLAGS_FILE))
    $(info $(if $(wildcard $(FLAGS_FILE)),Build flags changed ([$(SAVED_FLAGS)] -> [$(GIVEN_FLAGS)]),No record of the last build's flags): building from scratch)
    $(shell rm -rf $(BUILD_DIR) && mkdir -p $(BUILD_DIR) && echo '$(GIVEN_FLAGS)' > $(FLAGS_FILE))
  endif
endif

# --- Flags → defines ------------------------------------------------------------
ifdef SCREEN_PANEL
C_DEFS += -DSCREEN_PANEL
# No LEDs on this panel: the screen is the only indicator.
OLED = 1
endif
ifdef OLED
C_DEFS += -DOLED
# Display driver + fonts push the program past the 128 KB internal flash.
BOOT_SRAM = 1
endif
ifdef LOG
C_DEFS += -DLOG
endif
ifdef KNOB_CHECK
C_DEFS += -DKNOB_CHECK
endif
ifdef BOOT_SRAM
APP_TYPE = BOOT_SRAM
endif

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
