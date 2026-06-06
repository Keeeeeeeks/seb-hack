#include "wav.h"

#include <stdint.h>
#include <stdio.h>

static void write_u16_le(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void write_u32_le(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
    dst[2] = (uint8_t)((v >> 16) & 0xffu);
    dst[3] = (uint8_t)((v >> 24) & 0xffu);
}

static int16_t float_to_i16(float x) {
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return (int16_t)(x * 32767.0f);
}

int wav_write_mono16(const char *path, const float *samples, size_t count, uint32_t sample_rate_hz) {
    if (!path || !samples) return 0;

    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    uint32_t data_bytes = (uint32_t)(count * 2u);
    uint8_t header[44] = {0};
    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    write_u32_le(&header[4], 36u + data_bytes);
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    write_u32_le(&header[16], 16u);
    write_u16_le(&header[20], 1u);
    write_u16_le(&header[22], 1u);
    write_u32_le(&header[24], sample_rate_hz);
    write_u32_le(&header[28], sample_rate_hz * 2u);
    write_u16_le(&header[32], 2u);
    write_u16_le(&header[34], 16u);
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    write_u32_le(&header[40], data_bytes);

    if (fwrite(header, sizeof header, 1, fp) != 1) {
        fclose(fp);
        return 0;
    }

    for (size_t i = 0; i < count; ++i) {
        int16_t s = float_to_i16(samples[i]);
        uint8_t bytes[2];
        write_u16_le(bytes, (uint16_t)s);
        if (fwrite(bytes, sizeof bytes, 1, fp) != 1) {
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return 1;
}
