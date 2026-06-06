#include "fx_detune.h"

#include "audio_engine.h"

#include <math.h>

#define DETUNE_SLOPE_C_PER_DEGC 15.0f
#define DETUNE_LIMIT_CENTS 75.0f
#define DETUNE_TAU_S 0.4f

static float g_base_hz[MAX_VOICES];
static float g_ref_temp_c;
static int g_has_ref;
static float g_cents;

static float clamp_float(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static void push_all(void) {
    float factor = exp2f(g_cents / 1200.0f);
    for (int v = 0; v < MAX_VOICES; ++v) {
        if (g_base_hz[v] > 0.0f) voice_set_freq(v, g_base_hz[v] * factor);
    }
}

void fx_detune_init(void) {
    for (int v = 0; v < MAX_VOICES; ++v) g_base_hz[v] = 0.0f;
    g_ref_temp_c = 0.0f;
    g_has_ref = 0;
    g_cents = 0.0f;
}

void fx_detune_set_base(int v, float hz) {
    if (v < 0 || v >= MAX_VOICES) return;
    g_base_hz[v] = hz > 0.0f ? hz : 0.0f;
    if (g_base_hz[v] > 0.0f) {
        float factor = exp2f(g_cents / 1200.0f);
        voice_set_freq(v, g_base_hz[v] * factor);
    }
}

void fx_detune_update(float temp_c, float dt_s) {
    if (!isfinite(temp_c)) {
        push_all();
        return;
    }

    if (!g_has_ref) {
        g_ref_temp_c = temp_c;
        g_has_ref = 1;
        g_cents = 0.0f;
        push_all();
        return;
    }

    float target = clamp_float(
        DETUNE_SLOPE_C_PER_DEGC * (temp_c - g_ref_temp_c),
        -DETUNE_LIMIT_CENTS,
        DETUNE_LIMIT_CENTS
    );

    if (dt_s > 0.0f && isfinite(dt_s)) {
        float a = 1.0f - expf(-dt_s / DETUNE_TAU_S);
        g_cents += a * (target - g_cents);
    }

    push_all();
}

float fx_detune_get_cents(void) {
    return g_cents;
}
