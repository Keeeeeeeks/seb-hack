/**
 * @file mcp9808.h
 * @brief MCP9808 high-accuracy temperature sensor driver.
 *
 * Interface: I²C (up to 400 kHz fast-mode).
 * I²C address: 0x18–0x1F (A2:A0 pins select lower 3 bits).
 *
 * Resolution: 0.0625 °C (13-bit signed, two's complement in upper 13 bits
 * of the 16-bit ambient temperature register).
 *
 * Usage (on-target):
 *   mcp9808_init(&hi2c1, MCP9808_ADDR_DEFAULT);
 *   float temp_c;
 *   mcp9808_read_temp(&hi2c1, MCP9808_ADDR_DEFAULT, &temp_c);
 *
 * Host-compilable when MCP9808_HOST_BUILD is defined (HAL calls omitted).
 */

#ifndef MCP9808_H
#define MCP9808_H

#include <stdint.h>

/* ── I²C address (7-bit, A2:A0 = 000) ───────────────────────────────────── */
#define MCP9808_ADDR_DEFAULT   0x18u

/* ── Register addresses ──────────────────────────────────────────────────── */
#define MCP9808_REG_CONFIG     0x01u   /**< Configuration register          */
#define MCP9808_REG_TUPPER     0x02u   /**< Alert temperature upper boundary */
#define MCP9808_REG_TLOWER     0x03u   /**< Alert temperature lower boundary */
#define MCP9808_REG_TCRIT      0x04u   /**< Critical temperature register   */
#define MCP9808_REG_TAMBIENT   0x05u   /**< Ambient temperature register    */
#define MCP9808_REG_MFRID      0x06u   /**< Manufacturer ID (0x0054)        */
#define MCP9808_REG_DEVID      0x07u   /**< Device ID / revision            */
#define MCP9808_REG_RESOLUTION 0x08u   /**< Resolution register             */

/* ── Expected ID values ──────────────────────────────────────────────────── */
#define MCP9808_MFRID_VAL      0x0054u  /**< Microchip manufacturer ID */
#define MCP9808_DEVID_VAL      0x0400u  /**< Device ID (upper byte 0x04) */

/* ── Resolution settings ─────────────────────────────────────────────────── */
typedef enum {
    MCP9808_RES_0_5C    = 0x00,  /**< ±0.5 °C,   30 ms conversion  */
    MCP9808_RES_0_25C   = 0x01,  /**< ±0.25 °C,  65 ms conversion  */
    MCP9808_RES_0_125C  = 0x02,  /**< ±0.125 °C, 130 ms conversion */
    MCP9808_RES_0_0625C = 0x03,  /**< ±0.0625 °C, 250 ms conversion (default) */
} Mcp9808Resolution;

/* ── Error codes ─────────────────────────────────────────────────────────── */
typedef enum {
    MCP9808_OK         =  0,
    MCP9808_ERR_I2C    = -1,   /**< HAL I²C error */
    MCP9808_ERR_ID     = -2,   /**< Manufacturer/device ID mismatch */
} Mcp9808Status;

/* ── On-target API ───────────────────────────────────────────────────────── */
#ifndef MCP9808_HOST_BUILD

#include "stm32g4xx_hal.h"

/**
 * Initialise the MCP9808.
 *
 * Steps:
 *  1. Read and verify Manufacturer ID register (0x06 → 0x0054).
 *  2. Set resolution to 0.0625 °C (register 0x08 = 0x03).
 *  3. Ensure continuous conversion mode (CONFIG bit 8 = 0, default).
 *
 * @param hi2c   Pointer to initialised I²C handle.
 * @param addr7  7-bit I²C address (0x18–0x1F).
 * @return MCP9808_OK on success, negative error code on failure.
 */
Mcp9808Status mcp9808_init(I2C_HandleTypeDef *hi2c, uint8_t addr7);

/**
 * Read the ambient temperature.
 *
 * Reads the 16-bit ambient temperature register (0x05) and converts
 * the 13-bit two's-complement value to degrees Celsius.
 *
 * Conversion (per datasheet Section 5.1.3):
 *   - Bits [15:13]: alert flags (ignored here).
 *   - Bits [12]:    sign bit.
 *   - Bits [11:0]:  magnitude, LSB = 0.0625 °C.
 *
 * @param hi2c    Pointer to initialised I²C handle.
 * @param addr7   7-bit I²C address.
 * @param temp_c  Output: temperature in °C.
 * @return MCP9808_OK on success, MCP9808_ERR_I2C on bus error.
 */
Mcp9808Status mcp9808_read_temp(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                                 float *temp_c);

#endif /* MCP9808_HOST_BUILD */
#endif /* MCP9808_H */
