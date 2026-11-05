# glitch_lab firmware — runs INSIDE the eCTF `build-hsm` Docker image (TI clang + MSPM0 SDK).
# Invoke via ../build.ps1, not directly on Windows.
#
#   LAYOUT=bootloader (default)  linked at 0x6000, flash over UART with `uvx ectf hw ... flash`
#   LAYOUT=bare                  linked at 0x0,    flash over SWD with Uniflash (erases bootloader)

SDK      ?= $(MSPM0_SDK_INSTALL_DIR)
CGT      ?= $(TICLANG_ARMCOMPILER)
CC       := $(CGT)/bin/tiarmclang
OBJCOPY  := $(CGT)/bin/tiarmobjcopy
OBJDUMP  := $(CGT)/bin/tiarmobjdump

OUTDIR   ?= /out
LAYOUT   ?= bootloader
NAME     := glitchlab
OBJDIR   := $(OUTDIR)/obj_$(LAYOUT)

ifeq ($(LAYOUT),bare)
LINKERFILE := firmware_bare.ld
else
LINKERFILE := firmware.ld
endif

SRCS := main.c levels.c ti_msp_dl_config.c startup_mspm0l222x_ticlang.c
OBJS := $(addprefix $(OBJDIR)/,$(SRCS:.c=.o))

CFLAGS := -D__MSPM0L2228__ -O2 -gdwarf-3 -Wall \
          -mcpu=cortex-m0plus -march=thumbv6m -mfloat-abi=soft -mthumb \
          -I./inc \
          -I$(SDK)/source \
          -I$(SDK)/source/third_party/CMSIS/Core/Include

LFLAGS := -l$(SDK)/source/ti/driverlib/lib/ticlang/m0p/mspm0l122x_l222x/driverlib.a \
          -L$(SDK)/source \
          ./$(LINKERFILE) \
          -Wl,-m,$(OUTDIR)/$(NAME)_$(LAYOUT).map \
          -Wl,--rom_model -Wl,--warn_sections \
          -L$(CGT)/lib -llibc.a

OUT := $(OUTDIR)/$(NAME)_$(LAYOUT)

all: $(OUT).bin $(OUT).lst
	@echo "built $(OUT).elf / .bin / .lst  (layout=$(LAYOUT))"

$(OBJDIR):
	@mkdir -p $@

$(OBJDIR)/%.o: src/%.c inc/lab.h | $(OBJDIR)
	@echo CC $<
	@$(CC) $(CFLAGS) -c $< -o $@

$(OUT).elf: $(OBJS) $(LINKERFILE)
	@echo LD $@
	@$(CC) -Wl,-u,_c_int00 $(OBJS) $(LFLAGS) -o $@

$(OUT).bin: $(OUT).elf
	@$(OBJCOPY) -O binary $< $@

# Disassembly with interleaved source: map a winning ext_offset to the instruction you hit.
$(OUT).lst: $(OUT).elf
	@$(OBJDUMP) -d -S $< > $@

clean:
	rm -rf $(OBJDIR) $(OUT).*

.PHONY: all clean
