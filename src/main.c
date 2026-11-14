/*
 * Bare-metal LED blink for the SAM E51 (Cortex-M4F).
 *
 * - CPU runs from the internal DFLL48M (open loop, factory calibrated) via
 *   GCLK generator 0, no division -> 48 MHz.
 * - SysTick fires every 1 ms; delay_ms() waits on a tick counter.
 * - User LED0 on the SAM E51 Curiosity Nano (PA14, active-low) toggles
 *   every 500 ms.
 */
#include <stdint.h>
#include "sam.h"

#define CPU_HZ      48000000UL
#define LED_PIN     14U                 /* PA14 = LED0, active-low */
#define LED_MASK    (1UL << LED_PIN)
#define LED_PORT    (PORT_REGS->GROUP[0]) /* group 0 = port A */

static volatile uint32_t g_ticks;

/* We link with -nostartfiles (the DFP startup provides Reset_Handler), so
 * newlib's crti.o is absent. __libc_init_array() still calls _init(); on
 * Cortex-M constructors run from .init_array, so an empty hook suffices. */
void _init(void);
void _init(void)
{
}

void SysTick_Handler(void)
{
    g_ticks++;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = g_ticks;
    while ((uint32_t)(g_ticks - start) < ms) {
        __WFI();
    }
}

static void gclk0_set_source(uint32_t src)
{
    GCLK_REGS->GCLK_GENCTRL[0] = src | GCLK_GENCTRL_DIV(1) | GCLK_GENCTRL_GENEN_Msk;
    while (GCLK_REGS->GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL_GCLK0) {
    }
}

static void clock_init_48mhz(void)
{
    /* Flash wait states: fixed, conservative value for 48 MHz across the
     * full VDD range (instead of relying on AUTOWS). */
    NVMCTRL_REGS->NVMCTRL_CTRLA =
        (NVMCTRL_REGS->NVMCTRL_CTRLA & ~(NVMCTRL_CTRLA_AUTOWS_Msk | NVMCTRL_CTRLA_RWS_Msk))
        | NVMCTRL_CTRLA_RWS(2);

    /* Park GCLK0 (CPU clock) on the always-on 32 kHz oscillator so the
     * DFLL can be safely stopped and reconfigured. */
    gclk0_set_source(GCLK_GENCTRL_SRC_OSCULP32K);

    /* DFLL48M: disable, select open-loop mode, re-enable. DFLLVAL keeps the
     * factory coarse/fine calibration loaded at reset (~48 MHz). */
    OSCCTRL_REGS->OSCCTRL_DFLLCTRLA = 0;
    while (OSCCTRL_REGS->OSCCTRL_DFLLSYNC & OSCCTRL_DFLLSYNC_ENABLE_Msk) {
    }
    OSCCTRL_REGS->OSCCTRL_DFLLCTRLB = 0; /* MODE=0: open loop */
    while (OSCCTRL_REGS->OSCCTRL_DFLLSYNC & OSCCTRL_DFLLSYNC_DFLLCTRLB_Msk) {
    }
    OSCCTRL_REGS->OSCCTRL_DFLLCTRLA = OSCCTRL_DFLLCTRLA_ENABLE_Msk;
    while (OSCCTRL_REGS->OSCCTRL_DFLLSYNC & OSCCTRL_DFLLSYNC_ENABLE_Msk) {
    }
    while (!(OSCCTRL_REGS->OSCCTRL_STATUS & OSCCTRL_STATUS_DFLLRDY_Msk)) {
    }

    /* CPU clock = GCLK0 / 1 */
    MCLK_REGS->MCLK_CPUDIV = MCLK_CPUDIV_DIV(1);

    /* Move GCLK0 back to the DFLL: 48 MHz main clock. */
    gclk0_set_source(GCLK_GENCTRL_SRC_DFLL);

    SystemCoreClock = CPU_HZ;
}

int main(void)
{
    clock_init_48mhz();

    /* 1 ms SysTick from the 48 MHz core clock. */
    SysTick_Config(CPU_HZ / 1000U);

    /* PA14 as push-pull output, start high (LED off, active-low).
     * PORT's APB clock is on at reset. */
    LED_PORT.PORT_OUTSET = LED_MASK;
    LED_PORT.PORT_DIRSET = LED_MASK;

    for (;;) {
        LED_PORT.PORT_OUTTGL = LED_MASK;
        delay_ms(500);
    }
}
