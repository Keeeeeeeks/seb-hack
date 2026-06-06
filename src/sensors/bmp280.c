/**
 * @file bmp280.c
 * @brief BMP280 barometric pressure and temperature sensor driver implementation.
 *
 * Compensation formulas: Bosch BMP280 datasheet BST-BMP280-DS001-19,
 * Section 4.2.3 "Compensation formulas in double precision floating point".
 * Adapted here to single-precision float (sufficient for ±1 hPa accuracy).
 *
 * Calibration register layout (0x88–0x9F, little-endian):
 *   0x88/89: dig_T1 (uint16)
 *   0x8A/8B: dig_T2 (int16)
 *   0x8C/8D: dig_T3 (int16)
 *   0x8E/8F: dig_P1 (uint16)
 *   0x90/91: dig_P2 (int16)
 *   ... (P3–P9 follow in order)
 *
 * Raw data register layout (0xF7–0xFC, 20-bit values):
 *   0xF7: press_msb[7:0]
 *   0xF8: press_lsb[7:0]
 *   0xF9: press_xlsb[7:4] (bits [3:0] unused)
 *   0xFA: temp_msb[7:0]
 *   0xFB: temp_lsb[7:0]
 *   0xFC: temp_xlsb[7:4] (bits [3:0] unused)
 */

#include "bmp280.h"

#ifndef BMP280_HOST_BUILD

#include "stm32g4xx_hal.h"

#define I2C_TIMEOUT_MS  10u

/* ── Internal helpers ────────────────────────────────────────────────────── */

static Bmp280Status reg_write(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                               uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    if (HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr7 << 1),
                                 buf, 2, I2C_TIMEOUT_MS) != HAL_OK) {
        return BMP280_ERR_I2C;
    }
    return BMP280_OK;
}

static Bmp280Status reg_read(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                              uint8_t reg, uint8_t *dst, uint16_t len)
{
    if (HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr7 << 1),
                                 &reg, 1, I2C_TIMEOUT_MS) != HAL_OK) {
        return BMP280_ERR_I2C;
    }
    if (HAL_I2C_Master_Receive(hi2c, (uint16_t)((addr7 << 1) | 1u),
                                dst, len, I2C_TIMEOUT_MS) != HAL_OK) {
        return BMP280_ERR_I2C;
    }
    return BMP280_OK;
}

/**
 * Parse 24 calibration bytes (little-endian) into Bmp280Calib.
 */
static void parse_calib(const uint8_t *raw, Bmp280Calib *c)
{
    c->dig_T1 = (uint16_t)((uint16_t)raw[1]  << 8 | raw[0]);
    c->dig_T2 = (int16_t) ((uint16_t)raw[3]  << 8 | raw[2]);
    c->dig_T3 = (int16_t) ((uint16_t)raw[5]  << 8 | raw[4]);
    c->dig_P1 = (uint16_t)((uint16_t)raw[7]  << 8 | raw[6]);
    c->dig_P2 = (int16_t) ((uint16_t)raw[9]  << 8 | raw[8]);
    c->dig_P3 = (int16_t) ((uint16_t)raw[11] << 8 | raw[10]);
    c->dig_P4 = (int16_t) ((uint16_t)raw[13] << 8 | raw[12]);
    c->dig_P5 = (int16_t) ((uint16_t)raw[15] << 8 | raw[14]);
    c->dig_P6 = (int16_t) ((uint16_t)raw[17] << 8 | raw[16]);
    c->dig_P7 = (int16_t) ((uint16_t)raw[19] << 8 | raw[18]);
    c->dig_P8 = (int16_t) ((uint16_t)raw[21] << 8 | raw[20]);
    c->dig_P9 = (int16_t) ((uint16_t)raw[23] << 8 | raw[22]);
}

/* ── Bosch compensation formulas (single-precision float) ────────────────── */

/**
 * Compensate raw temperature ADC value.
 * Returns temperature in °C and sets *t_fine (used by pressure compensation).
 */
