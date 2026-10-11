/*
 * board_test.c - bring-up test for the STM32F446RET6 board.
 * Runs every test once, prints PASS/FAIL on USART1 (J1A) and USART2 (J2A)
 * at 115200 8N1, then loops: blinks the LED, toggles the test points and
 * echoes UART input. Every wait has a timeout, so a dead crystal or a
 * missing jumper can't stop the run.
 *
 * Pins (from the schematic): status LED PC5 (active high), SPI CS1 PA4,
 * test points PB0 PB1 PB4 PB6 PB7 PB10 PB14 PB15. Peripheral pins: main.c.
 */
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#if (HSE_VALUE % 1000000U) || (HSE_VALUE < 4000000U) || (HSE_VALUE > 26000000U)
#error "The PLL setup expects a whole-MHz crystal (HSE_VALUE) between 4 and 26 MHz."
#endif

void HardFault_Handler(void);

#define UART_115200_8N1 { .BaudRate = 115200, .WordLength = UART_WORDLENGTH_8B,  \
    .StopBits = UART_STOPBITS_1, .Parity = UART_PARITY_NONE, .Mode = UART_MODE_TX_RX, \
    .HwFlowCtl = UART_HWCONTROL_NONE, .OverSampling = UART_OVERSAMPLING_16 }

static UART_HandleTypeDef huart1 = { .Instance = USART1, .Init = UART_115200_8N1 };
static UART_HandleTypeDef huart2 = { .Instance = USART2, .Init = UART_115200_8N1 };
static SPI_HandleTypeDef hspi1 = { .Instance = SPI1, .Init = {
    .Mode = SPI_MODE_MASTER, .Direction = SPI_DIRECTION_2LINES,
    .DataSize = SPI_DATASIZE_8BIT, .CLKPolarity = SPI_POLARITY_LOW,
    .CLKPhase = SPI_PHASE_1EDGE, .NSS = SPI_NSS_SOFT,
    .BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16, .FirstBit = SPI_FIRSTBIT_MSB } };
static CAN_HandleTypeDef hcan1 = { .Instance = CAN1 };
static CAN_HandleTypeDef hcan2 = { .Instance = CAN2 };

static const uint8_t tp[] = { 0, 1, 4, 6, 7, 10, 14, 15 };   /* test points PBx */
#define TP_COUNT sizeof tp

static int g_fail;

/* ------------------------------------------------------------------ output */

static void out(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof buf - 1) n = sizeof buf - 1;
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 200);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)n, 200);
}

static void result(const char *name, int ok, const char *hint)
{
    out("  %-26s %s", name, ok ? "PASS" : "FAIL");
    if (!ok && hint) out("   <- %s", hint);
    out("\r\n");
    if (!ok) g_fail++;
}

/* ------------------------------------------------------------ chip / power */

static void test_chip(void)
{
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    const uint16_t cal = *(const uint16_t *)0x1FFF7A2AU;   /* VREFINT_CAL, taken at VDDA = 3.3 V */
    uint32_t sum = 0, mv = 0;
    char name[40];
    int n;

    out("Device ID 0x%03lX (0x421 = F446)\r\n", (unsigned long)HAL_GetDEVID());
    out("Unique ID %08lX-%08lX-%08lX\r\n",
        (unsigned long)uid[2], (unsigned long)uid[1], (unsigned long)uid[0]);
    result("Flash size 512 KB (RET6)", *(const uint16_t *)FLASHSIZE_BASE == 512,
           "wrong part fitted?");

    /* VDDA = 3.3 V * VREFINT_CAL / VREFINT reading (ADC1 channel 17) */
    __HAL_RCC_ADC1_CLK_ENABLE();
    ADC->CCR = ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE;  /* ADC clock PCLK2/4; VREFINT on */
    ADC1->SMPR1 = ADC_SMPR1_SMP17;                  /* 480 cycles on channel 17 */
    ADC1->SQR3 = 17;
    ADC1->CR2 = ADC_CR2_ADON;
    HAL_Delay(2);
    for (n = 0; n < 16; n++) {
        uint32_t t0 = HAL_GetTick();
        ADC1->CR2 |= ADC_CR2_SWSTART;
        while (!(ADC1->SR & ADC_SR_EOC) && HAL_GetTick() - t0 <= 5) { }
        if (!(ADC1->SR & ADC_SR_EOC)) break;
        sum += ADC1->DR;                            /* reading DR clears EOC */
    }
    ADC1->CR2 = 0;
    if (n == 16 && sum >= 16) mv = 3300U * cal / (sum / 16);
    snprintf(name, sizeof name, "VDDA = %lu mV", (unsigned long)mv);
    result(name, mv >= 3000 && mv <= 3600, "VDDA/VSSA not connected properly");
}

