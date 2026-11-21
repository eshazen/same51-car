/*
 * Minimal SD card driver, SPI mode, SERCOM1 on PA16/PA17/PA19, CS on PA08.
 *
 * CRC checking is switched on (CMD59) after init so every command and every
 * data block is CRC-protected; a wiring / signal-integrity problem shows up
 * as an error instead of silently corrupted data.
 */
#include <stddef.h>
#include "sam.h"
#include "board.h"
#include "sd_spi.h"

#define SD_INIT_HZ  400000UL    /* <= 400 kHz required during init   */
#define SD_FAST_HZ  8000000UL   /* after init; SD allows up to 25 MHz */

#define SPI         (SERCOM1_REGS->SPIM)
#define PORTA       (PORT_REGS->GROUP[0])

#define PIN_MOSI    16U
#define PIN_SCK     17U
#define PIN_MISO    19U
#define PIN_CS      8U
#define CS_MASK     (1UL << PIN_CS)

#define R1_IDLE     0x01U
#define R1_ILLEGAL  0x04U
#define TOKEN_START 0xFEU

/* ---------------------------------------------------------------- CRC -- */

static uint8_t crc7(const uint8_t *d, size_t n)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = d[i];
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if ((b ^ crc) & 0x80U) {
                crc ^= 0x09U;
            }
            b <<= 1;
        }
    }
    return crc & 0x7FU;
}

static uint16_t crc16(const uint8_t *d, size_t n)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ---------------------------------------------------------------- SPI -- */

static void spi_set_hz(uint32_t hz)
{
    /* Master: f_sck = f_ref / (2 * (BAUD + 1)); round so f_sck <= hz. */
    uint32_t baud = (CPU_HZ + 2U * hz - 1U) / (2U * hz) - 1U;
    if (baud > 255U) {
        baud = 255U;
    }
    SPI.SERCOM_CTRLA &= ~SERCOM_SPIM_CTRLA_ENABLE_Msk;
    while (SPI.SERCOM_SYNCBUSY & SERCOM_SPIM_SYNCBUSY_ENABLE_Msk) {
    }
    SPI.SERCOM_BAUD = (uint8_t)baud;
    SPI.SERCOM_CTRLA |= SERCOM_SPIM_CTRLA_ENABLE_Msk;
    while (SPI.SERCOM_SYNCBUSY & SERCOM_SPIM_SYNCBUSY_ENABLE_Msk) {
    }
}

static void spi_init(void)
{
    /* CS: GPIO output, idle high. */
    PORTA.PORT_OUTSET = CS_MASK;
    PORTA.PORT_DIRSET = CS_MASK;

    /* MOSI/SCK/MISO -> peripheral function C (SERCOM1). */
    PORTA.PORT_PMUX[PIN_MOSI / 2] = PORT_PMUX_PMUXE(MUX_PA16C_SERCOM1_PAD0)
                                  | PORT_PMUX_PMUXO(MUX_PA17C_SERCOM1_PAD1);
    PORTA.PORT_PMUX[PIN_MISO / 2] = (PORTA.PORT_PMUX[PIN_MISO / 2] & PORT_PMUX_PMUXE_Msk)
                                  | PORT_PMUX_PMUXO(MUX_PA19C_SERCOM1_PAD3);
    PORTA.PORT_PINCFG[PIN_MOSI] = PORT_PINCFG_PMUXEN_Msk;
    PORTA.PORT_PINCFG[PIN_SCK]  = PORT_PINCFG_PMUXEN_Msk;
    /* MISO gets the internal pull-up: the card's DO is open until it
     * drives it, and an SD socket without a pull-up would float. */
    PORTA.PORT_OUTSET = 1UL << PIN_MISO;
    PORTA.PORT_PINCFG[PIN_MISO] = PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_PULLEN_Msk;

    /* Clocks: APB bus clock + core GCLK (GCLK0, 48 MHz). */
    MCLK_REGS->MCLK_APBAMASK |= MCLK_APBAMASK_SERCOM1_Msk;
    GCLK_REGS->GCLK_PCHCTRL[SERCOM1_GCLK_ID_CORE] = GCLK_PCHCTRL_GEN_GCLK0 | GCLK_PCHCTRL_CHEN_Msk;
    while (!(GCLK_REGS->GCLK_PCHCTRL[SERCOM1_GCLK_ID_CORE] & GCLK_PCHCTRL_CHEN_Msk)) {
    }

    SPI.SERCOM_CTRLA = SERCOM_SPIM_CTRLA_SWRST_Msk;
    while (SPI.SERCOM_SYNCBUSY & SERCOM_SPIM_SYNCBUSY_SWRST_Msk) {
    }

    /* SPI master, mode 0 (CPOL=0, CPHA=0), MSB first, DO=PAD0 SCK=PAD1,
     * DI=PAD3, 8-bit characters, receiver on, hardware SS off. */
    SPI.SERCOM_CTRLA = SERCOM_SPIM_CTRLA_MODE_SPI_MASTER
                     | SERCOM_SPIM_CTRLA_DOPO(0)
                     | SERCOM_SPIM_CTRLA_DIPO(3);
    SPI.SERCOM_CTRLB = SERCOM_SPIM_CTRLB_RXEN_Msk;
    while (SPI.SERCOM_SYNCBUSY & SERCOM_SPIM_SYNCBUSY_CTRLB_Msk) {
    }

    spi_set_hz(SD_INIT_HZ);
}

