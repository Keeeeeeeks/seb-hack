/**
 * @file hcsr04.h
 * @brief HC-SR04 ultrasonic distance sensor driver.
 *
 * Interface: GPIO (trigger) + TIM4 input-capture (echo).
 *
 * Hardware assignment (matches main.h / bist.h):
 *   TRIG — PB5, GPIO output, active-high 10 µs pulse
 *   ECHO — PB6, TIM4_CH1 AF2, input-capture on both edges
 *
 * Measurement principle:
 *   1. Assert TRIG high for ≥10 µs, then low.
 *   2. Sensor emits 8× 40 kHz bursts and drives ECHO high.
 *   3. ECHO stays high for the round-trip time of the ultrasonic pulse.
 *   4. Distance [mm] = echo_duration_us * 1000 / (2 * 5.8)
 *                    = echo_duration_us * 1000 / 11.6
 *      (speed of sound ≈ 343 m/s → 0.343 mm/µs one-way,
 *       round-trip factor 2 → 0.1724 mm/µs → 5.8 µs/mm)
 *
 * Timer configuration (set up by caller in MX_TIM4_Init):
 *   - TIM4 clocked at 1 MHz (1 µs per tick) via prescaler.
 *   - CH1 configured for input-capture on both edges (TIM_ICPOLARITY_BOTHEDGE).
 *   - TIM4 global interrupt enabled (HAL_TIM_IC_Start_IT).
 *
 * ISR usage:
 *   Call hcsr04_tim_irq_handler() from HAL_TIM_IC_CaptureCallback().
 *   The driver is ISR-safe: shared state uses volatile fields.
 *
 * Usage (on-target):
 *   hcsr04_init();                    // once, after MX_TIM4_Init
 *   hcsr04_trigger();                 // call every ≥60 ms
 *   uint32_t d = hcsr04_get_mm();     // returns last valid distance
 *
 * Host-compilable when HCSR04_HOST_BUILD is defined.
 */

#ifndef HCSR04_H
#define HCSR04_H

#include <stdint.h>
#include <stdbool.h>

/* ── Timing constants ────────────────────────────────────────────────────── */
/** Maximum echo duration [µs] before declaring a timeout (~4 m range). */
#define HCSR04_TIMEOUT_US    25000u

/** Minimum valid distance [mm] (sensor blind zone ≈ 20 mm). */
#define HCSR04_MIN_DIST_MM   20u

/** Maximum valid distance [mm] (≈4 m). */
#define HCSR04_MAX_DIST_MM   4000u

/** Returned when no valid measurement is available. */
#define HCSR04_INVALID_MM    0xFFFFFFFFu

/* ── On-target API ───────────────────────────────────────────────────────── */
#ifndef HCSR04_HOST_BUILD

#include "stm32g4xx_hal.h"

/**
 * Initialise driver state.
 * Must be called once after MX_TIM4_Init() and HAL_TIM_IC_Start_IT().
 */
void hcsr04_init(void);

/**
 * Send a 10 µs trigger pulse on PB5.
 *
 * Must NOT be called more often than every 60 ms (sensor specification).
 * Safe to call from the main loop; uses HAL_Delay(1) for the pulse width
 * (1 ms >> 10 µs — acceptable for this application).
 */
void hcsr04_trigger(void);

/**
 * Return the most recent valid distance measurement [mm].
 *
 * Thread-safe: reads a volatile uint32_t written by the TIM4 ISR.
 *
 * @return Distance in mm, or HCSR04_INVALID_MM if no measurement is ready
 *         or the last measurement timed out.
 */
uint32_t hcsr04_get_mm(void);

/**
 * TIM4 input-capture ISR handler.
 *
 * Call this from HAL_TIM_IC_CaptureCallback() when htim->Instance == TIM4.
 * Captures rising and falling edges of the ECHO signal and computes distance.
 *
 * @param htim  TIM handle passed by HAL callback.
 */
void hcsr04_tim_irq_handler(TIM_HandleTypeDef *htim);

#endif /* HCSR04_HOST_BUILD */
#endif /* HCSR04_H */
