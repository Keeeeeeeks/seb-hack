/**
 * @file test_mapping.c
 * @brief Host unit tests for the mapping layer.
 *
 * Compile and run on host (no HAL, no CMSIS):
 *   gcc -std=c99 -Wall -Wextra -I../../src/mapping \
 *       ../../src/mapping/mapping.c test_mapping.c -lm -o test_mapping
 *   ./test_mapping
 *
 * Exit code: 0 = all pass, 1 = any failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "mapping.h"

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

/* ── Test: mapping_dist_to_hz ─────────────────────────────────────────── */

static void test_dist_to_hz(void)
{
    printf("\n--- mapping_dist_to_hz ---\n");

    /* At minimum distance → minimum frequency */
    ASSERT_NEAR("dist_min → freq_min",
                mapping_dist_to_hz(MAP_DIST_MIN_MM),
                MAP_FREQ_MIN_HZ, 0.5f);

    /* At maximum distance → maximum frequency */
    ASSERT_NEAR("dist_max → freq_max",
                mapping_dist_to_hz(MAP_DIST_MAX_MM),
                MAP_FREQ_MAX_HZ, 0.5f);

    /* Mid-point (t=0.5) → 110 * 8^0.5 = 110 * 2.828 ≈ 311.1 Hz */
    float mid_mm = MAP_DIST_MIN_MM + 0.5f * (MAP_DIST_MAX_MM - MAP_DIST_MIN_MM);
    float expected_mid = MAP_FREQ_MIN_HZ * powf(8.0f, 0.5f);
    ASSERT_NEAR("dist_mid → freq_mid (log scale)",
                mapping_dist_to_hz(mid_mm),
                expected_mid, 1.0f);

    /* Below minimum → clamped to freq_min */
    ASSERT_NEAR("dist_below_min → freq_min (clamp)",
                mapping_dist_to_hz(0.0f),
                MAP_FREQ_MIN_HZ, 0.5f);

    /* Above maximum → clamped to freq_max */
    ASSERT_NEAR("dist_above_max → freq_max (clamp)",
                mapping_dist_to_hz(9999.0f),
                MAP_FREQ_MAX_HZ, 0.5f);

    /* Spec: 880/110 = 8 = 2^3 → three octaves */
    float ratio = MAP_FREQ_MAX_HZ / MAP_FREQ_MIN_HZ;
    ASSERT_NEAR("freq range is exactly 3 octaves (ratio=8)",
                ratio, 8.0f, 0.001f);
}

/* ── Test: mapping_pitch_to_cutoff ───────────────────────────────────── */

static void test_pitch_to_cutoff(void)
{
    printf("\n--- mapping_pitch_to_cutoff ---\n");

    /* At min pitch → min cutoff */
    ASSERT_NEAR("pitch_min → cutoff_min",
                mapping_pitch_to_cutoff(MAP_PITCH_MIN_DEG),
                MAP_CUTOFF_MIN_HZ, 1.0f);

    /* At max pitch → max cutoff */
    ASSERT_NEAR("pitch_max → cutoff_max",
                mapping_pitch_to_cutoff(MAP_PITCH_MAX_DEG),
                MAP_CUTOFF_MAX_HZ, 1.0f);

    /* At 0° (mid) → mid cutoff */
    float expected_mid = (MAP_CUTOFF_MIN_HZ + MAP_CUTOFF_MAX_HZ) / 2.0f;
    ASSERT_NEAR("pitch_0deg → cutoff_mid",
                mapping_pitch_to_cutoff(0.0f),
                expected_mid, 1.0f);

    /* Clamp below min */
    ASSERT_NEAR("pitch_below_min → cutoff_min (clamp)",
                mapping_pitch_to_cutoff(-999.0f),
                MAP_CUTOFF_MIN_HZ, 1.0f);

    /* Clamp above max */
    ASSERT_NEAR("pitch_above_max → cutoff_max (clamp)",
                mapping_pitch_to_cutoff(999.0f),
                MAP_CUTOFF_MAX_HZ, 1.0f);
}

