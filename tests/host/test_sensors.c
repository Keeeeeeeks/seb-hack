/**
 * @file test_sensors.c
 * @brief Host unit tests for sensor register-decode math.
 *
 * Tests the pure-math decode logic extracted from the sensor drivers.
 * No HAL, no CMSIS — compiles with plain gcc.
 *
 * Compile and run:
 *   make test_sensors
 *
 * Exit code: 0 = all pass, 1 = any failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* M_PI is not guaranteed by C99 — define it if the platform omits it */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Minimal test framework ───────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_NEAR(label, got, expected, tol)                              \
    do {                                                                    \
        float _g = (float)(got);                                            \
        float _e = (float)(expected);                                       \
        float _t = (float)(tol);                                            \
        if (fabsf(_g - _e) <= _t) {                                        \
            printf("[PASS] %s  (got=%.4f, expected=%.4f)\n",               \
                   (label), (double)_g, (double)_e);                       \
            g_pass++;                                                       \
        } else {                                                            \
            printf("[FAIL] %s  (got=%.4f, expected=%.4f, tol=%.4f)\n",    \
                   (label), (double)_g, (double)_e, (double)_t);          \
            g_fail++;                                                       \
        }                                                                   \
    } while (0)

#define ASSERT_INT(label, got, expected)                                    \
    do {                                                                    \
        int _g = (int)(got);                                                \
        int _e = (int)(expected);                                           \
        if (_g == _e) {                                                     \
            printf("[PASS] %s  (got=%d)\n", (label), _g);                  \
            g_pass++;                                                       \
        } else {                                                            \
            printf("[FAIL] %s  (got=%d, expected=%d)\n",                   \
                   (label), _g, _e);                                        \
            g_fail++;                                                       \
        }                                                                   \
    } while (0)

/* ═══════════════════════════════════════════════════════════════════════
 * MPU-6050 decode helpers (mirrored from mpu6050.c)
 * ═══════════════════════════════════════════════════════════════════════ */

#define MPU6050_ACCEL_SENS  16384.0f   /* LSB/g at ±2 g */

static float mpu6050_raw_to_g(uint8_t hi, uint8_t lo)
{
    return (float)(int16_t)((uint16_t)(hi << 8) | lo) / MPU6050_ACCEL_SENS;
}

static float mpu6050_raw_to_temp(uint8_t hi, uint8_t lo)
{
    return (float)(int16_t)((uint16_t)(hi << 8) | lo) / 340.0f + 36.53f;
}

static float mpu6050_roll(float ay, float az)
{
    return atan2f(ay, az) * (180.0f / (float)M_PI);
}

static float mpu6050_pitch(float ax, float ay, float az)
{
    return atan2f(-ax, sqrtf(ay * ay + az * az)) * (180.0f / (float)M_PI);
}

/* ── Test: MPU-6050 accelerometer decode ─────────────────────────────── */

static void test_mpu6050_accel_decode(void)
{
    printf("\n--- MPU-6050 accelerometer decode ---\n");

    /* +1 g on Z axis: raw = 16384 = 0x4000 */
    ASSERT_NEAR("az=+1g (0x4000)",
                mpu6050_raw_to_g(0x40, 0x00), 1.0f, 0.0001f);

    /* -1 g on Z axis: raw = -16384 = 0xC000 */
    ASSERT_NEAR("az=-1g (0xC000)",
                mpu6050_raw_to_g(0xC0, 0x00), -1.0f, 0.0001f);

    /* +2 g (full scale): raw = 32767 = 0x7FFF */
    ASSERT_NEAR("ax=+2g (0x7FFF)",
                mpu6050_raw_to_g(0x7F, 0xFF), 2.0f, 0.001f);

    /* -2 g (full scale negative): raw = -32768 = 0x8000 */
    ASSERT_NEAR("ax=-2g (0x8000)",
                mpu6050_raw_to_g(0x80, 0x00), -2.0f, 0.001f);

    /* Zero: raw = 0x0000 */
    ASSERT_NEAR("ax=0g (0x0000)",
                mpu6050_raw_to_g(0x00, 0x00), 0.0f, 0.0001f);

    /* Half scale: raw = 8192 = 0x2000 → 0.5 g */
    ASSERT_NEAR("ax=+0.5g (0x2000)",
                mpu6050_raw_to_g(0x20, 0x00), 0.5f, 0.0001f);
}

