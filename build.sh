#!/bin/bash

TARGET = led_fft
# MCU: part number to build for
MCU = msp430g2553
# SOURCES: list of input source sources
SOURCES = led_fft.c fix_fft.c
# INCLUDES: list of includes, by default, use Includes directory
INCLUDES = -IInclude
# OUTDIR: directory to use for output
OUTDIR = build
# define flags

CFLAGS = -mmcu=$(MCU) -g -Os -Wall -Wunused -ffunction-sections -fdata-sections -fno-inline-small-functions $(INCLUDES)
ASFLAGS = -mmcu=$(MCU) -x assembler-with-cpp -Wa,-gstabs
LDFLAGS = -mmcu=$(MCU) -Wl,-Map=$(OUTDIR)/$(TARGET).map -Wl,--relax -Wl,--gc-sections
LIBS=-lm

CFLAGS=-Os -Wall -ffunction-sections -fdata-sections -fno-inline-small-functions
LDFLAGS=-mmcu=$(MCU) -Wl,-Map=$(OUTDIR)/$(TARGET).map -Wl,--relax -Wl,--gc-sections

msp430-gcc $CFLAGS
	-Wl,-Map=gcc/lp_8bitfft.map,--cref \
	-Wl,--relax \
	-Wl,--gc-sections \
	-mmcu=msp430g2553 \
	-c fix_fft.c -o gcc/fix_fft.o

msp430-gcc $CFLAGS
	-Wl,-Map=gcc/lp_8bitfft.map,--cref \
	-Wl,--relax -Wl,--gc-sections \
	-mmcu=msp430g2553 \
	-c lp_8bitfft.c -o gcc/lp_8bitfft.o

msp430-gcc -Os \
	-Wall \
	-ffunction-sections \
	-fdata-sections \
	-fno-inline-small-functions \
	-Wl,-Map=gcc/lp_8bitfft.map,--cref \
	-Wl,--relax \
	-Wl,--gc-sections \
	-mmcu=msp430g2553 \
	-o gcc/lp_8bitfft.elf gcc/fix_fft.o gcc/lp_8bitfft.o \
	-lm
