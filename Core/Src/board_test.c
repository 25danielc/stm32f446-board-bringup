/*
 * board_test.c - bring-up test for the STM32F446RET6 board
 *
 * Expects a STM32CubeIDE project with (in the .ioc):
 *   SYS    Debug = Serial Wire
 *   RCC    HSE = Disable, LSE = Disable   (this file starts them itself, with
 *                                          timeouts, so a bad crystal can't hang boot)
 *   USART1 Asynchronous 115200 8N1  PA9 / PA10   -> J1A
 *   USART2 Asynchronous 115200 8N1  PA2 / PA3    -> J2A
 *   SPI1   Full-Duplex Master, NSS Disable, prescaler 16+   PA5 / PA6 / PA7
 *   CAN1   Activated, pins moved to PB8 (RX) / PB9 (TX)    -> SZB sheet
 *   CAN2   Activated, PB12 (RX) / PB13 (TX)                -> SZA sheet
 * GPIOs (LED, CS1, test points) are configured here, no .ioc setup needed.
 *
 * Board pin map (from the schematic):
 *   STATUS_1 LED (yellow, active high) ... PC5  (pin 25)
 *   CS1 ...................................... PA4
 *   Test points TP0..TP15 .......... PB0 PB1 PB4 PB6 PB7 PB10 PB14 PB15
 */
#include "main.h"
#include "board_test.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ---- Set this to the frequency of Y1 (830106162701). Check its datasheet. ---- */
#ifndef BOARD_HSE_HZ                 /* the Makefile passes this from HSE=... */
#define BOARD_HSE_HZ      8000000U
#endif

#define CAN_BITRATE       500000U

#if (HSE_VALUE != BOARD_HSE_HZ)
#error "HSE_VALUE (stm32f4xx_hal_conf.h or project define) must equal BOARD_HSE_HZ (your Y1 crystal)."
#endif
#if ((BOARD_HSE_HZ % 1000000U) != 0U) || (BOARD_HSE_HZ < 4000000U) || (BOARD_HSE_HZ > 26000000U)
#error "This test's PLL setup expects a whole-MHz crystal between 4 and 26 MHz."
#endif

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern SPI_HandleTypeDef  hspi1;
extern CAN_HandleTypeDef  hcan1;
extern CAN_HandleTypeDef  hcan2;

#define LED_PORT   GPIOC
#define LED_PIN    GPIO_PIN_5
#define CS1_PORT   GPIOA
#define CS1_PIN    GPIO_PIN_4
#define TP_PORT    GPIOB

static const uint16_t tp_pins[] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_4, GPIO_PIN_6,
    GPIO_PIN_7, GPIO_PIN_10, GPIO_PIN_14, GPIO_PIN_15
};
static const char *const tp_names[] = {
    "PB0", "PB1", "PB4", "PB6", "PB7", "PB10", "PB14", "PB15"
};
#define TP_COUNT (sizeof tp_pins / sizeof tp_pins[0])

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

/* ------------------------------------------------------------------- GPIO */

static void gpio_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    g.Pin = LED_PIN;
    HAL_GPIO_Init(LED_PORT, &g);

    HAL_GPIO_WritePin(CS1_PORT, CS1_PIN, GPIO_PIN_SET);   /* SPI device deselected */
    g.Pin = CS1_PIN;
    HAL_GPIO_Init(CS1_PORT, &g);
}

static void tp_mode(uint16_t pin, uint32_t mode, uint32_t pull)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = pin; g.Mode = mode; g.Pull = pull; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TP_PORT, &g);
}

/* Each test point: must follow its own pull-up/pull-down (not stuck to a rail),
 * and driving it high must not pull any other test point high (no bridges). */
