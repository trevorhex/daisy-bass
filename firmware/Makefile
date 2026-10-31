# Pedal firmware. Needs the Arm toolchain and the two Electro-Smith
# libraries in ../lib (see README → "Firmware setup").
#
# Optional flags (rebuild with `make clean` first when changing them):
#   OLED=1        draw on a 128x64 SSD1306 on D11/D12 (implies BOOT_SRAM=1)
#   LOG=1         print CPU load over USB serial once a second
#   KNOB_CHECK=1  control-mapping diagnostic instead of audio
#   BOOT_SRAM=1   run via Electro-Smith's bootloader (~480 KB programs);
#                 install it once with `make program-boot`

TARGET = daisy-bass

CPP_SOURCES = main.cpp

LIBDAISY_DIR = ../lib/libDaisy
DAISYSP_DIR = ../lib/DaisySP

# Lets main.cpp include "dsp/…" and "control/…" from the repo root.
C_INCLUDES += -I..

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
