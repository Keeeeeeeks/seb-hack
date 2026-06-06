/**
 * @file bist.h
 * @brief Built-In Self-Test (BIST) — triggered by {"cmd":"selftest"}.
 *
 * Runs sequentially, prints one NDJSON result line per subsystem over
 * LPUART1, and sets telemetry status flags for any FAIL result.
 *
 * bist_run() blocks for approximately 1–2 s (servo sweep + DAC burst +
 * LED blinks).  It must be called from the main loop only — never from
 * an ISR.
 */

#ifndef BIST_H
#define BIST_H

#include <stdint.h>

/* ── Result codes ─────────────────────────────────────────────────────── */

typedef enum {
    BIST_PASS = 0,
    BIST_FAIL = 1,
} BistResult;

/* ── Per-subsystem status (written by bist_run, read by telemetry) ────── */

typedef struct {
    BistResult mpu;    /**< MPU-6050 WHO_AM_I check */
    BistResult mcp;    /**< MCP9808 Mfr ID check    */
    BistResult sr04;   /**< HC-SR04 echo ping        */
    BistResult dac;    /**< DAC 440 Hz tone burst    */
    BistResult servo;  /**< Servo sweep 0→90→180°   */
    BistResult led;    /**< LED blink × 3            */
    uint32_t   sr04_dist_mm;  /**< Distance measured during BIST ping */
} BistStatus;

/* ── Public API ───────────────────────────────────────────────────────── */

/**
 * Run the full BIST sequence.
 *
 * Sequence:
 *  1. I2C scan: write to MPU-6050 (0x68), read WHO_AM_I (0x75) → 0x68.
 *  2. I2C scan: write to MCP9808 (0x18), read Mfr ID (0x06) → 0x0054.
 *  3. HC-SR04: one 10 µs trigger on PB5, wait ≤30 ms for echo on PB6.
 *  4. DAC tone: emit 440 Hz for 200 ms via DAC1_OUT1 (PA4).
 *  5. Servo sweep: TIM3_CH1 → 0°, 90°, 180°, 90°, 200 ms per step.
 *  6. LED test: PA5 blink 3× fast (100 ms on/off).
 *  7. Print final summary line.
 *
 * Each step prints one NDJSON line:
 *   {"bist":"mpu",   "result":"PASS"}
 *   {"bist":"mcp",   "result":"PASS"}
 *   {"bist":"sr04",  "result":"PASS","dist_mm":342}
 *   {"bist":"dac",   "result":"PASS"}
 *   {"bist":"servo", "result":"PASS"}
 *   {"bist":"led",   "result":"PASS"}
 *   {"bist":"done",  "all":"PASS"}   (or "FAIL" if any step failed)
 *
 * @param status_out  Optional pointer to receive per-subsystem results.
 *                    May be NULL.
 */
void bist_run(BistStatus *status_out);

#endif /* BIST_H */