static float compensate_temp(int32_t adc_T, const Bmp280Calib *c,
                              int32_t *t_fine)
{
    float var1 = ((float)adc_T / 16384.0f - (float)c->dig_T1 / 1024.0f)
                 * (float)c->dig_T2;
    float var2 = ((float)adc_T / 131072.0f - (float)c->dig_T1 / 8192.0f)
                 * ((float)adc_T / 131072.0f - (float)c->dig_T1 / 8192.0f)
                 * (float)c->dig_T3;
    *t_fine = (int32_t)(var1 + var2);
    return (var1 + var2) / 5120.0f;
}

/**
 * Compensate raw pressure ADC value.
 * Requires t_fine from compensate_temp().
 * Returns pressure in hPa.
 */
static float compensate_press(int32_t adc_P, const Bmp280Calib *c,
                               int32_t t_fine)
{
    float var1 = (float)t_fine / 2.0f - 64000.0f;
    float var2 = var1 * var1 * (float)c->dig_P6 / 32768.0f;
    var2 = var2 + var1 * (float)c->dig_P5 * 2.0f;
    var2 = var2 / 4.0f + (float)c->dig_P4 * 65536.0f;
    var1 = ((float)c->dig_P3 * var1 * var1 / 524288.0f
            + (float)c->dig_P2 * var1) / 524288.0f;
    var1 = (1.0f + var1 / 32768.0f) * (float)c->dig_P1;

    if (var1 == 0.0f) {
        return 0.0f;   /* Avoid division by zero */
    }

    float p = 1048576.0f - (float)adc_P;
    p = (p - var2 / 4096.0f) * 6250.0f / var1;
    var1 = (float)c->dig_P9 * p * p / 2147483648.0f;
    var2 = p * (float)c->dig_P8 / 32768.0f;
    p = p + (var1 + var2 + (float)c->dig_P7) / 16.0f;

    return p / 100.0f;   /* Pa → hPa */
}

/* ── Public API ──────────────────────────────────────────────────────────── */

Bmp280Status bmp280_init(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                          Bmp280Dev *dev)
{
    Bmp280Status st;
    uint8_t id;

    dev->addr7 = addr7;
    dev->hi2c  = hi2c;

    /* 1. Verify chip ID */
    st = reg_read(hi2c, addr7, BMP280_REG_ID, &id, 1);
    if (st != BMP280_OK) return st;
    if (id != BMP280_CHIP_ID) return BMP280_ERR_ID;

    /* 2. Read 24 calibration bytes */
    uint8_t calib_raw[24];
    st = reg_read(hi2c, addr7, BMP280_REG_CALIB_START, calib_raw, 24);
    if (st != BMP280_OK) return st;
    parse_calib(calib_raw, &dev->calib);

    /* 3. Configure:
     *    ctrl_meas: osrs_t=×1, osrs_p=×4, mode=normal
     *    config:    t_sb=62.5 ms (bits[7:5]=001), filter=×4 (bits[4:2]=010)
     */
    st = reg_write(hi2c, addr7, BMP280_REG_CONFIG,
                   (0x01u << 5) | BMP280_FILTER_X4);
    if (st != BMP280_OK) return st;

    st = reg_write(hi2c, addr7, BMP280_REG_CTRL_MEAS,
                   BMP280_OSRS_T_X1 | BMP280_OSRS_P_X4 | BMP280_MODE_NORMAL);
    if (st != BMP280_OK) return st;

    return BMP280_OK;
}

Bmp280Status bmp280_read(const Bmp280Dev *dev,
                          float *press_hpa, float *temp_c)
{
    I2C_HandleTypeDef *hi2c = (I2C_HandleTypeDef *)dev->hi2c;
    uint8_t raw[6];

    Bmp280Status st = reg_read(hi2c, dev->addr7, BMP280_REG_PRESS_MSB,
                                raw, sizeof(raw));
    if (st != BMP280_OK) return st;

    /* Reconstruct 20-bit ADC values (MSB first, 4-bit left-aligned) */
    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4)
                    | (raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4)
                    | (raw[5] >> 4);

    int32_t t_fine;
    float t = compensate_temp(adc_T, &dev->calib, &t_fine);
    float p = compensate_press(adc_P, &dev->calib, t_fine);

    if (temp_c)    *temp_c    = t;
    if (press_hpa) *press_hpa = p;

    return BMP280_OK;
}

#endif /* BMP280_HOST_BUILD */
