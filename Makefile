######################################
# ZX47 - a 48K ZX Spectrum for the SwissMicros DM42n (DMCP5 native program, .pg5)
# Based on the SwissMicros DMCP5_SDK Makefile (BSD-3), as Chess 47, NAV47, TERM47 and GB47
######################################
# make              build/ZX47.pg5 (loads on DMCP5 3.57 and later: interface 3.17)
# make test         the PC tests: the Z80 core (prelim, zexdoc) and the 48K machine (boot, keys, tape, snapshots)
# make test-z80 ZEX=all   zexall too (minutes)
# make sim          the PC simulator (build/sim/sim); make shots: its screens -> build/sim/run/*.png
# make demo         release/ZX47DEMO.TAP from test/demo.bas

TARGET    = ZX47
BUILD_DIR = build
PYTHON   ?= python3
IFC      ?= 17

CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

GEN       = $(BUILD_DIR)/gen
C_SOURCES = dmcp/sys/pgm_syscalls.c src/main.c src/zx.c src/screen.c src/keys.c third_party/z80/z80.c $(GEN)/rom48.c
ASM_SOURCES = dmcp/startup_pgm.s
C_INCLUDES = -Idmcp -Isrc -Ithird_party/z80

C_DEFS   = -D__weak="__attribute__((weak))" -D__packed="__attribute__((__packed__))" -DPLATFORM_IFC_VER=$(IFC)
CPUFLAGS = -mthumb -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS   = -std=gnu11 $(CPUFLAGS) $(C_DEFS) $(C_INCLUDES) -Wall -fdata-sections -ffunction-sections -O2 -g
CFLAGS  += -MD -MP -MF $(BUILD_DIR)/.dep/$(@F).d
LDSCRIPT = stm32_program.ld
LDFLAGS  = $(CPUFLAGS) -T$(LDSCRIPT) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections \
           -Wl,--wrap=_malloc_r -g

all: $(BUILD_DIR)/$(TARGET).pg5

$(GEN)/rom48.c: roms/48.rom tools/bin2c.py
	mkdir -p $(GEN)
	$(PYTHON) tools/bin2c.py $< $@ zx_rom48

OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)/.dep
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)/.dep
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

# QSPI CRC is compiled into the program header: if it changes, rebuild once.
$(BUILD_DIR)/$(TARGET).pg5: $(BUILD_DIR)/$(TARGET).elf tools/pg5tool.py
	$(OBJCOPY) --remove-section .qspi -O binary $< $(BUILD_DIR)/$(TARGET)_flash.bin
	$(OBJCOPY) --only-section   .qspi -O binary $< $(BUILD_DIR)/$(TARGET)_qspi.bin
	@if ! $(PYTHON) tools/pg5tool.py qspi $(BUILD_DIR)/$(TARGET)_qspi.bin src/qspi_crc.h; then \
	   rm -f $(BUILD_DIR)/pgm_syscalls.o $<; $(MAKE) --no-print-directory $@; exit $$?; fi
	$(PYTHON) tools/pg5tool.py pg5 $(BUILD_DIR)/$(TARGET)_flash.bin $@
	$(SIZE) $<

$(BUILD_DIR)/.dep:
	mkdir -p $@

# ------------------------------------------------------------------ PC tests
T = $(BUILD_DIR)/test
ZEX ?= doc
test: test-z80 test-zx

test-z80:
	mkdir -p $(T)
	gcc -O2 -Wall -Ithird_party/z80 -o $(T)/z80test test/z80test.c third_party/z80/z80.c
	$(T)/z80test test/zex/prelim.com $(if $(filter all,$(ZEX)),test/zex/zexdoc.cim test/zex/zexall.cim,test/zex/zexdoc.cim)

test-zx: $(GEN)/rom48.c
	mkdir -p $(T)
	gcc -O2 -Wall -Isrc -Ithird_party/z80 -o $(T)/zxtest test/zxtest.c src/zx.c third_party/z80/z80.c $(GEN)/rom48.c
	$(PYTHON) tools/mktap.py test/demo.bas $(T)/demo.tap ZX47DEMO
	$(T)/zxtest --sna $(T)/demo.sna
	$(PYTHON) tools/sna2z80.py $(T)/demo.sna $(T)/demo_v1.z80 1
	$(PYTHON) tools/sna2z80.py $(T)/demo.sna $(T)/demo_v3.z80 3
	$(T)/zxtest

demo:
	$(PYTHON) tools/mktap.py test/demo.bas release/ZX47DEMO.TAP ZX47DEMO

# ------------------------------------------------------------------ the PC simulator
S = $(BUILD_DIR)/sim
sim: $(GEN)/rom48.c
	mkdir -p $(S)
	gcc -O2 -w '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' -DZX47_SIM \
	    -Itest/sim -Isrc -Ithird_party/z80 -o $(S)/sim test/sim/sim.c src/main.c src/zx.c src/screen.c \
	    src/keys.c third_party/z80/z80.c $(GEN)/rom48.c

# the screens: boot, BASIC typed, the menu, the demo tape loaded and running
shots: sim test-zx
	rm -rf $(S)/run && mkdir -p $(S)/run/simdisk/ZX47
	cp $(T)/demo.tap $(S)/run/simdisk/ZX47/DEMO.TAP
	cd $(S)/run && ZX47_FILE=/ZX47/DEMO.TAP ../sim WAIT:3500 SNAP:boot 7 ADD 2 DOT+COS 3 ENTER WAIT:800 SNAP:basic \
	    ADD ADD EXIT WAIT:300 SNAP:menu F3 WAIT:10000 SNAP:demo
	for n in boot basic menu demo; do $(PYTHON) test/sim/render.py $(S)/run/$$n $(S)/run/$$n.png; done

clean:
	-rm -fR $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/.dep/*)

.PHONY: all test test-z80 test-zx demo sim shots clean
