/**
 * @file audio_engine.c
 * @brief Audio synthesis engine — Milestone 1 implementation.
 *
 * M1: TIM6 IRQ-driven DAC output.
 *   - 32-sample sine wavetable (12-bit, pre-computed at init).
 *   - TIM6 fires at filt_hz * 32 Hz; ISR outputs one sample per interrupt.
 *   - Single voice (voice 0). Voices 1–3 reserved for M2.
 *   - No DMA, no FMAC (added in M2).
 *
 * Hardware:
 *   DAC1_OUT1 = PA4 (⚠ NEVER PA5 — that is LD2 LED)
 *   TIM6 clock = APB1 timer clock = 170 MHz
 *   TIM6 prescaler = 169 → 1 MHz timer clock
 *   TIM6 ARR = (1 000 000 / (hz * 32)) - 1
 *
 * ISR budget: ~10 cycles (wavetable lookup + multiply + DAC write).
 * At 16 kHz × 32 = 512 kHz ISR rate: 170 MHz / 512 kHz ≈ 332 cycles/period.
 * ISR is well within budget.
 *
 * NVIC: TIM6_DAC_IRQn at priority 0 (highest — audio must not be preempted).
 */

#include "audio_engine.h"
#include <math.h>    /* sinf, M_PI — FPU accelerated */
#include <stddef.h>

/* ── TIM6 prescaler → 1 MHz timer clock ──────────────────────────────── */
#define TIM6_PSC        169u   /* 170 MHz / (169+1) = 1 MHz */

/* ── Wavetable ────────────────────────────────────────────────────────── */

/**
 * 32-sample sine wavetable, 12-bit unsigned (0–4095).
 * Mid-scale = 2048 (silence). Computed once in audio_init().
 * Not in .dma_buf — M1 does not use DMA for audio.
 */
static uint16_t s_wavetable[AUDIO_WAVETABLE_N];

/* ── Voice state ──────────────────────────────────────────────────────── */

typedef struct {
    volatile uint32_t phase;      /**< Current wavetable index [0, N) */
    volatile uint32_t gain_q15;   /**< Gain * 32767 (q15 fixed-point) */
    volatile uint8_t  active;     /**< 1 = voice is playing */
} Voice;

static Voice s_voices[MAX_VOICES];

/* ── Master gain ──────────────────────────────────────────────────────── */

static volatile uint32_t s_master_gain_q15 = 32767u;  /**< 1.0 in q15 */

/* ── TIM6 handle (stored at init) ────────────────────────────────────── */

static TIM_HandleTypeDef *s_htim6 = NULL;
static DAC_HandleTypeDef *s_hdac1 = NULL;

/* ── Internal: compute TIM6 ARR for a given ISR rate ─────────────────── */

/**
 * Set TIM6 ARR so that the ISR fires at `isr_hz` Hz.
 * Timer clock = 1 MHz (after PSC=169).
 * ARR = (1 000 000 / isr_hz) - 1, clamped to [0, 0xFFFF].
 */
static void set_tim6_rate(float isr_hz)
{
    if (isr_hz < 1.0f) isr_hz = 1.0f;
    uint32_t arr = (uint32_t)(1000000.0f / isr_hz);
    if (arr < 1u)      arr = 1u;
    if (arr > 0xFFFFu) arr = 0xFFFFu;
    TIM6->ARR = arr - 1u;
}

/* ── Public: audio_init ───────────────────────────────────────────────── */

