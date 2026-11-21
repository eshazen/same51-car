/*
 * Board-level helpers shared between main.c and the SD driver.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#define CPU_HZ 48000000UL

/* Milliseconds since SysTick was started (wraps after ~49 days). */
uint32_t millis(void);
void delay_ms(uint32_t ms);

#endif
