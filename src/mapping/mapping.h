/**
 * @file mapping.h
 * @brief Sensor-value → audio-parameter mapping (pure math, no HAL).
 *
 * This module is intentionally free of any STM32 HAL or CMSIS headers so
 * that it can be compiled and unit-tested on a host (macOS / Linux) with
 * plain gcc.
 *
 * All constants are exposed as macros so tests can reference them directly.
 */

#ifndef MAPPING_H
#define MAPPING_H

#include <stdint.h>

/* ── Distance → frequency ─────────────────────────────────────────────── */
#define MAP_DIST_MIN_MM      20.0f
#define MAP_DIST_MAX_MM      4000.0f
#define MAP_FREQ_MIN_HZ      110.0f
#define MAP_FREQ_MAX_HZ      880.0f

/* ── Octave gate ──────────────────────────────────────────────────────── */
#define MAP_OCTAVE_ROLL_DEG  30.0f

/* ── IIR smoothing ────────────────────────────────────────────────────── */
#define MAP_IIR_ALPHA        0.1f

/* ── Tilt → filter cutoff (linear sweep) ─────────────────────────────── */
#define MAP_CUTOFF_MIN_HZ    400.0f
#define MAP_CUTOFF_MAX_HZ    4000.0f
#define MAP_PITCH_MIN_DEG    (-45.0f)
#define MAP_PITCH_MAX_DEG    45.0f

/* ── Vibrato ──────────────────────────────────────────────────────────── */
#define MAP_VIBRATO_MAX_CENTS  10.0f
#define MAP_VIBRATO_ROLL_MAX   45.0f

/* ── Servo ────────────────────────────────────────────────────────────── */
#define MAP_SERVO_MIN_DEG    0.0f
#define MAP_SERVO_MAX_DEG    180.0f

/* ── Clamp helper (works for float and integer types) ─────────────────── */
#define CLAMP(x, lo, hi)  ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))

/* ── Data types ───────────────────────────────────────────────────────── */

/**
 * Snapshot of all sensor readings passed into the mapping layer.
 * Populated by the main loop from sensor drivers.
 */
typedef struct {
    float dist_filt_mm;   /**< IIR-smoothed HC-SR04 distance [mm] */
    float roll_deg;       /**< Roll angle from MPU-6050 accel [°] */
    float pitch_deg;      /**< Pitch angle from MPU-6050 accel [°] */
    float temp_c;         /**< Ambient temperature from MCP9808 [°C] */
} SensorState;

/**
 * Audio parameters computed by mapping_update().
 * Consumed by audio_engine and servo driver.
 */
typedef struct {
    float synth_hz;              /**< Pre-smoothing pitch [Hz] */
    float filt_hz;               /**< Post-smoothing pitch driving DAC [Hz] */
    float cutoff_hz;             /**< FMAC filter cutoff [Hz] */
    float servo_deg;             /**< Servo angle [°] */
    float volume;                /**< Voice gain [0..1] */
    float vibrato_depth_cents;   /**< Vibrato depth [cents] */
} AudioParams;

/* ── Public API ───────────────────────────────────────────────────────── */

/**
 * Initialise internal IIR state.  Must be called once before
 * mapping_update().
 */
void mapping_init(void);

/**
 * Compute audio parameters from the current sensor snapshot.
 *
 * Applies:
 *   - distance → log-scale pitch (110–880 Hz)
 *   - octave gate (|roll| > 30° → pitch × 2)
 *   - 1st-order IIR smoothing (α = 0.1) on pitch
 *   - pitch_deg → filter cutoff (400–4000 Hz, linear)
 *   - |roll| → vibrato depth (0–10 cents)
 *   - distance → volume (closer = louder)
 *   - distance → servo angle (0–180°)
 *
 * @param s   Pointer to current sensor snapshot (read-only).
 * @param out Pointer to AudioParams struct to fill (write).
 */
void mapping_update(const SensorState *s, AudioParams *out);

/* ── Low-level helpers (exposed for unit tests) ───────────────────────── */

/**
 * Map distance [mm] → frequency [Hz] using log scale.
 * Clamps input to [MAP_DIST_MIN_MM, MAP_DIST_MAX_MM].
 */
float mapping_dist_to_hz(float dist_mm);

/**
 * Map pitch_deg → filter cutoff [Hz] (linear sweep).
 * Clamps input to [MAP_PITCH_MIN_DEG, MAP_PITCH_MAX_DEG].
 */
float mapping_pitch_to_cutoff(float pitch_deg);

/**
 * Octave gate: returns 1 if |roll_deg| > MAP_OCTAVE_ROLL_DEG, else 0.
 */
int mapping_octave_gate(float roll_deg);

/**
 * Apply one step of 1st-order IIR smoothing.
 * new_state = alpha * input + (1 - alpha) * prev_state
 */
float mapping_iir_step(float prev_state, float input, float alpha);

#endif /* MAPPING_H */