void audio_init(uint32_t sample_rate_hz)
{
    (void)sample_rate_hz;   /* Reserved for M2 */

    /* Retrieve HAL handles from main.c via extern declarations.
     * These are declared extern here to avoid including main.h. */
    extern TIM_HandleTypeDef htim6;
    extern DAC_HandleTypeDef hdac1;
    s_htim6 = &htim6;
    s_hdac1 = &hdac1;

    /* Step 1: generate 32-sample sine wavetable (12-bit, 0–4095) */
    for (uint32_t i = 0; i < AUDIO_WAVETABLE_N; i++) {
        float angle = 2.0f * (float)M_PI * (float)i / (float)AUDIO_WAVETABLE_N;
        /* Map [-1, +1] → [0, 4095] */
        s_wavetable[i] = (uint16_t)((sinf(angle) + 1.0f) * 2047.5f);
    }

    /* Step 2: initialise voice state */
    for (int v = 0; v < (int)MAX_VOICES; v++) {
        s_voices[v].phase      = 0;
        s_voices[v].gain_q15   = 32767u;   /* 1.0 */
        s_voices[v].active     = 0;
    }
    s_master_gain_q15 = 32767u;

    /* Step 3: configure TIM6 prescaler and default ARR (440 Hz × 32) */
    TIM6->PSC = TIM6_PSC;
    set_tim6_rate(440.0f * (float)AUDIO_WAVETABLE_N);

    /* Step 4: start DAC1_OUT1 in software-trigger mode (M1 — no DMA) */
    HAL_DAC_Start(s_hdac1, DAC_CHANNEL_1);
    HAL_DAC_SetValue(s_hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, AUDIO_DAC_MIDSCALE);

    /* Step 5: start TIM6 in interrupt mode */
    HAL_TIM_Base_Start_IT(s_htim6);

    /* Activate voice 0 at 440 Hz */
    s_voices[0].active = 1;
}

/* ── Public: voice_set_freq ───────────────────────────────────────────── */

void voice_set_freq(int v, float hz)
{
    if (v < 0 || v >= (int)MAX_VOICES) return;

    /* Clamp to audible range */
    if (hz < 20.0f)    hz = 20.0f;
    if (hz > 16000.0f) hz = 16000.0f;

    /* In M1, only voice 0 drives TIM6 rate */
    if (v == 0) {
        set_tim6_rate(hz * (float)AUDIO_WAVETABLE_N);
    }

    s_voices[v].active = 1;
}

/* ── Public: voice_set_gain ───────────────────────────────────────────── */

void voice_set_gain(int v, float g)
{
    if (v < 0 || v >= (int)MAX_VOICES) return;
    if (g < 0.0f) g = 0.0f;
    if (g > 1.0f) g = 1.0f;
    s_voices[v].gain_q15 = (uint32_t)(g * 32767.0f);
}

/* ── Public: filter_set_cutoff ────────────────────────────────────────── */

void filter_set_cutoff(float hz)
{
    (void)hz;   /* No-op in M1 — FMAC added in M2 */
}

/* ── Public: filter_set_q ─────────────────────────────────────────────── */

void filter_set_q(float q)
{
    (void)q;    /* No-op in M1 */
}

/* ── Public: audio_set_master_gain ───────────────────────────────────── */

void audio_set_master_gain(float g)
{
    if (g < 0.0f) g = 0.0f;
    if (g > 1.0f) g = 1.0f;
    s_master_gain_q15 = (uint32_t)(g * 32767.0f);
}

/* ── Public: audio_tim6_irq ───────────────────────────────────────────── */

/**
 * Called from TIM6_DAC_IRQHandler() in stm32g4xx_it.c.
 *
 * Advances voice 0 phase, looks up wavetable, applies gain, writes DAC.
 * Executes in ~10 cycles on Cortex-M4F.
 */
void audio_tim6_irq(void)
{
    /* Clear TIM6 update interrupt flag */
    TIM6->SR = ~TIM_SR_UIF;

    if (!s_voices[0].active) {
        /* Silence: output mid-scale */
        DAC1->DHR12R1 = AUDIO_DAC_MIDSCALE;
        return;
    }

    /* Advance phase counter (wraps at AUDIO_WAVETABLE_N) */
    uint32_t phase = s_voices[0].phase + 1u;
    if (phase >= AUDIO_WAVETABLE_N) phase = 0;
    s_voices[0].phase = phase;

    /* Wavetable lookup: 12-bit unsigned [0, 4095] */
    uint16_t sample_u = s_wavetable[phase];

    /* Convert to signed [-2048, 2047] for gain scaling */
    int32_t sample = (int32_t)sample_u - (int32_t)AUDIO_DAC_MIDSCALE;

    /* Apply voice gain (q15 multiply) */
    sample = (sample * (int32_t)s_voices[0].gain_q15) >> 15;

    /* Apply master gain (q15 multiply) */
    sample = (sample * (int32_t)s_master_gain_q15) >> 15;

    /* Clamp and convert back to 12-bit unsigned */
    if (sample < -2048) sample = -2048;
    if (sample >  2047) sample =  2047;

    DAC1->DHR12R1 = (uint32_t)(sample + (int32_t)AUDIO_DAC_MIDSCALE);
}