static void test_points(void)
{
    char name[32], hint[64];
    for (unsigned i = 0; i < TP_COUNT; i++) {
        int ok = 1;
        hint[0] = 0;

        tp_mode(tp_pins[i], GPIO_MODE_INPUT, GPIO_PULLUP);
        HAL_Delay(1);
        if (HAL_GPIO_ReadPin(TP_PORT, tp_pins[i]) != GPIO_PIN_SET) {
            ok = 0; snprintf(hint, sizeof hint, "stuck LOW (short to GND?)");
        }
        tp_mode(tp_pins[i], GPIO_MODE_INPUT, GPIO_PULLDOWN);
        HAL_Delay(1);
        if (ok && HAL_GPIO_ReadPin(TP_PORT, tp_pins[i]) != GPIO_PIN_RESET) {
            ok = 0; snprintf(hint, sizeof hint, "stuck HIGH (short to 3V3?)");
        }

        if (ok) {
            for (unsigned j = 0; j < TP_COUNT; j++)
                if (j != i) tp_mode(tp_pins[j], GPIO_MODE_INPUT, GPIO_PULLDOWN);
            HAL_GPIO_WritePin(TP_PORT, tp_pins[i], GPIO_PIN_SET);
            tp_mode(tp_pins[i], GPIO_MODE_OUTPUT_PP, GPIO_NOPULL);
            HAL_Delay(1);
            for (unsigned j = 0; j < TP_COUNT && ok; j++) {
                if (j != i && HAL_GPIO_ReadPin(TP_PORT, tp_pins[j]) == GPIO_PIN_SET) {
                    ok = 0; snprintf(hint, sizeof hint, "bridged to %s", tp_names[j]);
                }
            }
            HAL_GPIO_WritePin(TP_PORT, tp_pins[i], GPIO_PIN_RESET);
            tp_mode(tp_pins[i], GPIO_MODE_INPUT, GPIO_PULLDOWN);
        }
        snprintf(name, sizeof name, "Test point %s", tp_names[i]);
        result(name, ok, hint);
    }
    /* leave them as outputs for the scope test in the main loop */
    for (unsigned i = 0; i < TP_COUNT; i++) {
        HAL_GPIO_WritePin(TP_PORT, tp_pins[i], GPIO_PIN_RESET);
        tp_mode(tp_pins[i], GPIO_MODE_OUTPUT_PP, GPIO_NOPULL);
    }
}

/* ------------------------------------------------------------ chip / power */

