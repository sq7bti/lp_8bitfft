# MSP430 Makefile
# user configuration:
#######################################
# TARGET: name of the output file
TARGET = main
# MCU: part number to build for
MCU = msp430g2553
# SOURCES: list of input source sources
SOURCES = fix_fft.c led_fft.c
# INCLUDES: list of includes, by default, use Includes directory
INCLUDES = -IInclude -I/opt/ti/msp430-gcc/include
# OUTDIR: directory to use for output
OUTDIR = build
# define flags
CFLAGS = -mmcu=$(MCU) -g -Os -Wall -Wunused $(INCLUDES)
# CFLAGS += -mtiny-printf
ASFLAGS = -mmcu=$(MCU) -x assembler-with-cpp -Wa,-gstabs
#LDFLAGS = -mmcu=$(MCU) -Wl,-Map=$(OUTDIR)/$(TARGET).map -lm
LDFLAGS = -mmcu=$(MCU) -Wl,-Map=$(OUTDIR)/$(TARGET).map
#######################################
# end of user configuration
#######################################
#
#######################################
# binaries
#######################################
CC_PREFIX       = msp430-elf
CC      	= ${CC_PREFIX}-gcc
LD      	= ${CC_PREFIX}-ld
AR      	= ${CC_PREFIX}-ar
AS      	= ${CC_PREFIX}-as
GASP    	= ${CC_PREFIX}-gasp
NM      	= ${CC_PREFIX}-nm
OBJCOPY 	= ${CC_PREFIX}-objcopy
STRIP		= ${CC_PREFIX}-strip
SIZE		= ${CC_PREFIX}-size
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
	$(SIZE) $@

$(OUTDIR)/%.o: src/%.c | $(OUTDIR)
	$(CC) -c $(CFLAGS) -o $@ $<

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