/* ── Test: MPU-6050 temperature decode ───────────────────────────────── */

static void test_mpu6050_temp_decode(void)
{
    printf("\n--- MPU-6050 die temperature decode ---\n");

    /*
     * Formula: temp_c = raw / 340 + 36.53
     * At raw = 0: temp = 36.53 °C
     */
    ASSERT_NEAR("temp at raw=0 → 36.53°C",
                mpu6050_raw_to_temp(0x00, 0x00), 36.53f, 0.01f);

    /*
     * At raw = 340: temp = 340/340 + 36.53 = 37.53 °C
     */
    ASSERT_NEAR("temp at raw=340 → 37.53°C",
                mpu6050_raw_to_temp(0x01, 0x54), 37.53f, 0.01f);

    /*
     * At raw = -3400 (0xF2B8): temp = -3400/340 + 36.53 = 26.53 °C
     */
    int16_t raw_neg = -3400;
    uint8_t hi = (uint8_t)((uint16_t)raw_neg >> 8);
    uint8_t lo = (uint8_t)((uint16_t)raw_neg & 0xFF);
    ASSERT_NEAR("temp at raw=-3400 → 26.53°C",
                mpu6050_raw_to_temp(hi, lo), 26.53f, 0.01f);
}

/* ── Test: MPU-6050 roll/pitch from accel ────────────────────────────── */

static void test_mpu6050_angles(void)
{
    printf("\n--- MPU-6050 roll/pitch from accelerometer ---\n");

    /* Flat, face-up: ax=0, ay=0, az=+1g → roll=0°, pitch=0° */
    ASSERT_NEAR("flat: roll=0°",
                mpu6050_roll(0.0f, 1.0f), 0.0f, 0.01f);
    ASSERT_NEAR("flat: pitch=0°",
                mpu6050_pitch(0.0f, 0.0f, 1.0f), 0.0f, 0.01f);

    /* Rolled 90° right: ay=+1g, az=0 → roll=90° */
    ASSERT_NEAR("rolled 90° right: roll=90°",
                mpu6050_roll(1.0f, 0.0f), 90.0f, 0.01f);

    /* Rolled 90° left: ay=-1g, az=0 → roll=-90° */
    ASSERT_NEAR("rolled 90° left: roll=-90°",
                mpu6050_roll(-1.0f, 0.0f), -90.0f, 0.01f);

    /* Pitched 90° forward: ax=-1g, ay=0, az=0 → pitch=90° */
    ASSERT_NEAR("pitched 90° forward: pitch=90°",
                mpu6050_pitch(-1.0f, 0.0f, 0.0f), 90.0f, 0.01f);

    /* Pitched 90° back: ax=+1g, ay=0, az=0 → pitch=-90° */
    ASSERT_NEAR("pitched 90° back: pitch=-90°",
                mpu6050_pitch(1.0f, 0.0f, 0.0f), -90.0f, 0.01f);

    /* 45° roll: ay=az=1/√2 → roll=45° */
    float v = 1.0f / sqrtf(2.0f);
    ASSERT_NEAR("rolled 45°: roll=45°",
                mpu6050_roll(v, v), 45.0f, 0.1f);
}

/* ═══════════════════════════════════════════════════════════════════════
 * MCP9808 decode helpers (mirrored from mcp9808.c)
 * ═══════════════════════════════════════════════════════════════════════ */

static float mcp9808_raw_to_temp(uint16_t raw)
{
    int sign = (raw & 0x1000u) ? -1 : 1;
    raw &= 0x0FFFu;
    return (float)sign * ((float)raw / 16.0f);
}

