# Build:   make                 (assumes an 8 MHz crystal on Y1)
#          make HSE=16000000    (set to Y1's real frequency, whole MHz, 4-26 MHz)
# Flash:   make flash [HSE=...] (needs st-flash: brew install stlink)
# Output:  build/<HSE>/board_test.bin / .hex / .elf  (one folder per HSE value,
#          so switching HSE never reuses objects built for another crystal)

HSE    ?= 8000000
TARGET  = board_test
BUILD   = build/$(HSE)

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

MCU     = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS    = -DSTM32F446xx -DUSE_HAL_DRIVER -DHSE_VALUE=$(HSE)U
INCS    = -ICore/Inc \
          -IDrivers/STM32F4xx_HAL_Driver/Inc \
          -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
          -IDrivers/CMSIS/Include
CFLAGS  = $(MCU) $(DEFS) $(INCS) -Os -g -Wall -Wextra -ffunction-sections -fdata-sections -std=gnu11
LDFLAGS = $(MCU) -TSTM32F446RETx_FLASH.ld --specs=nano.specs --specs=nosys.specs -nostartfiles \
          -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map

SRCS    = $(wildcard Core/Src/*.c) $(wildcard Drivers/STM32F4xx_HAL_Driver/Src/*.c)
OBJS    = $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o))) $(BUILD)/startup_stm32f446xx.o
vpath %.c $(sort $(dir $(SRCS)))

all: $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex
	$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.c Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/startup_stm32f446xx.o: Core/startup_stm32f446xx.s | $(BUILD)
	$(CC) $(MCU) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

flash: $(BUILD)/$(TARGET).bin
	st-flash --reset write $< 0x08000000

clean:
	rm -rf build

.PHONY: all flash clean
