/*
 * main.c - hand-written replacement for the CubeMX-generated setup.
 * Creates the peripheral handles board_test.c uses and sets up their pins.
 * The chip runs on the internal 16 MHz HSI clock until board_test.c
 * tests the crystals.
 */
#include "main.h"
#include "board_test.h"

UART_HandleTypeDef huart1;   /* PA9 TX / PA10 RX  -> J1A */
UART_HandleTypeDef huart2;   /* PA2 TX / PA3 RX   -> J2A */
SPI_HandleTypeDef  hspi1;    /* PA5 SCK / PA6 MISO / PA7 MOSI */
CAN_HandleTypeDef  hcan1;    /* PB8 RX / PB9 TX   -> SZB */
CAN_HandleTypeDef  hcan2;    /* PB12 RX / PB13 TX -> SZA */

static void uart_init(UART_HandleTypeDef *h, USART_TypeDef *inst)
{
    h->Instance          = inst;
    h->Init.BaudRate     = 115200;
    h->Init.WordLength   = UART_WORDLENGTH_8B;
    h->Init.StopBits     = UART_STOPBITS_1;
    h->Init.Parity       = UART_PARITY_NONE;
    h->Init.Mode         = UART_MODE_TX_RX;
    h->Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    h->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(h) != HAL_OK) Error_Handler();
}

static void spi1_init(void)
{
    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 10;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();
}

int main(void)
{
    HAL_Init();                       /* SysTick 1 ms on HSI 16 MHz */

    uart_init(&huart1, USART1);
    uart_init(&huart2, USART2);
    spi1_init();
    hcan1.Instance = CAN1;            /* configured by board_test.c */
    hcan2.Instance = CAN2;

    BoardTest_Run();                  /* never returns */
}

/* ---- pin setup the HAL calls from HAL_xxx_Init ---- */

static void af_pins(GPIO_TypeDef *port, uint32_t pins, uint32_t af, uint32_t pull)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = pins; g.Mode = GPIO_MODE_AF_PP; g.Pull = pull;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate = af;
    HAL_GPIO_Init(port, &g);
}

void HAL_UART_MspInit(UART_HandleTypeDef *h)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    if (h->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        af_pins(GPIOA, GPIO_PIN_9 | GPIO_PIN_10, GPIO_AF7_USART1, GPIO_PULLUP);
    } else if (h->Instance == USART2) {
        __HAL_RCC_USART2_CLK_ENABLE();
        af_pins(GPIOA, GPIO_PIN_2 | GPIO_PIN_3, GPIO_AF7_USART2, GPIO_PULLUP);
    }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *h)
{
    if (h->Instance == SPI1) {
        __HAL_RCC_SPI1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        af_pins(GPIOA, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, GPIO_AF5_SPI1, GPIO_NOPULL);
    }
}

void HAL_CAN_MspInit(CAN_HandleTypeDef *h)
{
    __HAL_RCC_CAN1_CLK_ENABLE();      /* CAN2 also needs CAN1's clock */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    if (h->Instance == CAN1) {
        af_pins(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_AF9_CAN1, GPIO_NOPULL);
    } else if (h->Instance == CAN2) {
        __HAL_RCC_CAN2_CLK_ENABLE();
        af_pins(GPIOB, GPIO_PIN_12 | GPIO_PIN_13, GPIO_AF9_CAN2, GPIO_NOPULL);
    }
}

/* ---- interrupts and faults ---- */

void SysTick_Handler(void) { HAL_IncTick(); }

/* On any fault: blink the status LED (PC5) very fast forever. */
void Error_Handler(void)
{
    __disable_irq();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    GPIOC->MODER = (GPIOC->MODER & ~(3U << 10)) | (1U << 10);
    for (;;) {
        GPIOC->ODR ^= (1U << 5);
        for (volatile uint32_t i = 0; i < 100000; i++) { }
    }
}
void HardFault_Handler(void)  { Error_Handler(); }
void MemManage_Handler(void)  { Error_Handler(); }
void BusFault_Handler(void)   { Error_Handler(); }
void UsageFault_Handler(void) { Error_Handler(); }
