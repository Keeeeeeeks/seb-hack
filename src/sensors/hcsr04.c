/**
 * @file hcsr04.c
 * @brief HC-SR04 ultrasonic distance sensor driver implementation.
 *
 * TIM4 is configured at 1 MHz (1 µs/tick) by MX_TIM4_Init in main.c.
 * CH1 captures both edges of the ECHO signal on PB6 (AF2).
 *
 * Distance formula:
 *   echo_us = (capture_end - capture_start) [with 16-bit wrap handled]
 *   dist_mm = echo_us * 1000 / 5800
 *           = echo_us / 5.8
 *   (speed of sound 343 m/s, round-trip → 1 mm = 5.8 µs)
 *
 * ISR safety:
 *   g_hcsr04.rising_captured and g_hcsr04.dist_mm are volatile.
 *   The main context only reads dist_mm; the ISR only writes it after
 *   a complete measurement, so no critical section is needed on Cortex-M4
 *   for a 32-bit aligned word (single-cycle read/write).
 */

#include "hcsr04.h"

#ifndef HCSR04_HOST_BUILD

#include "main.h"   /* TRIG_PORT, TRIG_PIN, ECHO_PORT, ECHO_PIN, htim4 */

/* ── Driver state (ISR-shared fields are volatile) ───────────────────────── */
static struct {
    volatile uint32_t rise_tick;         /**< TIM4 count at rising edge  */
    volatile bool     rising_captured;   /**< True after first edge seen */
    volatile uint32_t dist_mm;           /**< Last valid distance [mm]   */
} g_hcsr04;

/* ── TIM4 prescaler gives 1 µs/tick at 170 MHz system clock.
 *    TIM4 is 16-bit → wraps at 65535 µs ≈ 65 ms.
 *    HCSR04_TIMEOUT_US (25000) is well within one period. ─────────────────── */

void hcsr04_init(void)
{
    g_hcsr04.rise_tick       = 0u;
    g_hcsr04.rising_captured = false;
    g_hcsr04.dist_mm         = HCSR04_INVALID_MM;
}

void hcsr04_trigger(void)
{
    /* Reset edge-detection state before each trigger */
    g_hcsr04.rising_captured = false;

    /* Assert TRIG high */
    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_SET);

    /* Hold for ≥10 µs.  HAL_Delay(1) gives 1 ms — safe upper bound.
     * A DWT-based 10 µs delay would be more precise but is not required
     * for the HC-SR04 (the sensor only needs ≥10 µs). */
    HAL_Delay(1);

    /* De-assert TRIG */
    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);
}

uint32_t hcsr04_get_mm(void)
{
    return g_hcsr04.dist_mm;
}

void hcsr04_tim_irq_handler(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM4) return;

    uint32_t captured = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

    if (!g_hcsr04.rising_captured) {
        /* Rising edge: record start time */
        g_hcsr04.rise_tick       = captured;
        g_hcsr04.rising_captured = true;

        /* Reconfigure capture polarity to falling edge */
        __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1,
                                      TIM_INPUTCHANNELPOLARITY_FALLING);
    } else {
        /* Falling edge: compute echo duration with 16-bit wrap */
        uint32_t echo_us;
        if (captured >= g_hcsr04.rise_tick) {
            echo_us = captured - g_hcsr04.rise_tick;
        } else {
            /* Timer wrapped (period = 65536 µs) */
            echo_us = (65536u - g_hcsr04.rise_tick) + captured;
        }

        g_hcsr04.rising_captured = false;

        /* Reconfigure capture polarity back to rising edge for next trigger */
        __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1,
                                      TIM_INPUTCHANNELPOLARITY_RISING);

        /* Reject timeout and out-of-range readings */
        if (echo_us == 0u || echo_us > HCSR04_TIMEOUT_US) {
            g_hcsr04.dist_mm = HCSR04_INVALID_MM;
            return;
        }

        /* dist_mm = echo_us / 5.8  (integer arithmetic: *1000/5800) */
        uint32_t dist = (echo_us * 1000u) / 5800u;

        if (dist < HCSR04_MIN_DIST_MM || dist > HCSR04_MAX_DIST_MM) {
            g_hcsr04.dist_mm = HCSR04_INVALID_MM;
        } else {
            g_hcsr04.dist_mm = dist;
        }
    }
}

#endif /* HCSR04_HOST_BUILD */
