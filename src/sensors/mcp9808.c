/**
 * @file mcp9808.c
 * @brief MCP9808 temperature sensor driver implementation.
 *
 * Register reference: Microchip MCP9808 datasheet DS20005095B.
 *
 * Temperature register (0x05) format (16-bit, big-endian):
 *   Bit 15:13 — TCRIT/TUPPER/TLOWER alert flags (read-only, ignored)
 *   Bit 12    — Sign (1 = negative)
 *   Bits 11:0 — Magnitude in 1/16 °C steps (0.0625 °C LSB)
 *
 * Positive: T = (raw & 0x0FFF) * 0.0625
 * Negative: T = (raw & 0x0FFF) * 0.0625 - 256.0
 */

#include "mcp9808.h"

#ifndef MCP9808_HOST_BUILD

#define I2C_TIMEOUT_MS  10u

/**
 * Write a register pointer (1 byte) then read 2 bytes (big-endian).
 */
static Mcp9808Status reg_read16(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                                 uint8_t reg, uint16_t *val_out)
{
    uint8_t buf[2];

    if (HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr7 << 1),
                                 &reg, 1, I2C_TIMEOUT_MS) != HAL_OK) {
        return MCP9808_ERR_I2C;
    }
    if (HAL_I2C_Master_Receive(hi2c, (uint16_t)((addr7 << 1) | 1u),
                                buf, 2, I2C_TIMEOUT_MS) != HAL_OK) {
        return MCP9808_ERR_I2C;
    }

    *val_out = ((uint16_t)buf[0] << 8) | buf[1];
    return MCP9808_OK;
}

/**
 * Write a register pointer + 1 data byte.
 */
static Mcp9808Status reg_write8(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                                 uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    if (HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr7 << 1),
                                 buf, 2, I2C_TIMEOUT_MS) != HAL_OK) {
        return MCP9808_ERR_I2C;
    }
    return MCP9808_OK;
}

Mcp9808Status mcp9808_init(I2C_HandleTypeDef *hi2c, uint8_t addr7)
{
    Mcp9808Status st;
    uint16_t id;

    /* 1. Verify Manufacturer ID */
    st = reg_read16(hi2c, addr7, MCP9808_REG_MFRID, &id);
    if (st != MCP9808_OK) return st;
    if (id != MCP9808_MFRID_VAL) return MCP9808_ERR_ID;

    /* 2. Set resolution to 0.0625 °C (maximum, 250 ms conversion time) */
    st = reg_write8(hi2c, addr7, MCP9808_REG_RESOLUTION,
                    (uint8_t)MCP9808_RES_0_0625C);
    if (st != MCP9808_OK) return st;

    /* 3. CONFIG register: leave at power-on default (0x0000).
     *    Continuous conversion is the default; SHDN bit (bit 8) = 0. */

    return MCP9808_OK;
}

Mcp9808Status mcp9808_read_temp(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                                 float *temp_c)
{
    uint16_t raw;
    Mcp9808Status st = reg_read16(hi2c, addr7, MCP9808_REG_TAMBIENT, &raw);
    if (st != MCP9808_OK) return st;

    /* Strip alert flag bits [15:13] */
    uint16_t magnitude = raw & 0x1FFFu;

    if (magnitude & 0x1000u) {
        /* Negative temperature: two's complement in 13-bit field */
        *temp_c = (float)(int16_t)(magnitude | 0xE000u) * 0.0625f;
    } else {
        /* Positive temperature */
        *temp_c = (float)magnitude * 0.0625f;
    }

    return MCP9808_OK;
}

#endif /* MCP9808_HOST_BUILD */
