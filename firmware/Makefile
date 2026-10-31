# Pedal firmware. Needs the ARM toolchain and the two Electro-Smith
# libraries in ../lib (see README → "Firmware setup").

TARGET = daisy-bass

CPP_SOURCES = main.cpp

LIBDAISY_DIR = ../lib/libDaisy
DAISYSP_DIR = ../lib/DaisySP

# Lets main.cpp include "dsp/EnvelopeFilter.h" from the repo root.
C_INCLUDES += -I..

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
