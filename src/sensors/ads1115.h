/**
 * @file ads1115.h
 * @brief ADS1115 16-bit, 4-channel I²C ADC driver.
 *
 * Interface: I²C (up to 400 kHz fast-mode).
 * I²C address: 0x48–0x4B (ADDR pin selects).
 *
 * The ADS1115 is a delta-sigma ADC with:
 *   - 16-bit resolution (signed, two's complement)
 *   - Programmable gain amplifier (PGA): ±0.256 V to ±6.144 V full-scale
 *   - 4 single-ended or 2 differential inputs
 *   - Programmable data rate: 8–860 SPS
 *   - Single-shot or continuous conversion mode
 *
 * This driver uses single-shot mode to minimise power consumption.
 * Each call to ads1115_read_channel() triggers one conversion and polls
 * the OS (operational status) bit until complete.
 *
 * Usage (on-target):
 *   ads1115_init(&hi2c1, ADS1115_ADDR_GND);
 *   float volts;
 *   ads1115_read_channel(&hi2c1, ADS1115_ADDR_GND,
 *                        ADS1115_MUX_AIN0_GND, ADS1115_PGA_4096MV,
 *                        &volts);
 *
 * Host-compilable when ADS1115_HOST_BUILD is defined.
 */

#ifndef ADS1115_H
#define ADS1115_H

#include <stdint.h>

/* ── I²C addresses (7-bit) ───────────────────────────────────────────────── */
#define ADS1115_ADDR_GND   0x48u   /**< ADDR pin = GND */
#define ADS1115_ADDR_VDD   0x49u   /**< ADDR pin = VDD */
#define ADS1115_ADDR_SDA   0x4Au   /**< ADDR pin = SDA */
#define ADS1115_ADDR_SCL   0x4Bu   /**< ADDR pin = SCL */

/* ── Register pointers ───────────────────────────────────────────────────── */
#define ADS1115_REG_CONV    0x00u   /**< Conversion register (read)  */
#define ADS1115_REG_CONFIG  0x01u   /**< Configuration register      */
#define ADS1115_REG_LO_THR  0x02u   /**< Low threshold register      */
#define ADS1115_REG_HI_THR  0x03u   /**< High threshold register     */

/* ── Config register bit fields ──────────────────────────────────────────── */

/** OS bit (bit 15): write 1 to start single-shot conversion. */
#define ADS1115_OS_START    (1u << 15)

/** MUX[14:12]: input multiplexer configuration. */
typedef enum {
    ADS1115_MUX_DIFF_01    = (0x00u << 12),  /**< AIN0 − AIN1 (default) */
    ADS1115_MUX_DIFF_03    = (0x01u << 12),  /**< AIN0 − AIN3           */
    ADS1115_MUX_DIFF_13    = (0x02u << 12),  /**< AIN1 − AIN3           */
    ADS1115_MUX_DIFF_23    = (0x03u << 12),  /**< AIN2 − AIN3           */
    ADS1115_MUX_AIN0_GND   = (0x04u << 12),  /**< AIN0 − GND            */
    ADS1115_MUX_AIN1_GND   = (0x05u << 12),  /**< AIN1 − GND            */
    ADS1115_MUX_AIN2_GND   = (0x06u << 12),  /**< AIN2 − GND            */
    ADS1115_MUX_AIN3_GND   = (0x07u << 12),  /**< AIN3 − GND            */
} Ads1115Mux;

/** PGA[11:9]: programmable gain amplifier full-scale range. */
typedef enum {
    ADS1115_PGA_6144MV  = (0x00u << 9),  /**< ±6.144 V, LSB = 187.5 µV */
    ADS1115_PGA_4096MV  = (0x01u << 9),  /**< ±4.096 V, LSB = 125.0 µV */
    ADS1115_PGA_2048MV  = (0x02u << 9),  /**< ±2.048 V, LSB =  62.5 µV (default) */
    ADS1115_PGA_1024MV  = (0x03u << 9),  /**< ±1.024 V, LSB =  31.25 µV */
    ADS1115_PGA_512MV   = (0x04u << 9),  /**< ±0.512 V, LSB =  15.625 µV */
    ADS1115_PGA_256MV   = (0x05u << 9),  /**< ±0.256 V, LSB =   7.8125 µV */
} Ads1115Pga;