static void report_reset_cause(void)
{
    uint32_t csr = RCC->CSR;
    out("Reset cause:");
    if (csr & RCC_CSR_BORRSTF)  out(" BOR");
    if (csr & RCC_CSR_PORRSTF)  out(" POR");
    if (csr & RCC_CSR_PINRSTF)  out(" PIN(NRST)");
    if (csr & RCC_CSR_SFTRSTF)  out(" SOFTWARE");
    if (csr & RCC_CSR_IWDGRSTF) out(" IWDG");
    if (csr & RCC_CSR_WWDGRSTF) out(" WWDG");
    out("\r\n");
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

/* VDDA from the internal reference (VREFINT, factory-calibrated at 3.3 V).
 * Register-level so the HAL ADC module doesn't need enabling. */
static uint32_t measure_vdda_mv(void)
{
    const uint16_t cal = *(const uint16_t *)0x1FFF7A2AU;   /* VREFINT_CAL */
    uint32_t sum = 0;

    __HAL_RCC_ADC1_CLK_ENABLE();
    ADC->CCR  = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE; /* PCLK2/4 */
    ADC1->CR1 = 0;
    ADC1->CR2 = 0;
    ADC1->SMPR1 |= ADC_SMPR1_SMP17;        /* 480 cycles on channel 17 (VREFINT) */
    ADC1->SQR1 = 0;                        /* 1 conversion */
    ADC1->SQR3 = 17;
    ADC1->CR2 |= ADC_CR2_ADON;
    HAL_Delay(2);

    for (int i = 0; i < 16; i++) {
        ADC1->SR = 0;
        ADC1->CR2 |= ADC_CR2_SWSTART;
        uint32_t t0 = HAL_GetTick();
        while (!(ADC1->SR & ADC_SR_EOC))
            if (HAL_GetTick() - t0 > 5) { ADC1->CR2 = 0; return 0; }
        sum += ADC1->DR;
    }
    ADC1->CR2 = 0;
    sum /= 16;
    return sum ? (3300U * cal) / sum : 0;
}

static void test_chip(void)
{
    uint16_t flash_kb = *(const uint16_t *)FLASHSIZE_BASE;
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    out("Device ID 0x%03lX (0x421 = F446; may read 0 without a debugger)\r\n",
        (unsigned long)HAL_GetDEVID());
    out("Unique ID %08lX-%08lX-%08lX\r\n",
        (unsigned long)uid[2], (unsigned long)uid[1], (unsigned long)uid[0]);
    result("Flash size 512 KB (RET6)", flash_kb == 512, "wrong part fitted?");

    uint32_t mv = measure_vdda_mv();
    char name[40];
    snprintf(name, sizeof name, "VDDA = %lu mV", (unsigned long)mv);
    result(name, mv >= 3000 && mv <= 3600, "VDDA/VSSA not connected properly");
}

/* ----------------------------------------------------------------- clocks */

static void test_lse(void)
{
    RCC_OscInitTypeDef o = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    o.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    o.LSEState       = RCC_LSE_ON;
    o.PLL.PLLState   = RCC_PLL_NONE;
    out("Starting LSE (X1 32.768 kHz), up to 5 s...\r\n");
    int ok = (HAL_RCC_OscConfig(&o) == HAL_OK);
    if (!ok) {
        o.LSEState = RCC_LSE_OFF;
        HAL_RCC_OscConfig(&o);
    }
    result("LSE 32.768 kHz starts", ok, "check X1, C10/C11, PC14/PC15 solder");
}

/* Start HSE, run SYSCLK = 84 MHz from PLL(HSE). If the UART text after this
 * point is readable, the crystal frequency matches BOARD_HSE_HZ. */
static void test_hse(void)
{
    RCC_OscInitTypeDef o = {0};
    RCC_ClkInitTypeDef c = {0};

    out("Starting HSE (Y1 %lu Hz)...\r\n", (unsigned long)BOARD_HSE_HZ);
    o.OscillatorType  = RCC_OSCILLATORTYPE_HSE;
    o.HSEState        = RCC_HSE_ON;
    o.PLL.PLLState    = RCC_PLL_ON;
    o.PLL.PLLSource   = RCC_PLLSOURCE_HSE;
    o.PLL.PLLM        = BOARD_HSE_HZ / 1000000U;   /* 1 MHz into the VCO */
    o.PLL.PLLN        = 336;                       /* VCO 336 MHz */
    o.PLL.PLLP        = RCC_PLLP_DIV4;             /* SYSCLK 84 MHz */
    o.PLL.PLLQ        = 7;                         /* 48 MHz */
    o.PLL.PLLR        = 2;

    if (HAL_RCC_OscConfig(&o) != HAL_OK) {
        o.HSEState = RCC_HSE_OFF;
        o.PLL.PLLState = RCC_PLL_OFF;
        HAL_RCC_OscConfig(&o);
        result("HSE starts", 0, "check Y1, C12/C13, PH0/PH1 solder; staying on HSI");
        return;
    }
    result("HSE starts", 1, NULL);

    c.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    c.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    c.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    c.APB1CLKDivider = RCC_HCLK_DIV2;              /* 42 MHz (max 45) */
    c.APB2CLKDivider = RCC_HCLK_DIV1;              /* 84 MHz */
    if (HAL_RCC_ClockConfig(&c, FLASH_LATENCY_2) != HAL_OK) {
        result("Switch to 84 MHz PLL", 0, "PLL failed; staying on HSI");
        return;
    }
    /* baud-rate registers depend on the bus clock: recompute them */
    HAL_UART_Init(&huart1);
    HAL_UART_Init(&huart2);
    out("  Now on HSE+PLL, SYSCLK %lu Hz. If this line is readable,\r\n"
        "  Y1 really is %lu Hz.\r\n",
        (unsigned long)HAL_RCC_GetSysClockFreq(), (unsigned long)BOARD_HSE_HZ);
    result("Switch to 84 MHz PLL", 1, NULL);
}

/* -------------------------------------------------------------------- SPI */

/* Needs a jumper wire PA7 (MOSI) -> PA6 (MISO). CS1 stays high so any
 * device on the bus ignores the traffic. */
static void test_spi(void)
{
    uint8_t tx[8] = {0x55, 0xAA, 0x00, 0xFF, 0x12, 0x34, 0xA5, 0x5A};
    uint8_t rx[8] = {0};
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof tx, 100);
    int ok = (st == HAL_OK) && memcmp(tx, rx, sizeof tx) == 0;
    result("SPI1 loopback (MOSI->MISO)", ok, "needs jumper PA7->PA6; skip if not fitted");
    if (!ok)
        out("    got %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7]);
}

/* -------------------------------------------------------------------- CAN */

static HAL_StatusTypeDef can_setup(CAN_HandleTypeDef *h, uint32_t mode)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t nq, presc = 0;

    for (nq = 16; nq >= 8; nq--) {
        if (pclk1 % (CAN_BITRATE * nq) == 0) { presc = pclk1 / (CAN_BITRATE * nq); break; }
    }
    if (presc == 0 || presc > 1024) return HAL_ERROR;

    uint32_t bs2 = 2, bs1 = nq - 1 - bs2;          /* ~87 % sample point */
    HAL_CAN_Stop(h);
    h->Init.Prescaler            = presc;
    h->Init.Mode                 = mode;
    h->Init.SyncJumpWidth        = CAN_SJW_1TQ;
    h->Init.TimeSeg1             = (bs1 - 1U) << CAN_BTR_TS1_Pos;
    h->Init.TimeSeg2             = (bs2 - 1U) << CAN_BTR_TS2_Pos;
    h->Init.TimeTriggeredMode    = DISABLE;
    h->Init.AutoBusOff           = ENABLE;
    h->Init.AutoWakeUp           = DISABLE;
    h->Init.AutoRetransmission   = DISABLE;        /* one shot: no endless retries */
    h->Init.ReceiveFifoLocked    = DISABLE;
    h->Init.TransmitFifoPriority = DISABLE;
    return HAL_CAN_Init(h);
}