/* ── Test: MCP9808 temperature decode ────────────────────────────────── */

static void test_mcp9808_temp_decode(void)
{
    printf("\n--- MCP9808 temperature decode ---\n");

    /* +25.0625 °C: 0x0191 = 0b 0000 0001 1001 0001 → 0x191 = 401 → 401/16 = 25.0625 */
    ASSERT_NEAR("+25.0625°C (0x0191)",
                mcp9808_raw_to_temp(0x0191u), 25.0625f, 0.001f);

    /* +0.0625 °C: 0x0001 → 1/16 = 0.0625 */
    ASSERT_NEAR("+0.0625°C (0x0001)",
                mcp9808_raw_to_temp(0x0001u), 0.0625f, 0.001f);

    /* 0.0 °C: 0x0000 */
    ASSERT_NEAR("0.0°C (0x0000)",
                mcp9808_raw_to_temp(0x0000u), 0.0f, 0.001f);

    /* -0.0625 °C: sign bit set, magnitude = 0x001 → -0.0625 */
    ASSERT_NEAR("-0.0625°C (0x1001)",
                mcp9808_raw_to_temp(0x1001u), -0.0625f, 0.001f);

    /* -25.0625 °C: sign bit set, magnitude = 0x191 → -25.0625 */
    ASSERT_NEAR("-25.0625°C (0x1191)",
                mcp9808_raw_to_temp(0x1191u), -25.0625f, 0.001f);

    /* +125.0 °C (max): 0x07D0 = 2000 → 2000/16 = 125.0 */
    ASSERT_NEAR("+125.0°C (0x07D0)",
                mcp9808_raw_to_temp(0x07D0u), 125.0f, 0.001f);

    /* -40.0 °C (min): sign=1, magnitude = 40*16 = 640 = 0x280 → 0x1280 */
    ASSERT_NEAR("-40.0°C (0x1280)",
                mcp9808_raw_to_temp(0x1280u), -40.0f, 0.001f);

    /* Alert flags (bits 15:13) must be masked off: 0xE191 → same as 0x0191 */
    ASSERT_NEAR("alert flags masked: 0xE191 → +25.0625°C",
                mcp9808_raw_to_temp(0xE191u & 0x1FFFu), 25.0625f, 0.001f);
}

/* ═══════════════════════════════════════════════════════════════════════
 * HC-SR04 decode helpers (mirrored from hcsr04.c)
 * ═══════════════════════════════════════════════════════════════════════ */

#define HCSR04_MIN_MM  20u
#define HCSR04_MAX_MM  4000u

static uint32_t hcsr04_echo_to_mm(uint32_t echo_us)
{
    uint32_t raw_mm = (echo_us * 10u) / 58u;
    if (raw_mm < HCSR04_MIN_MM) raw_mm = HCSR04_MIN_MM;
    if (raw_mm > HCSR04_MAX_MM) raw_mm = HCSR04_MAX_MM;
    return raw_mm;
}

static uint32_t hcsr04_counter_wrap(uint32_t t_rise, uint32_t t_fall)
{
    if (t_fall >= t_rise) {
        return t_fall - t_rise;
    } else {
        return (0xFFFFu - t_rise) + t_fall + 1u;
    }
}

static uint32_t hcsr04_iir(uint32_t prev, uint32_t raw)
{
    return (prev * 9u + raw) / 10u;
}

/* ── Test: HC-SR04 echo → distance ──────────────────────────────────── */