/* ── Test: mapping_octave_gate ───────────────────────────────────────── */

static void test_octave_gate(void)
{
    printf("\n--- mapping_octave_gate ---\n");

    /* No gate when |roll| ≤ 30° */
    ASSERT_INT("roll=0 → no gate",   mapping_octave_gate(0.0f),   0);
    ASSERT_INT("roll=29 → no gate",  mapping_octave_gate(29.0f),  0);
    ASSERT_INT("roll=-29 → no gate", mapping_octave_gate(-29.0f), 0);
    ASSERT_INT("roll=30 → no gate",  mapping_octave_gate(30.0f),  0);

    /* Gate fires when |roll| > 30° */
    ASSERT_INT("roll=31 → gate",     mapping_octave_gate(31.0f),  1);
    ASSERT_INT("roll=-31 → gate",    mapping_octave_gate(-31.0f), 1);
    ASSERT_INT("roll=90 → gate",     mapping_octave_gate(90.0f),  1);
    ASSERT_INT("roll=-90 → gate",    mapping_octave_gate(-90.0f), 1);
}

/* ── Test: mapping_iir_step ──────────────────────────────────────────── */

static void test_iir_step(void)
{
    printf("\n--- mapping_iir_step ---\n");

    /* With α=1.0 → output equals input */
    ASSERT_NEAR("alpha=1.0 → output=input",
                mapping_iir_step(100.0f, 200.0f, 1.0f),
                200.0f, 0.001f);

    /* With α=0.0 → output equals prev_state */
    ASSERT_NEAR("alpha=0.0 → output=prev",
                mapping_iir_step(100.0f, 200.0f, 0.0f),
                100.0f, 0.001f);

    /* With α=0.1 (spec value): 0.1*200 + 0.9*100 = 20 + 90 = 110 */
    ASSERT_NEAR("alpha=0.1 → 0.1*200+0.9*100=110",
                mapping_iir_step(100.0f, 200.0f, MAP_IIR_ALPHA),
                110.0f, 0.001f);

    /* Convergence: after many steps with constant input, output → input */
    float state = 0.0f;
    for (int i = 0; i < 200; i++) {
        state = mapping_iir_step(state, 440.0f, MAP_IIR_ALPHA);
    }
    ASSERT_NEAR("IIR converges to target after 200 steps",
                state, 440.0f, 0.1f);
}

/* ── Test: mapping_update (integration) ─────────────────────────────── */

