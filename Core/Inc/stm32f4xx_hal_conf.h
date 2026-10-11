/* HAL configuration: only the modules this firmware uses. */
#ifndef STM32F4xx_HAL_CONF_H
#define STM32F4xx_HAL_CONF_H

#define HAL_MODULE_ENABLED
#define HAL_CAN_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED      /* header only: UART/SPI handles contain DMA pointers */
#define HAL_FLASH_MODULE_ENABLED    /* header only: flash latency / cache macros */
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#ifndef HSE_VALUE                   /* the Makefile sets this from HSE=... */
#define HSE_VALUE              8000000U
#endif
#define HSE_STARTUP_TIMEOUT    100U       /* ms */
#define HSI_VALUE              16000000U
#define LSI_VALUE              32000U
#define LSE_VALUE              32768U
#define LSE_STARTUP_TIMEOUT    5000U      /* ms */
#define EXTERNAL_CLOCK_VALUE   12288000U  /* I2S_CKIN, unused */

#define VDD_VALUE                  3300U
#define TICK_INT_PRIORITY          0x0FU
#define USE_RTOS                   0U
#define PREFETCH_ENABLE            1U
#define INSTRUCTION_CACHE_ENABLE   1U
#define DATA_CACHE_ENABLE          1U
#define USE_SPI_CRC                0U

#include "stm32f4xx_hal_rcc.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_cortex.h"
#include "stm32f4xx_hal_can.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_spi.h"
#include "stm32f4xx_hal_uart.h"

#define assert_param(expr) ((void)0U)

#endif
