/**
 * @file mapping.c
 * @brief Sensor-value → audio-parameter mapping implementation.
 *
 * Pure math — no HAL, no CMSIS, no hardware dependencies.
 * Compiles on host (gcc) and on-target (arm-none-eabi-gcc) identically.
 */

#include "mapping.h"

#include <math.h>   /* powf, fabsf, sqrtf */
#include <stdint.h>

/* ── Internal IIR state ───────────────────────────────────────────────── */
static float s_filt_hz = MAP_FREQ_MIN_HZ;   /* smoothed pitch */

/* ── Public: init ─────────────────────────────────────────────────────── */
void mapping_init(void)
{
    s_filt_hz = MAP_FREQ_MIN_HZ;
}

/* ── Low-level helpers ────────────────────────────────────────────────── */

float mapping_dist_to_hz(float dist_mm)
{
    /* Clamp input */
    float d = CLAMP(dist_mm, MAP_DIST_MIN_MM, MAP_DIST_MAX_MM);

    /* Normalise to [0, 1] */
    float t = (d - MAP_DIST_MIN_MM) / (MAP_DIST_MAX_MM - MAP_DIST_MIN_MM);
    t = CLAMP(t, 0.0f, 1.0f);

    /* Log-scale mapping: f = f_min * (f_max/f_min)^t
     * f_max/f_min = 880/110 = 8 = 2^3  →  powf(8, t)
     */
    float hz = MAP_FREQ_MIN_HZ * powf(8.0f, t);
    return CLAMP(hz, MAP_FREQ_MIN_HZ, MAP_FREQ_MAX_HZ);
}

float mapping_pitch_to_cutoff(float pitch_deg)
{
    float p = CLAMP(pitch_deg, MAP_PITCH_MIN_DEG, MAP_PITCH_MAX_DEG);
    float t = (p - MAP_PITCH_MIN_DEG) / (MAP_PITCH_MAX_DEG - MAP_PITCH_MIN_DEG);
    t = CLAMP(t, 0.0f, 1.0f);
    float hz = MAP_CUTOFF_MIN_HZ + t * (MAP_CUTOFF_MAX_HZ - MAP_CUTOFF_MIN_HZ);
    return CLAMP(hz, MAP_CUTOFF_MIN_HZ, MAP_CUTOFF_MAX_HZ);
}

int mapping_octave_gate(float roll_deg)
{
    return (roll_deg > MAP_OCTAVE_ROLL_DEG || roll_deg < -MAP_OCTAVE_ROLL_DEG) ? 1 : 0;
}

float mapping_iir_step(float prev_state, float input, float alpha)
{
    return alpha * input + (1.0f - alpha) * prev_state;
}

/* ── Public: update ───────────────────────────────────────────────────── */
void mapping_update(const SensorState *s, AudioParams *out)
{
    /* 1. Distance → raw pitch */
    float synth_hz = mapping_dist_to_hz(s->dist_filt_mm);

    /* 2. Octave gate */
    if (mapping_octave_gate(s->roll_deg)) {
        synth_hz *= 2.0f;
        synth_hz = CLAMP(synth_hz, MAP_FREQ_MIN_HZ, 1760.0f);
    }

    /* 3. IIR smoothing on pitch */
    s_filt_hz = mapping_iir_step(s_filt_hz, synth_hz, MAP_IIR_ALPHA);

    /* 4. Tilt → filter cutoff */
    float cutoff_hz = mapping_pitch_to_cutoff(s->pitch_deg);

    /* 5. Vibrato depth from |roll| */
    float depth = fabsf(s->roll_deg) * (MAP_VIBRATO_MAX_CENTS / MAP_VIBRATO_ROLL_MAX);
    float vibrato_depth_cents = CLAMP(depth, 0.0f, MAP_VIBRATO_MAX_CENTS);

    /* 6. Volume: closer → louder (inverse linear) */
    float d = CLAMP(s->dist_filt_mm, MAP_DIST_MIN_MM, MAP_DIST_MAX_MM);
    float volume = 1.0f - (d - MAP_DIST_MIN_MM) / (MAP_DIST_MAX_MM - MAP_DIST_MIN_MM);
    volume = CLAMP(volume, 0.0f, 1.0f);

    /* 7. Servo angle: farther → higher angle */
    float servo_deg = (d / MAP_DIST_MAX_MM) * MAP_SERVO_MAX_DEG;
    servo_deg = CLAMP(servo_deg, MAP_SERVO_MIN_DEG, MAP_SERVO_MAX_DEG);

    /* Write outputs */
    out->synth_hz            = synth_hz;
    out->filt_hz             = s_filt_hz;
    out->cutoff_hz           = cutoff_hz;
    out->servo_deg           = servo_deg;
    out->volume              = volume;
    out->vibrato_depth_cents = vibrato_depth_cents;
}
