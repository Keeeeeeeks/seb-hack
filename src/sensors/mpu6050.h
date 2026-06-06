/**
 * @file mpu6050.h
 * @brief MPU-6050 6-axis IMU driver (accelerometer + gyroscope).
 *
 * Interface: I²C (400 kHz fast-mode).
 * I²C address: 0x68 (AD0=GND) or 0x69 (AD0=VCC).
 *
 * Usage (on-target):
 *   mpu6050_init(&hi2c1, MPU6050_ADDR_DEFAULT);
 *   mpu6050_read(&hi2c1, MPU6050_ADDR_DEFAULT, &data);
 *   // data.roll_deg, data.pitch_deg, data.ax_g … data.gz_dps
 *
 * The module is host-compilable when MPU6050_HOST_BUILD is defined;
 * in that mode the HAL calls are omitted and only the pure-math helpers
 * (mpu6050_accel_to_roll, mpu6050_accel_to_pitch) are available.
 */

#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>
#include <stdbool.h>

/* ── I²C addresses (7-bit, not shifted) ─────────────────────────────────── */
#define MPU6050_ADDR_DEFAULT   0x68u   /**< AD0 pin = GND */
#define MPU6050_ADDR_ALT       0x69u   /**< AD0 pin = VCC */

/* ── Register map ────────────────────────────────────────────────────────── */
#define MPU6050_REG_SMPLRT_DIV    0x19u
#define MPU6050_REG_CONFIG        0x1Au
#define MPU6050_REG_GYRO_CONFIG   0x1Bu
#define MPU6050_REG_ACCEL_CONFIG  0x1Cu
#define MPU6050_REG_ACCEL_XOUT_H  0x3Bu   /**< First of 14 consecutive bytes */
#define MPU6050_REG_PWR_MGMT_1    0x6Bu
#define MPU6050_REG_WHO_AM_I      0x75u

/* ── WHO_AM_I expected value ─────────────────────────────────────────────── */
#define MPU6050_WHO_AM_I_VAL      0x68u

/* ── Full-scale range selectors ──────────────────────────────────────────── */
/** Accelerometer full-scale range. */
typedef enum {
    MPU6050_ACCEL_FS_2G  = 0x00,   /**< ±2 g,  LSB/g = 16384 */
    MPU6050_ACCEL_FS_4G  = 0x08,   /**< ±4 g,  LSB/g = 8192  */
    MPU6050_ACCEL_FS_8G  = 0x10,   /**< ±8 g,  LSB/g = 4096  */
    MPU6050_ACCEL_FS_16G = 0x18,   /**< ±16 g, LSB/g = 2048  */
} Mpu6050AccelFs;

/** Gyroscope full-scale range. */
typedef enum {
    MPU6050_GYRO_FS_250DPS  = 0x00,  /**< ±250 °/s,  LSB/(°/s) = 131   */
    MPU6050_GYRO_FS_500DPS  = 0x08,  /**< ±500 °/s,  LSB/(°/s) = 65.5  */
    MPU6050_GYRO_FS_1000DPS = 0x10,  /**< ±1000 °/s, LSB/(°/s) = 32.8  */
    MPU6050_GYRO_FS_2000DPS = 0x18,  /**< ±2000 °/s, LSB/(°/s) = 16.4  */
} Mpu6050GyroFs;

/* ── Output data structure ───────────────────────────────────────────────── */
/**
 * Processed sensor data from one MPU-6050 read.
 * Raw 16-bit values are converted to physical units.
 */
typedef struct {
    float ax_g;       /**< Acceleration X [g] */
    float ay_g;       /**< Acceleration Y [g] */
    float az_g;       /**< Acceleration Z [g] */
    float gx_dps;     /**< Angular rate X [°/s] */
    float gy_dps;     /**< Angular rate Y [°/s] */
    float gz_dps;     /**< Angular rate Z [°/s] */
    float roll_deg;   /**< Roll  angle from accel [°], range ±180° */
    float pitch_deg;  /**< Pitch angle from accel [°], range ±90°  */
} Mpu6050Data;

/* ── Error codes ─────────────────────────────────────────────────────────── */
typedef enum {
    MPU6050_OK          =  0,
    MPU6050_ERR_I2C     = -1,   /**< HAL I²C error */
    MPU6050_ERR_WHOAMI  = -2,   /**< WHO_AM_I mismatch */
} Mpu6050Status;

/* ── Pure-math helpers (host-compilable) ─────────────────────────────────── */

/**
 * Compute roll angle [°] from accelerometer readings.
 * Uses atan2(ay, az).  Valid when the device is near-static.
 */
float mpu6050_accel_to_roll(float ay_g, float az_g);

/**
 * Compute pitch angle [°] from accelerometer readings.
 * Uses atan2(-ax, sqrt(ay²+az²)).  Valid when the device is near-static.
 */
float mpu6050_accel_to_pitch(float ax_g, float ay_g, float az_g);

/* ── On-target API (requires HAL) ────────────────────────────────────────── */
#ifndef MPU6050_HOST_BUILD

#include "stm32g4xx_hal.h"

/**
 * Initialise the MPU-6050.
 *
 * Steps:
 *  1. Verify WHO_AM_I register.
 *  2. Wake device (clear SLEEP bit in PWR_MGMT_1).
 *  3. Set sample-rate divider to 0 (1 kHz internal / (1+0) = 1 kHz).
 *  4. Configure DLPF to 44 Hz bandwidth (CONFIG = 0x03).
 *  5. Set accel FS to ±2 g, gyro FS to ±250 °/s.
 *
 * @param hi2c   Pointer to initialised I²C handle.
 * @param addr7  7-bit I²C address (MPU6050_ADDR_DEFAULT or _ALT).
 * @return MPU6050_OK on success, negative error code on failure.
 */
Mpu6050Status mpu6050_init(I2C_HandleTypeDef *hi2c, uint8_t addr7);

/**
 * Read one sample from the MPU-6050.
 *
 * Reads 14 consecutive bytes starting at ACCEL_XOUT_H (registers 0x3B–0x48),
 * converts raw values to physical units, and computes roll/pitch.
 *
 * @param hi2c   Pointer to initialised I²C handle.
 * @param addr7  7-bit I²C address.
 * @param out    Pointer to output data structure.
 * @return MPU6050_OK on success, MPU6050_ERR_I2C on bus error.
 */
Mpu6050Status mpu6050_read(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                            Mpu6050Data *out);

#endif /* MPU6050_HOST_BUILD */
#endif /* MPU6050_H */
