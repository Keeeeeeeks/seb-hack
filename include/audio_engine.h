#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <stdint.h>

#define MAX_VOICES 4

void audio_init(uint32_t sample_rate_hz);
void voice_set_freq(int v, float hz);
void voice_set_gain(int v, float g);
void filter_set_cutoff(float hz);
void filter_set_q(float q);
void audio_set_master_gain(float g);

#endif