/* ----------------------------------------------------------------- clocks */

static void test_lse(void)
{
    RCC_OscInitTypeDef o = { .OscillatorType = RCC_OSCILLATORTYPE_LSE,
                             .LSEState = RCC_LSE_ON, .PLL.PLLState = RCC_PLL_NONE };
    out("Starting LSE (X1 32.768 kHz), up to 5 s...\r\n");
    int ok = HAL_RCC_OscConfig(&o) == HAL_OK;      /* HAL enables backup-domain access */
    if (!ok) {
        o.LSEState = RCC_LSE_OFF;
        HAL_RCC_OscConfig(&o);
    }
    result("LSE 32.768 kHz starts", ok, "check X1, C10/C11, PC14/PC15 solder");
}

/* SYSCLK 84 MHz from PLL(HSE): VCO in 1 MHz, VCO 336 MHz, /4 = 84 MHz.
 * Readable text after the switch means Y1 really is HSE_VALUE. */
static void test_hse(void)
{
    RCC_OscInitTypeDef o = {
        .OscillatorType = RCC_OSCILLATORTYPE_HSE, .HSEState = RCC_HSE_ON,
        .PLL = { .PLLState = RCC_PLL_ON, .PLLSource = RCC_PLLSOURCE_HSE,
                 .PLLM = HSE_VALUE / 1000000U, .PLLN = 336, .PLLP = RCC_PLLP_DIV4,
                 .PLLQ = 7, .PLLR = 2 } };
    RCC_ClkInitTypeDef c = {
        .ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2,
        .SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK, .AHBCLKDivider = RCC_SYSCLK_DIV1,
        .APB1CLKDivider = RCC_HCLK_DIV2,           /* 42 MHz (max 42 w/o over-drive) */
        .APB2CLKDivider = RCC_HCLK_DIV1 };         /* 84 MHz (max 84 w/o over-drive) */

    out("Starting HSE (Y1 %lu Hz)...\r\n", (unsigned long)HSE_VALUE);
    if (HAL_RCC_OscConfig(&o) != HAL_OK) {
        o.HSEState = RCC_HSE_OFF;
        o.PLL.PLLState = RCC_PLL_OFF;
        HAL_RCC_OscConfig(&o);
        result("HSE starts", 0, "check Y1, C12/C13, PH0/PH1 solder; staying on HSI");
        return;
    }
    result("HSE starts", 1, NULL);

    int ok = HAL_RCC_ClockConfig(&c, FLASH_LATENCY_2) == HAL_OK;
    HAL_UART_Init(&huart1);     /* baud registers depend on the bus clocks, */
    HAL_UART_Init(&huart2);     /* which change even if the switch fails    */
    if (ok)
        out("  Now on HSE+PLL, SYSCLK %lu Hz. If this line is readable,\r\n"
            "  Y1 really is %lu Hz.\r\n",
            (unsigned long)HAL_RCC_GetSysClockFreq(), (unsigned long)HSE_VALUE);
    result("Switch to 84 MHz PLL", ok, "PLL failed; staying on HSI");
}

/* ------------------------------------------------------------- test points */

static void tp_mode(uint32_t pins, uint32_t mode, uint32_t pull)
{
    GPIO_InitTypeDef g = { .Pin = pins, .Mode = mode, .Pull = pull,
                           .Speed = GPIO_SPEED_FREQ_LOW };
    HAL_GPIO_Init(GPIOB, &g);
}

/* Each test point must follow its own pull-up and pull-down (not stuck to a
 * rail), and no other test point may follow it when it is driven high then
 * low (bridge). Following both edges means a pin stuck high elsewhere isn't
 * mistaken for a bridge. */
static void test_points(void)
{
    uint32_t all = 0;
    for (unsigned i = 0; i < TP_COUNT; i++) all |= 1U << tp[i];

    for (unsigned i = 0; i < TP_COUNT; i++) {
        uint32_t pin = 1U << tp[i];
        char name[32], hint[32] = "";

        tp_mode(all, GPIO_MODE_INPUT, GPIO_PULLDOWN);
        tp_mode(pin, GPIO_MODE_INPUT, GPIO_PULLUP);
        HAL_Delay(1);
        if (!(GPIOB->IDR & pin)) {
            strcpy(hint, "stuck LOW (short to GND?)");
        } else {
            tp_mode(pin, GPIO_MODE_INPUT, GPIO_PULLDOWN);
            HAL_Delay(1);
            if (GPIOB->IDR & pin) {
                strcpy(hint, "stuck HIGH (short to 3V3?)");
            } else {
                HAL_GPIO_WritePin(GPIOB, pin, GPIO_PIN_SET);
                tp_mode(pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL);
                HAL_Delay(1);
                uint32_t high = GPIOB->IDR;
                HAL_GPIO_WritePin(GPIOB, pin, GPIO_PIN_RESET);
                HAL_Delay(1);
                uint32_t bridged = high & ~GPIOB->IDR & all & ~pin;
                if (bridged)
                    snprintf(hint, sizeof hint, "bridged to PB%d", __builtin_ctz(bridged));
            }
        }
        snprintf(name, sizeof name, "Test point PB%d", tp[i]);
        result(name, hint[0] == 0, hint);
    }
    /* leave them as low outputs for the scope test in the main loop */
    HAL_GPIO_WritePin(GPIOB, all, GPIO_PIN_RESET);
    tp_mode(all, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL);
}

