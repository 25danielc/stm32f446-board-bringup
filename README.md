# STM32F446 board bring-up test (no STM32CubeIDE needed)

## Option A: just flash a prebuilt file (no compiler needed)

Pick the file in `prebuilt/` that matches the frequency of crystal Y1
(8, 12, 16 or 25 MHz); it's usually printed on the crystal can. A build
for the wrong frequency is not harmless:
- crystal **faster** than the build (e.g. 16 MHz fitted, 8 MHz file): the PLL
  and the CPU run far beyond their limits; expect "HSE starts FAIL" or a
  crash (very fast LED blink).
- crystal **slower** than the build (e.g. 8 MHz fitted, 25 MHz file): the
  chip only runs slower; the text after "HSE starts" is garbled, or HSE
  is wrongly reported as FAIL, and every other test still runs.

So if you can't read the marking, start with 25 MHz and work downwards.

### With an ST-LINK on J3A (macOS)
    brew install stlink
    st-flash --reset write prebuilt/board_test_8MHz.bin 0x08000000

### With only a USB-UART adapter on J1A (no ST-LINK)
The ST bootloader on this chip talks on USART1 (J1A) only; USART2 (J2A)
is not a bootloader port (AN2606, STM32F446xx section).
1. Jumper J3A pin 7 (BOOT0) to J3A pin 1 (3V3), then press reset (SW20).
   PB2 (pin 28, BOOT1) must also be low at reset.
2. Adapter TX -> J1A pin 3, adapter RX -> J1A pin 2, GND -> J1A pin 4.
3. Flash:

        brew install stm32flash
        stm32flash -w prebuilt/board_test_8MHz.bin -v /dev/tty.usbserial-XXXX

   (`ls /dev/tty.usb*` shows the port name.)
4. Remove the BOOT0 jumper and press reset to run the test.

## Option B: build it yourself (~1 GB toolchain instead of CubeIDE)
    brew install --cask gcc-arm-embedded
    make HSE=8000000          # your crystal's frequency
    make flash HSE=8000000    # uses st-flash; same HSE as the build

Output goes to `build/<HSE>/` (one folder per crystal frequency).

## Watch the results
Open the serial port at 115200 8N1:

    screen /dev/tty.usbserial-XXXX 115200      (quit: Ctrl-A then K)

Use J2A (UART2) or J1A (UART1); the report prints on both.

## Before first power-up
- R20 must NOT be fitted (it puts 3.3 V on the VCAP_1 pin).
- VDDA (pin 13) needs a supply from 3V3; VSSA (pin 12) must go to GND.
- Power: 3.3 V into J3A pin 1 (+) and pin 8 (GND), current-limited to ~100 mA.
  Never 5 V on these pins.

## Folder contents
- `Core/Src/board_test.c`  the tests and peripheral settings
- `Core/Src/main.c`        pin/clock setup the HAL calls back into, fault handler
- `Drivers/`               ST's STM32F4 HAL (only the modules used) and Arm CMSIS (BSD-3 / Apache-2.0, licenses included)
- `prebuilt/`              ready-made `.bin` files, one per crystal frequency
