# MSP430 Makefile
# #####################################
#
# Part of the uCtools project
# uctools.github.com
#
#######################################
# user configuration:
#######################################
# TARGET: name of the output file
TARGET = led_fft
# MCU: part number to build for
MCU = msp430g2553
# SOURCES: list of input source sources
SOURCES = led_fft.c fix_fft.c
# INCLUDES: list of includes, by default, use Includes directory
#INCLUDES = -IInclude
INCLUDES =
# OUTDIR: directory to use for output
OUTDIR = build
# define flags
#CFLAGS = -mmcu=$(MCU) -g -Os -Wall -Wunused -ffunction-sections -fdata-sections -fno-inline-small-functions $(INCLUDES)
CFLAGS = -Os -Wall -ffunction-sections -fdata-sections -fno-inline-small-functions -mmcu=$(MCU) $(INCLUDES)
ASFLAGS = -mmcu=$(MCU) -x assembler-with-cpp -Wa,-gstabs
LDFLAGS = -mmcu=$(MCU) -Wl,-Map=$(OUTDIR)/$(TARGET).map -Wl,--relax -Wl,--gc-sections
#LIBS=-lm
#######################################
# end of user configuration
#######################################
#
#######################################
# binaries
#######################################
CC      	= msp430-gcc
LD      	= msp430-ld
AR      	= msp430-ar
AS      	= msp430-gcc
GASP    	= msp430-gasp
NM      	= msp430-nm
OBJCOPY 	= msp430-objcopy
STRIP		= msp430-strip
SIZE		= msp430-size
MAKETXT 	= srec_cat
UNIX2DOS	= unix2dos
RM      	= rm -f
MKDIR		= mkdir -p
#######################################

# file that includes all dependencies
DEPEND = $(SOURCES:.c=.d)

# list of object files, placed in the build directory regardless of source path
OBJECTS = $(addprefix $(OUTDIR)/,$(notdir $(SOURCES:.c=.o)))

# default: build hex file and TI TXT file
all: $(OUTDIR)/$(TARGET).hex $(OUTDIR)/$(TARGET).txt

# TI TXT file
$(OUTDIR)/%.txt: $(OUTDIR)/%.hex
	$(MAKETXT) -O $@ -TITXT $< -I
	$(UNIX2DOS) $(OUTDIR)/$(TARGET).txt

# intel hex file
$(OUTDIR)/%.hex: $(OUTDIR)/%.elf
	$(OBJCOPY) -O ihex $< $@

# elf file
$(OUTDIR)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@
	$(STRIP) $@
	$(SIZE) --format=sysv $@

$(OUTDIR)/%.o: src/%.c | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ -c $<

# assembly listing
%.lst: %.c
	$(CC) -c $(ASFLAGS) -Wa,-anlhd $< > $@

# create the output directory
$(OUTDIR):
	$(MKDIR) $(OUTDIR)

# remove build artifacts and executables
clean:
	-$(RM) $(OUTDIR)/*

.PHONY: all clean