/** MODE bit [8]: 0 = continuous, 1 = single-shot. */
#define ADS1115_MODE_SINGLE  (1u << 8)
#define ADS1115_MODE_CONT    (0u << 8)

/** DR[7:5]: data rate. */
typedef enum {
    ADS1115_DR_8SPS   = (0x00u << 5),
    ADS1115_DR_16SPS  = (0x01u << 5),
    ADS1115_DR_32SPS  = (0x02u << 5),
    ADS1115_DR_64SPS  = (0x03u << 5),
    ADS1115_DR_128SPS = (0x04u << 5),  /**< Default */
    ADS1115_DR_250SPS = (0x05u << 5),
    ADS1115_DR_475SPS = (0x06u << 5),
    ADS1115_DR_860SPS = (0x07u << 5),
} Ads1115Dr;

/** COMP_QUE[1:0]: disable comparator (set to 11). */
#define ADS1115_COMP_QUE_DISABLE  0x03u

/* ── LSB voltage per PGA setting [µV] ───────────────────────────────────── */
/** Returns the LSB size in µV for a given PGA setting. */
static inline float ads1115_lsb_uv(Ads1115Pga pga)
{
    /* Full-scale / 32768 (15-bit positive range) */
    switch (pga) {
        case ADS1115_PGA_6144MV: return 187.5f;
        case ADS1115_PGA_4096MV: return 125.0f;
        case ADS1115_PGA_2048MV: return  62.5f;
        case ADS1115_PGA_1024MV: return  31.25f;
        case ADS1115_PGA_512MV:  return  15.625f;
        case ADS1115_PGA_256MV:  return   7.8125f;
        default:                 return  62.5f;
    }
}

/* ── Error codes ─────────────────────────────────────────────────────────── */
typedef enum {
    ADS1115_OK         =  0,
    ADS1115_ERR_I2C    = -1,   /**< HAL I²C error */
    ADS1115_ERR_TIMEOUT = -2,  /**< Conversion did not complete in time */
} Ads1115Status;

/* ── On-target API ───────────────────────────────────────────────────────── */
#ifndef ADS1115_HOST_BUILD

#include "stm32g4xx_hal.h"

/**
 * Initialise the ADS1115.
 *
 * Verifies I²C communication by writing and reading back the config register.
 * Sets the default data rate to 128 SPS and disables the comparator.
 *
 * @param hi2c   Pointer to initialised I²C handle.
 * @param addr7  7-bit I²C address.
 * @return ADS1115_OK on success, ADS1115_ERR_I2C on bus error.
 */
Ads1115Status ads1115_init(I2C_HandleTypeDef *hi2c, uint8_t addr7);

/**
 * Trigger a single-shot conversion and return the result in volts.
 *
 * Blocks until the conversion is complete (polls OS bit, max ~200 ms at
 * 8 SPS).  For 128 SPS (default) the conversion takes ~8 ms.
 *
 * @param hi2c    Pointer to initialised I²C handle.
 * @param addr7   7-bit I²C address.
 * @param mux     Input multiplexer selection.
 * @param pga     PGA full-scale range.
 * @param volts   Output: measured voltage in volts.
 * @return ADS1115_OK on success, negative error code on failure.
 */
Ads1115Status ads1115_read_channel(I2C_HandleTypeDef *hi2c, uint8_t addr7,
                                    Ads1115Mux mux, Ads1115Pga pga,
                                    float *volts);

#endif /* ADS1115_HOST_BUILD */
#endif /* ADS1115_H */
