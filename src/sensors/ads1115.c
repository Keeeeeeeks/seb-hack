/**
 * @file ads1115.c
 * @brief ADS1115 16-bit I²C ADC driver implementation.
 *
 * Register reference: Texas Instruments ADS1113/4/5 datasheet SBAS444D.
 *
 * Config register (16-bit, big-endian):
 *   Bit 15:    OS — operational status / single-shot start
 *   Bits 14:12 — MUX — input multiplexer
 *   Bits 11:9  — PGA — programmable gain amplifier
 *   Bit 8:     MODE — 0=continuous, 1=single-shot
 *   Bits 7:5   — DR — data rate
 *   Bit 4:     COMP_MODE — 0=traditional, 1=window
 *   Bit 3:     COMP_POL — comparator polarity
 *   Bit 2:     COMP_LAT — latching comparator
 *   Bits 1:0   — COMP_QUE — comparator queue / disable
 *
 * Conversion register (16-bit, big-endian, signed two's complement).
 */

#include "ads1115.h"

#ifndef ADS1115_HOST_BUILD

#include "stm32g4xx_hal.h"

#define I2C_TIMEOUT_MS      10u
/** Maximum polls waiting for conversion complete (OS bit = 1). */
#define CONV_POLL_MAX       200u
/** Delay between polls [ms]. */
#define CONV_POLL_DELAY_MS  1u

/* ── Internal helpers ────────────────────────────────────────────────────── */

/**
 * Write a 16-bit value to a register (big-endian).
 */
static Ads1115Status reg_write16(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                                  uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = {
        reg,
        (uint8_t)(val >> 8),
        (uint8_t)(val & 0xFFu)
    };
    if (HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr7 << 1),
                                 buf, 3, I2C_TIMEOUT_MS) != HAL_OK) {
        return ADS1115_ERR_I2C;
    }
    return ADS1115_OK;
}

/**
 * Set register pointer then read 2 bytes (big-endian).
 */
static Ads1115Status reg_read16(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                                 uint8_t reg, uint16_t *val_out)
{
    uint8_t buf[2];

    if (HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr7 << 1),
                                 &reg, 1, I2C_TIMEOUT_MS) != HAL_OK) {
        return ADS1115_ERR_I2C;
    }
    if (HAL_I2C_Master_Receive(hi2c, (uint16_t)((addr7 << 1) | 1u),
                                buf, 2, I2C_TIMEOUT_MS) != HAL_OK) {
        return ADS1115_ERR_I2C;
    }

    *val_out = ((uint16_t)buf[0] << 8) | buf[1];
    return ADS1115_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

Ads1115Status ads1115_init(I2C_HandleTypeDef *hi2c, uint8_t addr7)
{
    /* Write a known config (single-shot, AIN0-GND, ±2.048 V, 128 SPS,
     * comparator disabled) and verify the bus is responsive. */
    uint16_t cfg = (uint16_t)(ADS1115_MUX_AIN0_GND)
                 | (uint16_t)(ADS1115_PGA_2048MV)
                 | ADS1115_MODE_SINGLE
                 | (uint16_t)(ADS1115_DR_128SPS)
                 | ADS1115_COMP_QUE_DISABLE;

    return reg_write16(hi2c, addr7, ADS1115_REG_CONFIG, cfg);
}

Ads1115Status ads1115_read_channel(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                                    Ads1115Mux mux, Ads1115Pga pga,
                                    float *volts)
{
    /* Build config word: start single-shot conversion */
    uint16_t cfg = ADS1115_OS_START
                 | (uint16_t)mux
                 | (uint16_t)pga
                 | ADS1115_MODE_SINGLE
                 | (uint16_t)ADS1115_DR_128SPS
                 | ADS1115_COMP_QUE_DISABLE;

    Ads1115Status st = reg_write16(hi2c, addr7, ADS1115_REG_CONFIG, cfg);
    if (st != ADS1115_OK) return st;

    /* Poll OS bit until conversion is complete (OS=1 when idle/done) */
    uint16_t status_reg;
    uint32_t polls = 0u;
    do {
        HAL_Delay(CONV_POLL_DELAY_MS);
        st = reg_read16(hi2c, addr7, ADS1115_REG_CONFIG, &status_reg);
        if (st != ADS1115_OK) return st;
        polls++;
        if (polls >= CONV_POLL_MAX) return ADS1115_ERR_TIMEOUT;
    } while (!(status_reg & ADS1115_OS_START));  /* OS=1 means ready */

    /* Read conversion result */
    uint16_t raw_u;
    st = reg_read16(hi2c, addr7, ADS1115_REG_CONV, &raw_u);
    if (st != ADS1115_OK) return st;

    /* Convert signed 16-bit raw to voltage */
    int16_t raw = (int16_t)raw_u;
    float lsb_v = ads1115_lsb_uv(pga) / 1000000.0f;  /* µV → V */
    *volts = (float)raw * lsb_v;

    return ADS1115_OK;
}

#endif /* ADS1115_HOST_BUILD */
