/**
 * @file bmp280.h
 * @brief BMP280 barometric pressure and temperature sensor driver.
 *
 * Interface: I²C (up to 400 kHz fast-mode).
 * I²C address: 0x76 (SDO=GND) or 0x77 (SDO=VCC).
 *
 * The BMP280 provides:
 *   - Pressure:    300–1100 hPa, ±1 hPa absolute accuracy
 *   - Temperature: -40–+85 °C,  ±1 °C accuracy
 *
 * Compensation: the sensor stores 12 factory-trimmed calibration coefficients
 * in OTP registers (0x88–0x9F).  Raw ADC values must be passed through the
 * Bosch-supplied compensation formulas (integer version used here to avoid
 * 64-bit float on Cortex-M4).
 *
 * Usage (on-target):
 *   Bmp280Dev dev;
 *   bmp280_init(&hi2c1, BMP280_ADDR_DEFAULT, &dev);
 *   float press_hpa, temp_c;
 *   bmp280_read(&dev, &press_hpa, &temp_c);
 *
 * Host-compilable when BMP280_HOST_BUILD is defined.
 */

#ifndef BMP280_H
#define BMP280_H

#include <stdint.h>

/* ── I²C addresses (7-bit) ───────────────────────────────────────────────── */
#define BMP280_ADDR_DEFAULT   0x76u   /**< SDO pin = GND */
#define BMP280_ADDR_ALT       0x77u   /**< SDO pin = VCC */

/* ── Register addresses ──────────────────────────────────────────────────── */
#define BMP280_REG_CALIB_START  0x88u  /**< First calibration word (dig_T1) */
#define BMP280_REG_ID           0xD0u  /**< Chip ID register                */
#define BMP280_REG_RESET        0xE0u  /**< Soft-reset register             */
#define BMP280_REG_STATUS       0xF3u  /**< Status register                 */
#define BMP280_REG_CTRL_MEAS    0xF4u  /**< osrs_t, osrs_p, mode            */
#define BMP280_REG_CONFIG       0xF5u  /**< t_sb, filter, spi3w_en          */
#define BMP280_REG_PRESS_MSB    0xF7u  /**< First of 6 data bytes           */

/* ── Chip ID ─────────────────────────────────────────────────────────────── */
#define BMP280_CHIP_ID          0x60u  /**< BMP280 chip ID */

/* ── ctrl_meas register bit fields ──────────────────────────────────────── */
/** Oversampling for temperature (osrs_t, bits [7:5]). */
#define BMP280_OSRS_T_X1   (0x01u << 5)  /**< ×1 oversampling */
#define BMP280_OSRS_T_X2   (0x02u << 5)
#define BMP280_OSRS_T_X4   (0x03u << 5)

/** Oversampling for pressure (osrs_p, bits [4:2]). */
#define BMP280_OSRS_P_X1   (0x01u << 2)  /**< ×1 oversampling */
#define BMP280_OSRS_P_X4   (0x03u << 2)
#define BMP280_OSRS_P_X16  (0x05u << 2)

/** Power mode (bits [1:0]). */
#define BMP280_MODE_SLEEP  0x00u
#define BMP280_MODE_FORCED 0x01u
#define BMP280_MODE_NORMAL 0x03u

/** IIR filter coefficient (config register bits [4:2]). */
#define BMP280_FILTER_OFF  (0x00u << 2)
#define BMP280_FILTER_X4   (0x02u << 2)
#define BMP280_FILTER_X16  (0x04u << 2)

/* ── Calibration data (12 coefficients, loaded from OTP at init) ─────────── */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} Bmp280Calib;

/* ── Device handle ───────────────────────────────────────────────────────── */
/**
 * Opaque device handle.  Initialised by bmp280_init(); passed to bmp280_read().
 * Caller allocates statically — no heap.
 */
typedef struct {
    Bmp280Calib calib;   /**< Factory calibration coefficients */
    uint8_t     addr7;   /**< 7-bit I²C address */
#ifndef BMP280_HOST_BUILD
    void       *hi2c;    /**< Pointer to I2C_HandleTypeDef (cast in .c) */
#endif
} Bmp280Dev;

/* ── Error codes ─────────────────────────────────────────────────────────── */
typedef enum {
    BMP280_OK         =  0,
    BMP280_ERR_I2C    = -1,   /**< HAL I²C error */
    BMP280_ERR_ID     = -2,   /**< Chip ID mismatch */
} Bmp280Status;

/* ── On-target API ───────────────────────────────────────────────────────── */
#ifndef BMP280_HOST_BUILD

#include "stm32g4xx_hal.h"

/**
 * Initialise the BMP280.
 *
 * Steps:
 *  1. Verify chip ID (0xD0 → 0x60).
 *  2. Read 24 bytes of calibration data from 0x88.
 *  3. Configure: normal mode, ×4 pressure oversampling, ×1 temperature
 *     oversampling, IIR filter ×4, standby 62.5 ms.
 *
 * @param hi2c   Pointer to initialised I²C handle.
 * @param addr7  7-bit I²C address (BMP280_ADDR_DEFAULT or _ALT).
 * @param dev    Pointer to caller-allocated Bmp280Dev handle.
 * @return BMP280_OK on success, negative error code on failure.
 */
Bmp280Status bmp280_init(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                          Bmp280Dev *dev);

/**
 * Read compensated pressure and temperature.
 *
 * Uses the Bosch integer compensation formulas from the BMP280 datasheet
 * (Section 4.2.3).  Both outputs are optional (pass NULL to skip).
 *
 * @param dev       Initialised device handle.
 * @param press_hpa Output: pressure in hPa (may be NULL).
 * @param temp_c    Output: temperature in °C (may be NULL).
 * @return BMP280_OK on success, BMP280_ERR_I2C on bus error.
 */
Bmp280Status bmp280_read(const Bmp280Dev *dev,
                          float *press_hpa, float *temp_c);

#endif /* BMP280_HOST_BUILD */
#endif /* BMP280_H */
