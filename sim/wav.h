#ifndef TRACK_B_WAV_H
#define TRACK_B_WAV_H

#include <stddef.h>
#include <stdint.h>

int wav_write_mono16(const char *path, const float *samples, size_t count, uint32_t sample_rate_hz);

#endif