/* Accept everything. CAN1 uses filter bank 0, CAN2 bank 14 (filters live in CAN1). */
static void can_filters(void)
{
    CAN_FilterTypeDef f = {0};
    f.FilterMode           = CAN_FILTERMODE_IDMASK;
    f.FilterScale          = CAN_FILTERSCALE_32BIT;
    f.FilterFIFOAssignment = CAN_RX_FIFO0;
    f.FilterActivation     = ENABLE;
    f.SlaveStartFilterBank = 14;
    f.FilterBank = 0;
    HAL_CAN_ConfigFilter(&hcan1, &f);
    f.FilterBank = 14;
    HAL_CAN_ConfigFilter(&hcan2, &f);
}

static int can_send_recv(CAN_HandleTypeDef *tx, CAN_HandleTypeDef *rx, uint32_t id)
{
    CAN_TxHeaderTypeDef th = {0};
    CAN_RxHeaderTypeDef rh;
    uint8_t d[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, (uint8_t)id};
    uint8_t r[8];
    uint32_t mb;

    while (HAL_CAN_GetRxFifoFillLevel(rx, CAN_RX_FIFO0))
        HAL_CAN_GetRxMessage(rx, CAN_RX_FIFO0, &rh, r);

    th.StdId = id; th.IDE = CAN_ID_STD; th.RTR = CAN_RTR_DATA; th.DLC = 8;
    if (HAL_CAN_AddTxMessage(tx, &th, d, &mb) != HAL_OK) return 0;

    uint32_t t0 = HAL_GetTick();
    while (HAL_CAN_GetRxFifoFillLevel(rx, CAN_RX_FIFO0) == 0) {
        if (HAL_GetTick() - t0 > 50) { HAL_CAN_AbortTxRequest(tx, mb); return 0; }
    }
    if (HAL_CAN_GetRxMessage(rx, CAN_RX_FIFO0, &rh, r) != HAL_OK) return 0;
    return rh.StdId == id && rh.DLC == 8 && memcmp(d, r, 8) == 0;
}

static int can_start(CAN_HandleTypeDef *h, uint32_t mode)
{
    if (can_setup(h, mode) != HAL_OK) return 0;
    can_filters();
    return HAL_CAN_Start(h) == HAL_OK;
}

static void test_can(void)
{
    int ok;

    /* 1) Internal loopback: proves the peripheral, clocks and TX pin mux.
     *    Needs no transceiver and no bus. */
    ok = can_start(&hcan1, CAN_MODE_LOOPBACK) && can_send_recv(&hcan1, &hcan1, 0x101);
    result("CAN1 internal loopback", ok, "CAN1 pins must be PB8/PB9 in the .ioc");
    HAL_CAN_Stop(&hcan1);

    ok = can_start(&hcan2, CAN_MODE_LOOPBACK) && can_send_recv(&hcan2, &hcan2, 0x102);
    result("CAN2 internal loopback", ok, NULL);
    HAL_CAN_Stop(&hcan2);

    /* 2) Real bus: CAN1 <-> CAN2 through both transceivers. Wire CANH-CANH,
     *    CANL-CANL between the two ports with 120 ohm at each end (~60 ohm). */
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
    gpio_init();

    for (int i = 0; i < 6; i++) {                  /* LED works: 3 quick blinks */
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        HAL_Delay(100);
    }

    out("\r\n\r\n===== STM32F446 board bring-up test =====\r\n");
    out("(this text on both UART1/J1A and UART2/J2A means both TX paths work)\r\n");
    report_reset_cause();

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

    uint32_t ms = 0, blink = g_fail ? 100 : 500;
    for (;;) {
        uint32_t t0 = HAL_GetTick();
        ms++;
        for (unsigned i = 0; i < TP_COUNT; i++)
            if (ms % (i + 1) == 0) HAL_GPIO_TogglePin(TP_PORT, tp_pins[i]);
        if (ms % blink == 0) HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        echo(&huart1);
        echo(&huart2);
        while (HAL_GetTick() == t0) { }            /* 1 ms per iteration */
    }
}
