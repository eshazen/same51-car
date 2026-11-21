/*
 * Bare-metal microSD (SPI mode) interface test for the SAM E51 Curiosity Nano.
 *
 * Clocking is the same as the blink project: DFLL48M (open loop) -> GCLK0 ->
 * 48 MHz CPU, 1 ms SysTick.
 *
 * Test sequence: SD init (CMD0/8/ACMD41/58/59/16), read CSD and CID, then
 * read blocks 0..TEST_BLOCKS-1 with CRC16 checking. Results are reported on
 * user LED0 (PA14, active-low):
 *
 *   PASS: slow steady blink, 1 s period.
 *   FAIL: N short flashes, a 1.5 s pause, repeated. N is the sd_status_t
 *         value (see sd_spi.h); e.g. 1 flash = no response to CMD0.
 *
 * Everything gathered is also left in the global 'g_test' for inspection
 * with a debugger (e.g. `p/x g_test` in gdb).
 */
#include <stdint.h>
#include "sam.h"
#include "board.h"
#include "sd_spi.h"

#define LED_PIN     14U                 /* PA14 = LED0, active-low */
#define LED_MASK    (1UL << LED_PIN)
#define LED_PORT    (PORT_REGS->GROUP[0])

#define TEST_BLOCKS 64U                 /* blocks read back-to-back */

static volatile uint32_t g_ticks;

struct sd_test {
    sd_status_t status;
    sd_card_t   card;
    uint32_t    capacity_mb;
    uint32_t    blocks_read;            /* blocks that read back with good CRC */
    uint32_t    read_ms;                /* time for TEST_BLOCKS reads          */
    uint8_t     mbr_signature_ok;       /* block 0 ends in 0x55 0xAA           */
    uint8_t     block0[SD_BLOCK_SIZE];
};

struct sd_test g_test;
static uint8_t s_buf[SD_BLOCK_SIZE];

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

uint32_t millis(void)
{
    return g_ticks;
}

void delay_ms(uint32_t ms)
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
    /* Flash wait states: fixed, conservative value for 48 MHz. */
    NVMCTRL_REGS->NVMCTRL_CTRLA =
        (NVMCTRL_REGS->NVMCTRL_CTRLA & ~(NVMCTRL_CTRLA_AUTOWS_Msk | NVMCTRL_CTRLA_RWS_Msk))
        | NVMCTRL_CTRLA_RWS(2);

    /* Park GCLK0 on OSCULP32K so the DFLL can be stopped and reconfigured. */
    gclk0_set_source(GCLK_GENCTRL_SRC_OSCULP32K);

    /* DFLL48M: disable, open-loop mode, re-enable (factory calibration). */
    OSCCTRL_REGS->OSCCTRL_DFLLCTRLA = 0;
    while (OSCCTRL_REGS->OSCCTRL_DFLLSYNC & OSCCTRL_DFLLSYNC_ENABLE_Msk) {
    }
    OSCCTRL_REGS->OSCCTRL_DFLLCTRLB = 0;
    while (OSCCTRL_REGS->OSCCTRL_DFLLSYNC & OSCCTRL_DFLLSYNC_DFLLCTRLB_Msk) {
    }
    OSCCTRL_REGS->OSCCTRL_DFLLCTRLA = OSCCTRL_DFLLCTRLA_ENABLE_Msk;
    while (OSCCTRL_REGS->OSCCTRL_DFLLSYNC & OSCCTRL_DFLLSYNC_ENABLE_Msk) {
    }
    while (!(OSCCTRL_REGS->OSCCTRL_STATUS & OSCCTRL_STATUS_DFLLRDY_Msk)) {
    }

    MCLK_REGS->MCLK_CPUDIV = MCLK_CPUDIV_DIV(1);
    gclk0_set_source(GCLK_GENCTRL_SRC_DFLL);

    SystemCoreClock = CPU_HZ;
}

static void led(int on)
{
    if (on) {
        LED_PORT.PORT_OUTCLR = LED_MASK;
    } else {
        LED_PORT.PORT_OUTSET = LED_MASK;
    }
}

static sd_status_t run_test(void)
{
    sd_card_t *card = &g_test.card;

    sd_status_t st = sd_init(card);
    if (st != SD_OK) {
        return st;
    }
    g_test.capacity_mb = card->blocks / 2048U;

    uint32_t t0 = millis();
    for (uint32_t lba = 0; lba < TEST_BLOCKS; lba++) {
        st = sd_read_block(card, lba, lba == 0 ? g_test.block0 : s_buf);
        if (st != SD_OK) {
            return st;
        }
        g_test.blocks_read++;
    }
    g_test.read_ms = millis() - t0;
    g_test.mbr_signature_ok = g_test.block0[510] == 0x55U && g_test.block0[511] == 0xAAU;
    return SD_OK;
}

int main(void)
{
    clock_init_48mhz();
    SysTick_Config(CPU_HZ / 1000U);

    LED_PORT.PORT_OUTSET = LED_MASK;    /* LED off */
    LED_PORT.PORT_DIRSET = LED_MASK;

    g_test.status = run_test();

    for (;;) {
        if (g_test.status == SD_OK) {
            led(1);
            delay_ms(500);
            led(0);
            delay_ms(500);
        } else {
            for (unsigned i = 0; i < (unsigned)g_test.status; i++) {
                led(1);
                delay_ms(150);
                led(0);
                delay_ms(250);
            }
            delay_ms(1500);
        }
    }
}
