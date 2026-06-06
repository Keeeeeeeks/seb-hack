/**
 * @file audio_engine.h
 * @brief Audio synthesis engine — public API (frozen, shared contract).
 *
 * Milestone 1 ("Reliable Hum"):
 *   TIM6 IRQ-driven DAC output. 32-sample sine wavetable computed at init.
 *   No DMA, no FMAC. Sample rate = filt_hz * 32 (updated on voice_set_freq).
 *
 * Milestone 2 ("CORDIC Synth"):
 *   CORDIC wavetable (256 samples) + DAC DMA (ping-pong) + FMAC IIR filter.
 *   Same public API — only the implementation changes.
 *
 * Target: STM32G474RET6 (NUCLEO-G474RE)
 * DAC:    DAC1_OUT1 on PA4 (⚠ NEVER PA5 — that is LD2 LED)
 * Timer:  TIM6 TRGO → DAC trigger (M2) / TIM6 IRQ → DAC write (M1)
 *
 * NVIC priority: audio DMA = 0 (highest). TIM6 IRQ = 0 in M1.
 *
 * Thread safety:
 *   voice_set_freq / voice_set_gain / audio_set_master_gain may be called
 *   from main loop. They write 32-bit values that are atomic on Cortex-M4.
 *   filter_set_cutoff / filter_set_q must be called from main loop only
 *   (they recompute FMAC coefficients — not ISR-safe in M2).
 */

#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <stdint.h>
#include "stm32g4xx_hal.h"

/* ── Constants ────────────────────────────────────────────────────────── */

#define MAX_VOICES          4u     /**< Maximum simultaneous voices */
#define AUDIO_WAVETABLE_N   32u    /**< Samples per sine cycle (M1) */
#define AUDIO_DAC_MIDSCALE  2048u  /**< 12-bit DAC mid-scale (silence) */

/* ── Public API ───────────────────────────────────────────────────────── */

/**
 * Initialise the audio engine.
 *
 * M1 sequence:
 *  1. Generate 32-sample sine wavetable (12-bit, 0–4095).
 *  2. Configure TIM6: PSC=169 (1 MHz), ARR computed for default 440 Hz × 32.
 *  3. Start TIM6 in interrupt mode (HAL_TIM_Base_Start_IT).
 *  4. Set DAC1_OUT1 to mid-scale (silence).
 *
 * Must be called after MX_TIM6_Init() and MX_DAC1_Init() in main.c.
 *
 * @param sample_rate_hz  Ignored in M1 (TIM6 ARR is set per-voice frequency).
 *                        Reserved for M2 DMA mode (e.g. 32000 Hz).
 */
void audio_init(uint32_t sample_rate_hz);

/**
 * Set the frequency of voice `v`.
 *
 * Updates TIM6 ARR so that the ISR fires at `hz * AUDIO_WAVETABLE_N` Hz,
 * producing one complete sine cycle per `hz` seconds.
 *
 * In M1 only voice 0 is used (single-voice). Voices 1–3 are reserved for M2.
 *
 * Frequency is clamped to [20 Hz, 16000 Hz].
 * 32-bit ARR write is atomic on Cortex-M4 — no critical section needed.
 *
 * @param v   Voice index (0 = primary voice in M1).
 * @param hz  Desired frequency in Hz.
 */
void voice_set_freq(int v, float hz);

/**
 * Set the gain of voice `v`.
 *
 * @param v  Voice index.
 * @param g  Gain in [0.0, 1.0]. Values outside range are clamped.
 */
void voice_set_gain(int v, float g);

/**
 * Set the low-pass filter cutoff frequency (M2 — FMAC IIR).
 *
 * In M1 this is a no-op (no filter implemented).
 *
 * @param hz  Cutoff frequency in Hz (clamped to [20, Fs/2]).
 */
void filter_set_cutoff(float hz);

/**
 * Set the filter Q factor (M2 — FMAC IIR).
 *
 * In M1 this is a no-op.
 *
 * @param q  Q factor (clamped to [0.1, 10.0]).
 */
void filter_set_q(float q);

/**
 * Set the master output gain.
 *
 * @param g  Gain in [0.0, 1.0]. Values outside range are clamped.
 */
void audio_set_master_gain(float g);

/**
 * TIM6 period-elapsed IRQ handler (M1 audio ISR).
 *
 * Must be called from TIM6_DAC_IRQHandler() in stm32g4xx_it.c:
 *   void TIM6_DAC_IRQHandler(void) { audio_tim6_irq(); }
 *
 * Advances the phase counter for voice 0, looks up the wavetable,
 * applies gain, and writes the result to DAC1->DHR12R1.
 *
 * Executes in ~10 cycles on Cortex-M4F (wavetable lookup + multiply + store).
 * Must complete before the next TIM6 period (≥ 1/(16000*32) = 1.95 µs at max freq).
 */
void audio_tim6_irq(void);

#endif /* AUDIO_ENGINE_H */