static void test_mapping_update(void)
{
    printf("\n--- mapping_update (integration) ---\n");

    mapping_init();

    /* ── Scenario 1: hand at minimum distance, no tilt ── */
    {
        SensorState s = {
            .dist_filt_mm = MAP_DIST_MIN_MM,
            .roll_deg     = 0.0f,
            .pitch_deg    = 0.0f,
            .temp_c       = 22.0f,
        };
        AudioParams out = {0};
        mapping_update(&s, &out);

        ASSERT_NEAR("update: dist_min → synth_hz=freq_min",
                    out.synth_hz, MAP_FREQ_MIN_HZ, 0.5f);
        ASSERT_NEAR("update: pitch=0 → cutoff_mid",
                    out.cutoff_hz,
                    (MAP_CUTOFF_MIN_HZ + MAP_CUTOFF_MAX_HZ) / 2.0f, 1.0f);
        ASSERT_NEAR("update: dist_min → volume=1.0",
                    out.volume, 1.0f, 0.01f);
        ASSERT_NEAR("update: roll=0 → vibrato_depth=0",
                    out.vibrato_depth_cents, 0.0f, 0.01f);
        ASSERT_NEAR("update: dist_min → servo_deg≈0",
                    out.servo_deg, 0.0f, 1.0f);
    }

    /* ── Scenario 2: hand at maximum distance, no tilt ── */
    {
        mapping_init();   /* reset IIR */
        SensorState s = {
            .dist_filt_mm = MAP_DIST_MAX_MM,
            .roll_deg     = 0.0f,
            .pitch_deg    = 0.0f,
            .temp_c       = 22.0f,
        };
        AudioParams out = {0};
        mapping_update(&s, &out);

        ASSERT_NEAR("update: dist_max → synth_hz=freq_max",
                    out.synth_hz, MAP_FREQ_MAX_HZ, 0.5f);
        ASSERT_NEAR("update: dist_max → volume≈0",
                    out.volume, 0.0f, 0.01f);
        ASSERT_NEAR("update: dist_max → servo_deg=180",
                    out.servo_deg, MAP_SERVO_MAX_DEG, 1.0f);
    }

    /* ── Scenario 3: octave gate fires ── */
    {
        mapping_init();
        SensorState s = {
            .dist_filt_mm = MAP_DIST_MIN_MM,   /* → 110 Hz */
            .roll_deg     = 45.0f,              /* |roll| > 30° → gate */
            .pitch_deg    = 0.0f,
            .temp_c       = 22.0f,
        };
        AudioParams out = {0};
        mapping_update(&s, &out);

        /* synth_hz should be doubled: 110 * 2 = 220 Hz */
        ASSERT_NEAR("update: octave gate → synth_hz doubled",
                    out.synth_hz, 220.0f, 1.0f);
    }

    /* ── Scenario 4: IIR smoothing — filt_hz lags synth_hz ── */
    {
        mapping_init();
        SensorState s = {
            .dist_filt_mm = MAP_DIST_MAX_MM,   /* → 880 Hz */
            .roll_deg     = 0.0f,
            .pitch_deg    = 0.0f,
            .temp_c       = 22.0f,
        };
        AudioParams out = {0};
        mapping_update(&s, &out);

        /* After one step from init (filt=110), filt_hz = 0.1*880 + 0.9*110 = 187 */
        float expected_filt = MAP_IIR_ALPHA * MAP_FREQ_MAX_HZ +
                              (1.0f - MAP_IIR_ALPHA) * MAP_FREQ_MIN_HZ;
        ASSERT_NEAR("update: IIR filt_hz lags synth_hz after 1 step",
                    out.filt_hz, expected_filt, 1.0f);
    }

    /* ── Scenario 5: vibrato depth proportional to |roll| ── */
    {
        mapping_init();
        SensorState s = {
            .dist_filt_mm = MAP_DIST_MIN_MM,
            .roll_deg     = MAP_VIBRATO_ROLL_MAX,   /* 45° → max depth */
            .pitch_deg    = 0.0f,
            .temp_c       = 22.0f,
        };
        AudioParams out = {0};
        mapping_update(&s, &out);

        ASSERT_NEAR("update: roll=45° → vibrato_depth=max_cents",
                    out.vibrato_depth_cents, MAP_VIBRATO_MAX_CENTS, 0.1f);
    }

    /* ── Scenario 6: pitch tilt → filter cutoff extremes ── */
    {
        mapping_init();
        SensorState s_lo = {
            .dist_filt_mm = MAP_DIST_MIN_MM,
            .roll_deg     = 0.0f,
            .pitch_deg    = MAP_PITCH_MIN_DEG,
            .temp_c       = 22.0f,
        };
        AudioParams out_lo = {0};
        mapping_update(&s_lo, &out_lo);
        ASSERT_NEAR("update: pitch_min → cutoff_min",
                    out_lo.cutoff_hz, MAP_CUTOFF_MIN_HZ, 1.0f);

        mapping_init();
        SensorState s_hi = {
            .dist_filt_mm = MAP_DIST_MIN_MM,
            .roll_deg     = 0.0f,
            .pitch_deg    = MAP_PITCH_MAX_DEG,
            .temp_c       = 22.0f,
        };
        AudioParams out_hi = {0};
        mapping_update(&s_hi, &out_hi);
        ASSERT_NEAR("update: pitch_max → cutoff_max",
                    out_hi.cutoff_hz, MAP_CUTOFF_MAX_HZ, 1.0f);
    }
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== mapping unit tests ===\n");

    test_dist_to_hz();
    test_pitch_to_cutoff();
    test_octave_gate();
    test_iir_step();
    test_mapping_update();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
