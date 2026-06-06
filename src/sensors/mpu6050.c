/**
 * @file mpu6050.c
 * @brief MPU-6050 6-axis IMU driver implementation.
 *
 * Register references: InvenSense MPU-6050 Product Specification Rev 3.4,
 * and MPU-6000/6050 Register Map Rev 4.2.
 *
 * Accel sensitivity at ±2 g:  16384 LSB/g
 * Gyro  sensitivity at ±250°/s: 131.0 LSB/(°/s)
 */

#include "mpu6050.h"
#include <math.h>   /* atan2f, sqrtf */

/* ── Sensitivity constants (matching init configuration) ─────────────────── */
#define ACCEL_SENS_2G    16384.0f   /* LSB/g  for ±2 g  range */
#define GYRO_SENS_250DPS   131.0f   /* LSB/(°/s) for ±250 °/s range */

/* ── Pure-math helpers ───────────────────────────────────────────────────── */

float mpu6050_accel_to_roll(float ay_g, float az_g)
{
    return atan2f(ay_g, az_g) * (180.0f / 3.14159265f);
}

float mpu6050_accel_to_pitch(float ax_g, float ay_g, float az_g)
{
    float denom = sqrtf(ay_g * ay_g + az_g * az_g);
    return atan2f(-ax_g, denom) * (180.0f / 3.14159265f);
}

/* ── On-target implementation ────────────────────────────────────────────── */
#ifndef MPU6050_HOST_BUILD

/* HAL I²C timeout [ms] */
#define I2C_TIMEOUT_MS  10u

/**
 * Write a single byte to a register.
 * addr7 is the 7-bit address; HAL expects it left-shifted by 1.
 */
static Mpu6050Status reg_write(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                                uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    if (HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr7 << 1),
                                 buf, 2, I2C_TIMEOUT_MS) != HAL_OK) {
        return MPU6050_ERR_I2C;
    }
    return MPU6050_OK;
}

/**
 * Read one or more bytes starting at reg.
 */
static Mpu6050Status reg_read(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                               uint8_t reg, uint8_t *dst, uint16_t len)
{
    /* Write register address */
    if (HAL_I2C_Master_Transmit(hi2c, (uint16_t)(addr7 << 1),
                                 &reg, 1, I2C_TIMEOUT_MS) != HAL_OK) {
        return MPU6050_ERR_I2C;
    }
    /* Read data */
    if (HAL_I2C_Master_Receive(hi2c, (uint16_t)((addr7 << 1) | 1u),
                                dst, len, I2C_TIMEOUT_MS) != HAL_OK) {
        return MPU6050_ERR_I2C;
    }
    return MPU6050_OK;
}

Mpu6050Status mpu6050_init(I2C_HandleTypeDef *hi2c, uint8_t addr7)
{
    Mpu6050Status st;
    uint8_t val;

    /* 1. Verify WHO_AM_I */
    st = reg_read(hi2c, addr7, MPU6050_REG_WHO_AM_I, &val, 1);
    if (st != MPU6050_OK) return st;
    if (val != MPU6050_WHO_AM_I_VAL) return MPU6050_ERR_WHOAMI;

    /* 2. Wake device: clear SLEEP bit (bit 6) in PWR_MGMT_1.
     *    Use internal 8 MHz oscillator (CLKSEL = 0). */
    st = reg_write(hi2c, addr7, MPU6050_REG_PWR_MGMT_1, 0x00u);
    if (st != MPU6050_OK) return st;

    /* 3. Sample-rate divider = 0 → output rate = gyro rate / (1+0) = 1 kHz */
    st = reg_write(hi2c, addr7, MPU6050_REG_SMPLRT_DIV, 0x00u);
    if (st != MPU6050_OK) return st;

    /* 4. DLPF: CONFIG = 0x03 → accel BW 44 Hz, gyro BW 42 Hz */
    st = reg_write(hi2c, addr7, MPU6050_REG_CONFIG, 0x03u);
    if (st != MPU6050_OK) return st;

    /* 5. Accel full-scale ±2 g (ACCEL_CONFIG = 0x00) */
    st = reg_write(hi2c, addr7, MPU6050_REG_ACCEL_CONFIG,
                   (uint8_t)MPU6050_ACCEL_FS_2G);
    if (st != MPU6050_OK) return st;

    /* 6. Gyro full-scale ±250 °/s (GYRO_CONFIG = 0x00) */
    st = reg_write(hi2c, addr7, MPU6050_REG_GYRO_CONFIG,
                   (uint8_t)MPU6050_GYRO_FS_250DPS);
    if (st != MPU6050_OK) return st;

    return MPU6050_OK;
}

Mpu6050Status mpu6050_read(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                            Mpu6050Data *out)
{
    /* 14 bytes: ACCEL_XOUT_H/L, ACCEL_YOUT_H/L, ACCEL_ZOUT_H/L,
     *           TEMP_OUT_H/L,
     *           GYRO_XOUT_H/L, GYRO_YOUT_H/L, GYRO_ZOUT_H/L */
    uint8_t raw[14];
    Mpu6050Status st = reg_read(hi2c, addr7, MPU6050_REG_ACCEL_XOUT_H,
                                 raw, sizeof(raw));
    if (st != MPU6050_OK) return st;

    /* Reconstruct signed 16-bit values (big-endian) */
    int16_t ax_raw = (int16_t)((uint16_t)(raw[0]  << 8) | raw[1]);
    int16_t ay_raw = (int16_t)((uint16_t)(raw[2]  << 8) | raw[3]);
    int16_t az_raw = (int16_t)((uint16_t)(raw[4]  << 8) | raw[5]);
    /* raw[6..7] = temperature — not used here */
    int16_t gx_raw = (int16_t)((uint16_t)(raw[8]  << 8) | raw[9]);
    int16_t gy_raw = (int16_t)((uint16_t)(raw[10] << 8) | raw[11]);
    int16_t gz_raw = (int16_t)((uint16_t)(raw[12] << 8) | raw[13]);

    /* Convert to physical units */
    out->ax_g   = (float)ax_raw / ACCEL_SENS_2G;
    out->ay_g   = (float)ay_raw / ACCEL_SENS_2G;
    out->az_g   = (float)az_raw / ACCEL_SENS_2G;
    out->gx_dps = (float)gx_raw / GYRO_SENS_250DPS;
    out->gy_dps = (float)gy_raw / GYRO_SENS_250DPS;
    out->gz_dps = (float)gz_raw / GYRO_SENS_250DPS;

    /* Compute tilt angles from accelerometer (static approximation) */
    out->roll_deg  = mpu6050_accel_to_roll(out->ay_g, out->az_g);
    out->pitch_deg = mpu6050_accel_to_pitch(out->ax_g, out->ay_g, out->az_g);

    return MPU6050_OK;
}

#endif /* MPU6050_HOST_BUILD */