static uint8_t spi_xfer(uint8_t out)
{
    while (!(SPI.SERCOM_INTFLAG & SERCOM_SPIM_INTFLAG_DRE_Msk)) {
    }
    SPI.SERCOM_DATA = out;
    while (!(SPI.SERCOM_INTFLAG & SERCOM_SPIM_INTFLAG_RXC_Msk)) {
    }
    return (uint8_t)SPI.SERCOM_DATA;
}

static void cs_low(void)
{
    PORTA.PORT_OUTCLR = CS_MASK;
}

/* Raise CS, then clock one more byte so the card releases DO. */
static void deselect(void)
{
    PORTA.PORT_OUTSET = CS_MASK;
    spi_xfer(0xFF);
}

/* ----------------------------------------------------------------- SD -- */

/* Wait until the card stops signalling busy (DO held low). */
static int wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = millis();
    do {
        if (spi_xfer(0xFF) == 0xFF) {
            return 1;
        }
    } while ((uint32_t)(millis() - t0) < timeout_ms);
    return 0;
}

/* Select the card and send a command; returns R1 (0xFF on timeout). The card
 * is left selected so the caller can read trailing response bytes / data. */
static uint8_t sd_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t frame[6] = {
        (uint8_t)(0x40U | cmd),
        (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
        (uint8_t)(arg >> 8),  (uint8_t)arg,
        0,
    };
    frame[5] = (uint8_t)((crc7(frame, 5) << 1) | 1U);

    cs_low();
    if (cmd != 0 && !wait_ready(500)) {
        return 0xFF;
    }
    for (int i = 0; i < 6; i++) {
        spi_xfer(frame[i]);
    }

    /* Response arrives within NCR = 1..8 bytes. */
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 10; i++) {
        r1 = spi_xfer(0xFF);
        if (!(r1 & 0x80U)) {
            break;
        }
    }
    return r1;
}

static uint8_t sd_acmd(uint8_t cmd, uint32_t arg)
{
    uint8_t r1 = sd_cmd(55, 0);
    deselect();
    if (r1 > R1_IDLE) {
        return r1;
    }
    return sd_cmd(cmd, arg);
}

/* Read a data block of 'len' bytes after a command; checks CRC16. */
static int read_data(sd_card_t *card, uint8_t *buf, size_t len)
{
    uint32_t t0 = millis();
    uint8_t tok;
    do {
        tok = spi_xfer(0xFF);
    } while (tok == 0xFF && (uint32_t)(millis() - t0) < 200U);
    card->last_token = tok;
    if (tok != TOKEN_START) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        buf[i] = spi_xfer(0xFF);
    }
    card->crc_rx = (uint16_t)(spi_xfer(0xFF) << 8);
    card->crc_rx |= spi_xfer(0xFF);
    card->crc_calc = crc16(buf, len);
    return card->crc_rx == card->crc_calc;
}

static sd_status_t read_reg(sd_card_t *card, uint8_t cmd, uint8_t *reg)
{
    uint8_t r1 = sd_cmd(cmd, 0);
    card->last_r1 = r1;
    int ok = (r1 == 0) && read_data(card, reg, 16);
    deselect();
    return ok ? SD_OK : (cmd == 9 ? SD_ERR_CSD : SD_ERR_CID);
}

static uint32_t csd_blocks(const uint8_t *csd)
{
    switch (csd[0] >> 6) {
    case 0: { /* CSD v1.0 (SDSC) */
        uint32_t c_size = ((uint32_t)(csd[6] & 0x03U) << 10)
                        | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        uint32_t mult = ((csd[9] & 0x03U) << 1) | (csd[10] >> 7);
        uint32_t bl_len = csd[5] & 0x0FU;
        return (c_size + 1U) << (mult + 2U + bl_len - 9U);
    }
    case 1: { /* CSD v2.0 (SDHC/SDXC) */
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3FU) << 16)
                        | ((uint32_t)csd[8] << 8) | csd[9];
        return (c_size + 1U) * 1024U;
    }
    default:
        return 0;
    }
}

