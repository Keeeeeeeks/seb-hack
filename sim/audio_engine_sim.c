#include "audio_engine_sim.h"

#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float x1;
    float x2;
    float y1;
    float y2;
} Biquad;

static uint32_t g_sample_rate_hz = 32000;
static double g_phase[MAX_VOICES];
static float g_voice_hz[MAX_VOICES];
static float g_voice_gain[MAX_VOICES];
static float g_cutoff_hz = 8000.0f;
static float g_q = 0.707f;
static float g_master_gain = 1.0f;
static Biquad g_lpf;

static float clamp_float(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static void biquad_update(void) {
    float nyquist_safe = (float)g_sample_rate_hz * 0.45f;
    float fc = clamp_float(g_cutoff_hz, 20.0f, nyquist_safe);
    float q = clamp_float(g_q, 0.1f, 10.0f);
    float w0 = 2.0f * (float)M_PI * fc / (float)g_sample_rate_hz;
    float cosw = cosf(w0);
    float sinw = sinf(w0);
    float alpha = sinw / (2.0f * q);
    float a0 = 1.0f + alpha;

    g_lpf.b0 = ((1.0f - cosw) * 0.5f) / a0;
    g_lpf.b1 = (1.0f - cosw) / a0;
    g_lpf.b2 = g_lpf.b0;
    g_lpf.a1 = (-2.0f * cosw) / a0;
    g_lpf.a2 = (1.0f - alpha) / a0;
}

static float biquad_tick(Biquad *f, float x) {
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

void sim_reset(void) {
    g_sample_rate_hz = 32000;
    g_cutoff_hz = 8000.0f;
    g_q = 0.707f;
    g_master_gain = 1.0f;
    for (int v = 0; v < MAX_VOICES; ++v) {
        g_phase[v] = 0.0;
        g_voice_hz[v] = 0.0f;
        g_voice_gain[v] = 0.0f;
    }
    g_lpf = (Biquad){0};
    biquad_update();
}

void audio_init(uint32_t sample_rate_hz) {
    sim_reset();
    if (sample_rate_hz > 0u) g_sample_rate_hz = sample_rate_hz;
    biquad_update();
}

void voice_set_freq(int v, float hz) {
    if (v < 0 || v >= MAX_VOICES) return;
    g_voice_hz[v] = hz > 0.0f ? hz : 0.0f;
}

void voice_set_gain(int v, float g) {
    if (v < 0 || v >= MAX_VOICES) return;
    g_voice_gain[v] = clamp_float(g, 0.0f, 1.0f);
}

void filter_set_cutoff(float hz) {
    g_cutoff_hz = hz;
    biquad_update();
}

void filter_set_q(float q) {
    g_q = q;
    biquad_update();
}

void audio_set_master_gain(float g) {
    g_master_gain = clamp_float(g, 0.0f, 1.0f);
}

void sim_get_voice(int v, float *hz, float *gain) {
    if (hz) *hz = 0.0f;
    if (gain) *gain = 0.0f;
    if (v < 0 || v >= MAX_VOICES) return;
    if (hz) *hz = g_voice_hz[v];
    if (gain) *gain = g_voice_gain[v];
}

uint32_t sim_sample_rate_hz(void) {
    return g_sample_rate_hz;
}

void sim_render(float *out, int nframes) {
    if (!out || nframes <= 0) return;

    for (int i = 0; i < nframes; ++i) {
        float mix = 0.0f;
        for (int v = 0; v < MAX_VOICES; ++v) {
            if (g_voice_hz[v] <= 0.0f || g_voice_gain[v] <= 0.0f) continue;
            mix += g_voice_gain[v] * sinf(2.0f * (float)M_PI * (float)g_phase[v]);
            g_phase[v] += (double)g_voice_hz[v] / (double)g_sample_rate_hz;
            while (g_phase[v] >= 1.0) g_phase[v] -= 1.0;
        }
        out[i] = clamp_float(biquad_tick(&g_lpf, mix) * g_master_gain, -1.0f, 1.0f);
    }
}
