#ifndef AUDIO_ENGINE_SIM_H
#define AUDIO_ENGINE_SIM_H

#include <stdint.h>
#include "audio_engine.h"

void sim_reset(void);
void sim_render(float *out, int nframes);
void sim_get_voice(int v, float *hz, float *gain);
uint32_t sim_sample_rate_hz(void);

#endif
