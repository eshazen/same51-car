# Bare-metal SAM E51 microSD (SPI mode) test — arm-none-eabi-gcc
# Target board: SAM E51 Curiosity Nano (ATSAME51J20A, on-board nEDBG debugger)
#
#   make                    build build/sdcard.elf, .bin, .hex
#   make flash              build, then program the board over USB (OpenOCD + nEDBG)
#   make DEVICE=same51j19a  build for another SAME51 part
#   make clean

DEVICE  ?= same51j20a
TARGET  := sdcard
BUILD   := build

DFP     := ../vendor/dfp
CMSIS   := ../vendor/CMSIS/Core/Include

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
OPENOCD ?= openocd
OPENOCD_CFG ?= board/microchip_same51_curiosity_nano.cfg

DEVICE_DEFINE := __$(shell echo $(DEVICE) | tr a-z A-Z)__
LDSCRIPT := $(DFP)/gcc/gcc/$(DEVICE)_flash.ld

SRCS := src/main.c src/sd_spi.c \
        $(DFP)/gcc/system_$(DEVICE).c \
        $(DFP)/gcc/gcc/startup_$(DEVICE).c
OBJS := $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o)))
vpath %.c $(sort $(dir $(SRCS)))

ARCH    := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16

CFLAGS  := $(ARCH) -std=c11 -Os -g3 -Wall -Wextra \
           -ffunction-sections -fdata-sections \
           -D$(DEVICE_DEFINE) -I$(DFP)/include -I$(CMSIS) \
           -MMD -MP

LDFLAGS := $(ARCH) -T$(LDSCRIPT) -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage \
           -nostartfiles --specs=nano.specs

.PHONY: all clean flash
all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD):
	mkdir -p $@

flash: $(BUILD)/$(TARGET).elf
	$(OPENOCD) -f $(OPENOCD_CFG) -c "program $< verify reset exit"

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