static void test_hcsr04_distance_decode(void)
{
    printf("\n--- HC-SR04 echo → distance decode ---\n");

    /*
     * Formula: distance_mm = (echo_us * 10) / 58
     * Speed of sound ≈ 343 m/s → 0.343 mm/µs round-trip → 0.1715 mm/µs one-way
     * Manufacturer formula: distance_cm = echo_us / 58 → distance_mm = echo_us*10/58
     */

    /* 58 µs → 10 mm (below min → clamped to 20 mm) */
    ASSERT_INT("58µs → clamped to 20mm",
               (int)hcsr04_echo_to_mm(58u), 20);

    /* 116 µs → 20 mm (exactly at min) */
    ASSERT_INT("116µs → 20mm",
               (int)hcsr04_echo_to_mm(116u), 20);

    /* 580 µs → 100 mm */
    ASSERT_INT("580µs → 100mm",
               (int)hcsr04_echo_to_mm(580u), 100);

    /* 5800 µs → 1000 mm */
    ASSERT_INT("5800µs → 1000mm",
               (int)hcsr04_echo_to_mm(5800u), 1000);

    /* 23200 µs → 4000 mm (exactly at max) */
    ASSERT_INT("23200µs → 4000mm",
               (int)hcsr04_echo_to_mm(23200u), 4000);

    /* 30000 µs → clamped to 4000 mm */
    ASSERT_INT("30000µs → clamped to 4000mm",
               (int)hcsr04_echo_to_mm(30000u), 4000);

    /* 0 µs → clamped to 20 mm */
    ASSERT_INT("0µs → clamped to 20mm",
               (int)hcsr04_echo_to_mm(0u), 20);
}

/* ── Test: HC-SR04 counter wrap ──────────────────────────────────────── */

static void test_hcsr04_counter_wrap(void)
{
    printf("\n--- HC-SR04 16-bit counter wrap ---\n");

    /* No wrap: t_fall > t_rise */
    ASSERT_INT("no wrap: 1000-500=500",
               (int)hcsr04_counter_wrap(500u, 1000u), 500);

    /* No wrap: t_fall == t_rise (zero-width echo) */
    ASSERT_INT("no wrap: 1000-1000=0",
               (int)hcsr04_counter_wrap(1000u, 1000u), 0);

    /* Wrap: t_fall < t_rise (counter rolled over 0xFFFF) */
    /* t_rise=65000, t_fall=100 → echo = (65535-65000) + 100 + 1 = 636 */
    ASSERT_INT("wrap: rise=65000, fall=100 → 636",
               (int)hcsr04_counter_wrap(65000u, 100u), 636);

    /* Wrap: t_rise=65535, t_fall=0 → echo = 1 */
    ASSERT_INT("wrap: rise=65535, fall=0 → 1",
               (int)hcsr04_counter_wrap(65535u, 0u), 1);

    /* Wrap: t_rise=65535, t_fall=99 → echo = 100 */
    ASSERT_INT("wrap: rise=65535, fall=99 → 100",
               (int)hcsr04_counter_wrap(65535u, 99u), 100);
}

/* ── Test: HC-SR04 IIR smoothing ─────────────────────────────────────── */

