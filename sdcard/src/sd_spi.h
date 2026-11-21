/*
 * Minimal SD / SDHC / SDXC driver, SPI mode, on SERCOM1.
 *
 *   MOSI  PA16  SERCOM1 PAD0  (function C)
 *   SCK   PA17  SERCOM1 PAD1  (function C)
 *   MISO  PA19  SERCOM1 PAD3  (function C)
 *   CS    PA08  GPIO, active-low
 *
 * There is no card-detect line, so a card is assumed to be present.
 */
#ifndef SD_SPI_H
#define SD_SPI_H

#include <stdint.h>

#define SD_BLOCK_SIZE 512U

typedef enum {
    SD_OK = 0,
    SD_ERR_CMD0,        /* 1: no idle response: wiring, power, CS, no card */
    SD_ERR_CMD8,        /* 2: CMD8 echo/voltage mismatch                   */
    SD_ERR_ACMD41,      /* 3: card never left idle state                   */
    SD_ERR_CMD58,       /* 4: reading OCR failed                           */
    SD_ERR_CMD59,       /* 5: enabling CRC checking failed                 */
    SD_ERR_CMD16,       /* 6: setting 512-byte block length failed         */
    SD_ERR_CSD,         /* 7: reading CSD failed (incl. CRC error)         */
    SD_ERR_CID,         /* 8: reading CID failed (incl. CRC error)         */
    SD_ERR_READ,        /* 9: reading a data block failed (incl. CRC)      */
} sd_status_t;

typedef enum {
    SD_TYPE_UNKNOWN = 0,
    SD_TYPE_V1,         /* SD v1.x, byte addressed          */
    SD_TYPE_V2_SC,      /* SD v2+ standard capacity, byte   */
    SD_TYPE_V2_HC,      /* SDHC / SDXC, block addressed     */
} sd_type_t;

typedef struct {
    sd_type_t type;
    uint32_t  ocr;
    uint8_t   cid[16];
    uint8_t   csd[16];
    uint32_t  blocks;       /* capacity in 512-byte blocks       */
    /* Diagnostics from the most recent failure. */
    uint8_t   last_r1;      /* last R1 response (0xFF = none)     */
    uint8_t   last_token;   /* last data token / error token      */
    uint16_t  crc_rx;       /* data CRC16 sent by the card        */
    uint16_t  crc_calc;     /* data CRC16 computed locally        */
} sd_card_t;

/* Bring up SERCOM1 + pins, then run the SPI-mode init sequence. On success
 * the SPI clock is raised to SD_FAST_HZ and card->type/ocr/cid/csd/blocks
 * are filled in. */
sd_status_t sd_init(sd_card_t *card);

/* Read one 512-byte block. 'lba' is always a block number. */
sd_status_t sd_read_block(sd_card_t *card, uint32_t lba, uint8_t *buf);

#endif