/* -------------------------------------------------------------------- SPI */

/* Needs a jumper wire PA7 (MOSI) -> PA6 (MISO). CS1 stays high so any
 * device on the bus ignores the traffic. */
static void test_spi(void)
{
    uint8_t tx[8] = {0x55, 0xAA, 0x00, 0xFF, 0x12, 0x34, 0xA5, 0x5A};
    uint8_t rx[8] = {0};
    int ok = HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof tx, 100) == HAL_OK &&
             memcmp(tx, rx, sizeof tx) == 0;
    result("SPI1 loopback (MOSI->MISO)", ok, "needs jumper PA7->PA6; skip if not fitted");
    if (!ok)
        out("    got %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7]);
}

/* -------------------------------------------------------------------- CAN */

/* 500 kbit/s, sample point ~87 %, for the two possible APB1 clocks:
 * HSI 16 MHz / 2 / (1+13+2 tq), or PLL 42 MHz / 6 / (1+11+2 tq). */
static int can_start(CAN_HandleTypeDef *h, uint32_t mode)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    if (pclk1 == 16000000U)      { h->Init.Prescaler = 2; h->Init.TimeSeg1 = CAN_BS1_13TQ; }
    else if (pclk1 == 42000000U) { h->Init.Prescaler = 6; h->Init.TimeSeg1 = CAN_BS1_11TQ; }
    else return 0;
    h->Init.TimeSeg2           = CAN_BS2_2TQ;
    h->Init.SyncJumpWidth      = CAN_SJW_1TQ;
    h->Init.Mode               = mode;
    h->Init.AutoBusOff         = ENABLE;
    h->Init.AutoRetransmission = DISABLE;       /* one shot: no endless retries */
    if (HAL_CAN_Init(h) != HAL_OK) return 0;

    /* Accept everything. The filters live in CAN1; CAN2 owns banks 14..27. */
    CAN_FilterTypeDef f = {
        .FilterBank = (h->Instance == CAN1) ? 0 : 14, .SlaveStartFilterBank = 14,
        .FilterMode = CAN_FILTERMODE_IDMASK, .FilterScale = CAN_FILTERSCALE_32BIT,
        .FilterFIFOAssignment = CAN_RX_FIFO0, .FilterActivation = ENABLE };
    HAL_CAN_ConfigFilter(h, &f);
    return HAL_CAN_Start(h) == HAL_OK;
}

static int can_send_recv(CAN_HandleTypeDef *tx, CAN_HandleTypeDef *rx, uint32_t id)
{
    CAN_TxHeaderTypeDef th = { .StdId = id, .IDE = CAN_ID_STD, .RTR = CAN_RTR_DATA, .DLC = 8 };
    CAN_RxHeaderTypeDef rh;
    uint8_t d[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, (uint8_t)id};
    uint8_t r[8];
    uint32_t mb;

    while (HAL_CAN_GetRxFifoFillLevel(rx, CAN_RX_FIFO0))
        HAL_CAN_GetRxMessage(rx, CAN_RX_FIFO0, &rh, r);
    if (HAL_CAN_AddTxMessage(tx, &th, d, &mb) != HAL_OK) return 0;

    uint32_t t0 = HAL_GetTick();
    while (HAL_CAN_GetRxFifoFillLevel(rx, CAN_RX_FIFO0) == 0) {
        if (HAL_GetTick() - t0 > 50) { HAL_CAN_AbortTxRequest(tx, mb); return 0; }
    }
    return HAL_CAN_GetRxMessage(rx, CAN_RX_FIFO0, &rh, r) == HAL_OK &&
           rh.StdId == id && rh.DLC == 8 && memcmp(d, r, 8) == 0;
}

