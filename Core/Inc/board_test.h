#ifndef BOARD_TEST_H
#define BOARD_TEST_H

/* Runs every bring-up test once, prints PASS/FAIL on USART1 and USART2
 * (115200 8N1), then loops forever: blinks the status LED, toggles the
 * test points and echoes anything you type. Never returns. */
void BoardTest_Run(void);

#endif