sd_status_t sd_init(sd_card_t *card)
{
    uint8_t r1;
    uint8_t buf[4];

    *card = (sd_card_t){ .last_r1 = 0xFF, .last_token = 0xFF };
    spi_init();

    /* Power-up: >= 1 ms, then >= 74 clocks with CS and DI high. */
    delay_ms(10);
    PORTA.PORT_OUTSET = CS_MASK;
    for (int i = 0; i < 10; i++) {
        spi_xfer(0xFF);
    }

    /* CMD0: GO_IDLE_STATE -> enter SPI mode. */
    for (int tries = 0; ; tries++) {
        r1 = sd_cmd(0, 0);
        deselect();
        if (r1 == R1_IDLE) {
            break;
        }
        if (tries >= 10) {
            card->last_r1 = r1;
            return SD_ERR_CMD0;
        }
        delay_ms(10);
    }

    /* CMD8: SEND_IF_COND, 2.7-3.6 V, check pattern 0xAA. */
    r1 = sd_cmd(8, 0x1AAU);
    card->last_r1 = r1;
    uint32_t acmd41_arg;
    if (r1 == R1_IDLE) {
        for (int i = 0; i < 4; i++) {
            buf[i] = spi_xfer(0xFF);
        }
        deselect();
        if ((buf[2] & 0x0FU) != 0x01U || buf[3] != 0xAAU) {
            return SD_ERR_CMD8;
        }
        card->type = SD_TYPE_V2_SC;
        acmd41_arg = 1UL << 30;          /* HCS: we support SDHC/SDXC */
    } else if (r1 & R1_ILLEGAL) {
        deselect();
        card->type = SD_TYPE_V1;
        acmd41_arg = 0;
    } else {
        deselect();
        return SD_ERR_CMD8;
    }

    /* ACMD41: SD_SEND_OP_COND until the card leaves idle (<= 1 s). */
    uint32_t t0 = millis();
    do {
        r1 = sd_acmd(41, acmd41_arg);
        deselect();
    } while (r1 == R1_IDLE && (uint32_t)(millis() - t0) < 1000U);
    card->last_r1 = r1;
    if (r1 != 0) {
        return SD_ERR_ACMD41;
    }

    /* CMD58: READ_OCR. CCS (bit 30) = block addressing. */
    r1 = sd_cmd(58, 0);
    card->last_r1 = r1;
    if (r1 != 0) {
        deselect();
        return SD_ERR_CMD58;
    }
    for (int i = 0; i < 4; i++) {
        buf[i] = spi_xfer(0xFF);
    }
    deselect();
    card->ocr = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
              | ((uint32_t)buf[2] << 8) | buf[3];
    if (card->type == SD_TYPE_V2_SC && (card->ocr & (1UL << 30))) {
        card->type = SD_TYPE_V2_HC;
    }

    /* CMD59: CRC_ON_OFF -> on. */
    r1 = sd_cmd(59, 1);
    deselect();
    card->last_r1 = r1;
    if (r1 != 0) {
        return SD_ERR_CMD59;
    }

    /* CMD16: SET_BLOCKLEN 512 (fixed and ignored on SDHC/SDXC). */
    if (card->type != SD_TYPE_V2_HC) {
        r1 = sd_cmd(16, SD_BLOCK_SIZE);
        deselect();
        card->last_r1 = r1;
        if (r1 != 0) {
            return SD_ERR_CMD16;
        }
    }

    spi_set_hz(SD_FAST_HZ);

    sd_status_t st = read_reg(card, 9, card->csd);   /* CMD9: SEND_CSD */
    if (st != SD_OK) {
        return st;
    }
    card->blocks = csd_blocks(card->csd);
    return read_reg(card, 10, card->cid);            /* CMD10: SEND_CID */
}

sd_status_t sd_read_block(sd_card_t *card, uint32_t lba, uint8_t *buf)
{
    uint32_t addr = (card->type == SD_TYPE_V2_HC) ? lba : lba * SD_BLOCK_SIZE;
    uint8_t r1 = sd_cmd(17, addr);                   /* READ_SINGLE_BLOCK */
    card->last_r1 = r1;
    int ok = (r1 == 0) && read_data(card, buf, SD_BLOCK_SIZE);
    deselect();
    return ok ? SD_OK : SD_ERR_READ;
}