static void test_can(void)
{
    /* 1) Loopback: the RX pin is ignored, so this needs no transceiver or
     *    bus and only proves the peripheral and its clock. */
    result("CAN1 internal loopback", can_start(&hcan1, CAN_MODE_LOOPBACK) &&
           can_send_recv(&hcan1, &hcan1, 0x101), "internal: not a wiring fault");
    HAL_CAN_Stop(&hcan1);
    result("CAN2 internal loopback", can_start(&hcan2, CAN_MODE_LOOPBACK) &&
           can_send_recv(&hcan2, &hcan2, 0x102), "internal: not a wiring fault");
    HAL_CAN_Stop(&hcan2);

    /* 2) Real bus: CAN1 <-> CAN2 through both transceivers. Wire CANH-CANH,
     *    CANL-CANL between the two ports with 120 ohm at each end (~60 ohm).
     *    Joining needs 11 recessive (high) bits on RX. */
    int s1 = can_start(&hcan1, CAN_MODE_NORMAL);
    int s2 = can_start(&hcan2, CAN_MODE_NORMAL);
    result("CAN1 joins bus", s1, "RX pin PB8 not idle-high: transceiver power/standby pin?");
    result("CAN2 joins bus", s2, "RX pin PB12 not idle-high: transceiver power/standby pin?");
    if (s1 && s2) {
        result("CAN1 -> CAN2 over the bus", can_send_recv(&hcan1, &hcan2, 0x123),
               "bus wiring / termination / transceiver");
        result("CAN2 -> CAN1 over the bus", can_send_recv(&hcan2, &hcan1, 0x321),
               "bus wiring / termination / transceiver");
    }
    HAL_CAN_Stop(&hcan1);
    HAL_CAN_Stop(&hcan2);
}

/* -------------------------------------------------------------- main loop */

static void echo(UART_HandleTypeDef *h)
{
    if (h->Instance->SR & USART_SR_RXNE) {
        uint8_t ch = (uint8_t)h->Instance->DR;
        HAL_UART_Transmit(h, &ch, 1, 10);
    } else if (h->Instance->SR & USART_SR_ORE) {
        (void)h->Instance->DR;                     /* clear overrun */
    }
}

void BoardTest_Run(void)
{
    if (HAL_UART_Init(&huart1) != HAL_OK || HAL_UART_Init(&huart2) != HAL_OK ||
        HAL_SPI_Init(&hspi1) != HAL_OK)
        HardFault_Handler();

    /* LED PC5 and CS1 PA4 (high = SPI device deselected) as outputs */
    GPIO_InitTypeDef g = { .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL,
                           .Speed = GPIO_SPEED_FREQ_LOW };
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    g.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOC, &g);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOA, &g);

    for (int i = 0; i < 6; i++) {                  /* LED works: 3 quick blinks */
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_5);
        HAL_Delay(100);
    }

    out("\r\n\r\n===== STM32F446 board bring-up test =====\r\n");
    out("(this text on both UART1/J1A and UART2/J2A means both TX paths work)\r\n");
    uint32_t csr = RCC->CSR;
    out("Reset cause:%s%s%s%s%s%s\r\n",
        (csr & RCC_CSR_BORRSTF)  ? " BOR" : "",  (csr & RCC_CSR_PORRSTF)  ? " POR" : "",
        (csr & RCC_CSR_PINRSTF)  ? " PIN(NRST)" : "", (csr & RCC_CSR_SFTRSTF) ? " SOFTWARE" : "",
        (csr & RCC_CSR_IWDGRSTF) ? " IWDG" : "", (csr & RCC_CSR_WWDGRSTF) ? " WWDG" : "");
    __HAL_RCC_CLEAR_RESET_FLAGS();

    test_chip();
    test_lse();
    test_hse();
    test_points();
    test_spi();
    test_can();

    out("==========================================\r\n");
    out("%d failure(s). LED: slow blink = all pass, fast blink = failures.\r\n", g_fail);
    out("Test points now toggle: PB0 500 Hz, PB1 250, PB4 167, PB6 125,\r\n"
        "PB7 100, PB10 83, PB14 71, PB15 62.5 Hz.\r\n");
    out("Type on either UART: characters are echoed (tests RX).\r\n");
    out("Press the reset button (SW20) to run again.\r\n");

    /* 1 ms per pass: test point i toggles every i+1 ms = 500/(i+1) Hz */
    uint32_t ms = 0, blink = g_fail ? 100 : 500;
    for (;;) {
        uint32_t t0 = HAL_GetTick();
        ms++;
        for (unsigned i = 0; i < TP_COUNT; i++)
            if (ms % (i + 1) == 0) HAL_GPIO_TogglePin(GPIOB, 1U << tp[i]);
        if (ms % blink == 0) HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_5);
        echo(&huart1);
        echo(&huart2);
        while (HAL_GetTick() == t0) { }
    }
}