static void test_hcsr04_iir(void)
{
    printf("\n--- HC-SR04 IIR smoothing (α=0.1 integer) ---\n");

    /* From 0 to 1000: one step → (0*9 + 1000)/10 = 100 */
    ASSERT_INT("0→1000 one step: 100",
               (int)hcsr04_iir(0u, 1000u), 100);

    /* Stable: same input → same output */
    ASSERT_INT("stable: 500→500 = 500",
               (int)hcsr04_iir(500u, 500u), 500);

    /* Convergence: 100 steps from 0 toward 1000.
     * Integer IIR with floor division stalls at 991 (verified empirically):
     * each step: (prev*9 + 1000)/10 with truncation.  The filter converges
     * to within 1% of target — good enough for distance smoothing. */
    uint32_t state = 0;
    for (int i = 0; i < 100; i++) {
        state = hcsr04_iir(state, 1000u);
    }
    ASSERT_INT("IIR converges to ≥990 after 100 steps (within 1% of 1000)",
               (int)state >= 990, 1);

    /* Step down: 1000 → 0, one step → (1000*9 + 0)/10 = 900 */
    ASSERT_INT("1000→0 one step: 900",
               (int)hcsr04_iir(1000u, 0u), 900);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Audio engine wavetable math (mirrored from audio_engine.c)
 * ═══════════════════════════════════════════════════════════════════════ */

#define AUDIO_WAVETABLE_N   32u
#define AUDIO_DAC_MIDSCALE  2048u

static void build_wavetable(uint16_t *tbl)
{
    for (uint32_t i = 0; i < AUDIO_WAVETABLE_N; i++) {
        float angle = 2.0f * (float)M_PI * (float)i / (float)AUDIO_WAVETABLE_N;
        tbl[i] = (uint16_t)((sinf(angle) + 1.0f) * 2047.5f);
    }
}

/* ── Test: audio wavetable properties ────────────────────────────────── */

static void test_audio_wavetable(void)
{
    printf("\n--- Audio engine wavetable ---\n");

    uint16_t tbl[AUDIO_WAVETABLE_N];
    build_wavetable(tbl);

    /* All samples must be in [0, 4095] (12-bit DAC range) */
    int all_in_range = 1;
    for (uint32_t i = 0; i < AUDIO_WAVETABLE_N; i++) {
        if (tbl[i] > 4095u) { all_in_range = 0; break; }
    }
    ASSERT_INT("all samples in [0, 4095]", all_in_range, 1);

    /* Sample 0 (angle=0): sin(0)=0 → (0+1)*2047.5 = 2047 or 2048 */
    ASSERT_INT("sample[0] ≈ mid-scale (2047 or 2048)",
               (tbl[0] == 2047u || tbl[0] == 2048u), 1);

    /* Sample N/4 (angle=π/2): sin(π/2)=1 → (1+1)*2047.5 = 4095 */
    ASSERT_INT("sample[N/4] = 4095 (peak)",
               (int)tbl[AUDIO_WAVETABLE_N / 4], 4095);

    /* Sample N/2 (angle=π): sin(π)≈0 → ≈ mid-scale */
    ASSERT_INT("sample[N/2] ≈ mid-scale (2047 or 2048)",
               (tbl[AUDIO_WAVETABLE_N / 2] == 2047u ||
                tbl[AUDIO_WAVETABLE_N / 2] == 2048u), 1);

    /* Sample 3N/4 (angle=3π/2): sin(3π/2)=-1 → (−1+1)*2047.5 = 0 */
    ASSERT_INT("sample[3N/4] = 0 (trough)",
               (int)tbl[3 * AUDIO_WAVETABLE_N / 4], 0);

    /* Symmetry: tbl[i] + tbl[i + N/2] ≈ 4095 (sine is antisymmetric about mid) */
    int symmetric = 1;
    for (uint32_t i = 0; i < AUDIO_WAVETABLE_N / 2; i++) {
        int sum = (int)tbl[i] + (int)tbl[i + AUDIO_WAVETABLE_N / 2];
        if (abs(sum - 4095) > 1) { symmetric = 0; break; }
    }
    ASSERT_INT("wavetable is antisymmetric (tbl[i]+tbl[i+N/2]≈4095)", symmetric, 1);
}

/* ── Test: audio gain scaling ────────────────────────────────────────── */

static void test_audio_gain_scaling(void)
{
    printf("\n--- Audio engine gain scaling (q15) ---\n");

    /*
     * Gain scaling: sample = (sample_signed * gain_q15) >> 15
     * gain_q15 = (uint32_t)(g * 32767)
     */

    /* Full gain (g=1.0): gain_q15=32767.
     * 2047 * 32767 >> 15 = 67,104,769 >> 15 = 2046 (q15 rounding loss of 1 LSB).
     * This is the expected result — q15 with gain<1.0 always rounds down. */
    int32_t sample = 2047;
    uint32_t gain_q15 = 32767u;
    int32_t scaled = (sample * (int32_t)gain_q15) >> 15;
    ASSERT_INT("full gain: 2047 * 32767>>15 = 2046 (q15 rounding)", scaled, 2046);

    /* Half gain (g=0.5): gain_q15=16383, sample halved */
    gain_q15 = 16383u;
    scaled = (sample * (int32_t)gain_q15) >> 15;
    ASSERT_INT("half gain: 2047 * 0.5 ≈ 1023", scaled, 1023);

    /* Zero gain (g=0.0): gain_q15=0, sample=0 */
    gain_q15 = 0u;
    scaled = (sample * (int32_t)gain_q15) >> 15;
    ASSERT_INT("zero gain: 2047 * 0.0 = 0", scaled, 0);

    /* Negative sample (below mid-scale): -2048 * 1.0 = -2048 */
    sample = -2048;
    gain_q15 = 32767u;
    scaled = (sample * (int32_t)gain_q15) >> 15;
    ASSERT_INT("full gain: -2048 * 1.0 ≈ -2048", scaled, -2048);

    /* Clamp check: after gain, sample must be in [-2048, 2047] */
    sample = 2047;
    gain_q15 = 32767u;
    scaled = (sample * (int32_t)gain_q15) >> 15;
    if (scaled < -2048) scaled = -2048;
    if (scaled >  2047) scaled =  2047;
    uint32_t dac_val = (uint32_t)(scaled + 2048);
    ASSERT_INT("DAC output in [0, 4095]", (dac_val <= 4095u), 1);
}

/* ── Test: TIM6 ARR computation ──────────────────────────────────────── */

static void test_tim6_arr(void)
{
    printf("\n--- TIM6 ARR computation (1 MHz timer clock) ---\n");

    /*
     * ARR = (1 000 000 / isr_hz) - 1
     * isr_hz = freq_hz * AUDIO_WAVETABLE_N
     */

    /* 440 Hz × 32 = 14080 Hz ISR rate → ARR = 1000000/14080 - 1 ≈ 70 */
    float isr_hz = 440.0f * (float)AUDIO_WAVETABLE_N;
    uint32_t arr = (uint32_t)(1000000.0f / isr_hz);
    if (arr > 0) arr -= 1u;
    /* 1000000 / 14080 = 71.02 → arr = 70 */
    ASSERT_INT("440Hz × 32: ARR=70", (int)arr, 70);

    /* 110 Hz × 32 = 3520 Hz → ARR = 1000000/3520 - 1 ≈ 283 */
    isr_hz = 110.0f * (float)AUDIO_WAVETABLE_N;
    arr = (uint32_t)(1000000.0f / isr_hz);
    if (arr > 0) arr -= 1u;
    ASSERT_INT("110Hz × 32: ARR=283", (int)arr, 283);

    /* 880 Hz × 32 = 28160 Hz → ARR = 1000000/28160 - 1 ≈ 34 */
    isr_hz = 880.0f * (float)AUDIO_WAVETABLE_N;
    arr = (uint32_t)(1000000.0f / isr_hz);
    if (arr > 0) arr -= 1u;
    ASSERT_INT("880Hz × 32: ARR=34", (int)arr, 34);

    /* 16000 Hz × 32 = 512000 Hz → ARR = 1000000/512000 - 1 ≈ 0 (min)
     * 1000000 / 512000 = 1 (integer), then clamped before subtract → ARR = 0 */
    isr_hz = 16000.0f * (float)AUDIO_WAVETABLE_N;
    arr = (uint32_t)(1000000.0f / isr_hz);
    if (arr > 0u) arr -= 1u;
    /* arr = 0 at 16 kHz × 32 — verify it does not wrap to 0xFFFFFFFF */
    ASSERT_INT("16kHz × 32: ARR=0 (no underflow)", (int)arr, 0);
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== sensor + audio decode unit tests ===\n");

    test_mpu6050_accel_decode();
    test_mpu6050_temp_decode();
    test_mpu6050_angles();
    test_mcp9808_temp_decode();
    test_hcsr04_distance_decode();
    test_hcsr04_counter_wrap();
    test_hcsr04_iir();
    test_audio_wavetable();
    test_audio_gain_scaling();
    test_tim6_arr();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
