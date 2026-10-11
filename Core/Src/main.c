/*
 * main.c - pin/clock setup the HAL calls back into, and the fault handler.
 * The chip runs on the internal 16 MHz HSI until board_test.c tests the crystals.
 */
#include "stm32f4xx_hal.h"

void BoardTest_Run(void);   /* board_test.c, never returns */

int main(void)
{
    HAL_Init();             /* SysTick 1 ms on HSI 16 MHz */
    BoardTest_Run();
}

static void af_pins(GPIO_TypeDef *port, uint32_t pins, uint32_t af, uint32_t pull)
{
    GPIO_InitTypeDef g = {
        .Pin = pins, .Mode = GPIO_MODE_AF_PP, .Pull = pull,
        .Speed = GPIO_SPEED_FREQ_VERY_HIGH, .Alternate = af,
    };
    HAL_GPIO_Init(port, &g);
}

void HAL_UART_MspInit(UART_HandleTypeDef *h)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    if (h->Instance == USART1) {        /* PA9 TX / PA10 RX -> J1A */
        __HAL_RCC_USART1_CLK_ENABLE();
        af_pins(GPIOA, GPIO_PIN_9 | GPIO_PIN_10, GPIO_AF7_USART1, GPIO_PULLUP);
    } else {                            /* USART2: PA2 TX / PA3 RX -> J2A */
        __HAL_RCC_USART2_CLK_ENABLE();
        af_pins(GPIOA, GPIO_PIN_2 | GPIO_PIN_3, GPIO_AF7_USART2, GPIO_PULLUP);
    }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *h)
{
    (void)h;                            /* SPI1: PA5 SCK / PA6 MISO / PA7 MOSI */
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    af_pins(GPIOA, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, GPIO_AF5_SPI1, GPIO_NOPULL);
}

void HAL_CAN_MspInit(CAN_HandleTypeDef *h)
{
    __HAL_RCC_CAN1_CLK_ENABLE();        /* CAN2 also needs CAN1's clock */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    if (h->Instance == CAN1) {          /* PB8 RX / PB9 TX */
        af_pins(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_AF9_CAN1, GPIO_NOPULL);
    } else {                            /* CAN2: PB12 RX / PB13 TX */
        __HAL_RCC_CAN2_CLK_ENABLE();
        af_pins(GPIOB, GPIO_PIN_12 | GPIO_PIN_13, GPIO_AF9_CAN2, GPIO_NOPULL);
    }
}

void SysTick_Handler(void) { HAL_IncTick(); }

/* HardFault (MemManage/BusFault/UsageFault escalate to it) or failed HAL
 * init: blink the status LED (PC5) very fast forever. */
void HardFault_Handler(void)
{
    __disable_irq();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    GPIOC->MODER = (GPIOC->MODER & ~(3U << 10)) | (1U << 10);
    for (;;) {
        GPIOC->ODR ^= (1U << 5);
        for (volatile uint32_t i = 0; i < 100000; i++) { }
    }
}
