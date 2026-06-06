#include "audio_engine.h"
#include "audio_engine_sim.h"
#include "fx_detune.h"
#include "wav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE_HZ 32000u
#define TELEMETRY_HZ 30u
#define MAX_EVENTS 256
#define MAX_LINE 256

typedef enum {
    EV_MASTER,
    EV_CUTOFF,
    EV_Q,
    EV_BASE,
    EV_GAIN,
    EV_TEMP,
    EV_END
} EventKind;

typedef struct {
    double t_s;
    EventKind kind;
    int voice;
    float value;
} Event;

typedef struct {
    float temp_c;
    float detune_c;
    float synth_hz;
    double rms_accum;
    int rms_count;
    FILE *csv;
    FILE *json;
} TelemetryState;

static int parse_event(const char *line, Event *ev) {
    double t = 0.0;
    char cmd[32] = {0};
    if (!line || !ev) return 0;
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') return 0;
    if (sscanf(line, "%lf %31s", &t, cmd) != 2) return 0;

    ev->t_s = t;
    ev->voice = 0;
    ev->value = 0.0f;

    if (strcmp(cmd, "master") == 0) {
        ev->kind = EV_MASTER;
        return sscanf(line, "%lf %31s %f", &t, cmd, &ev->value) == 3;
    }
    if (strcmp(cmd, "cutoff") == 0) {
        ev->kind = EV_CUTOFF;
        return sscanf(line, "%lf %31s %f", &t, cmd, &ev->value) == 3;
    }
    if (strcmp(cmd, "q") == 0) {
        ev->kind = EV_Q;
        return sscanf(line, "%lf %31s %f", &t, cmd, &ev->value) == 3;
    }
    if (strcmp(cmd, "base") == 0) {
        ev->kind = EV_BASE;
        return sscanf(line, "%lf %31s %d %f", &t, cmd, &ev->voice, &ev->value) == 4;
    }
    if (strcmp(cmd, "gain") == 0) {
        ev->kind = EV_GAIN;
        return sscanf(line, "%lf %31s %d %f", &t, cmd, &ev->voice, &ev->value) == 4;
    }
    if (strcmp(cmd, "temp") == 0) {
        ev->kind = EV_TEMP;
        return sscanf(line, "%lf %31s %f", &t, cmd, &ev->value) == 3;
    }
    if (strcmp(cmd, "end") == 0) {
        ev->kind = EV_END;
        return 1;
    }
    return 0;
}

static int load_events(const char *path, Event *events, int max_events, double *duration_s) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    int count = 0;
    char line[MAX_LINE];
    double last_t = 0.0;
    while (fgets(line, sizeof line, fp)) {
        Event ev;
        if (!parse_event(line, &ev)) continue;
        if (count >= max_events) {
            fclose(fp);
            return -2;
        }
        events[count++] = ev;
        if (ev.t_s > last_t) last_t = ev.t_s;
        if (ev.kind == EV_END) *duration_s = ev.t_s;
    }
    fclose(fp);

    if (*duration_s <= 0.0) *duration_s = last_t + 1.0;
    return count;
}

static void apply_event(const Event *ev, TelemetryState *telemetry) {
    switch (ev->kind) {
        case EV_MASTER:
            audio_set_master_gain(ev->value);
            break;
        case EV_CUTOFF:
            filter_set_cutoff(ev->value);
            break;
        case EV_Q:
            filter_set_q(ev->value);
            break;
        case EV_BASE:
            fx_detune_set_base(ev->voice, ev->value);
            if (ev->voice == 0) telemetry->synth_hz = ev->value;
            break;
        case EV_GAIN:
            voice_set_gain(ev->voice, ev->value);
            break;
        case EV_TEMP:
            telemetry->temp_c = ev->value;
            break;
        case EV_END:
            break;
    }
}

static void emit_telemetry(TelemetryState *st, int frame_index) {
    float hz[MAX_VOICES];
    float gain[MAX_VOICES];
    for (int v = 0; v < MAX_VOICES; ++v) sim_get_voice(v, &hz[v], &gain[v]);

    float rms = 0.0f;
    if (st->rms_count > 0) rms = sqrtf((float)(st->rms_accum / (double)st->rms_count));
    float t_s = (float)frame_index / (float)TELEMETRY_HZ;
    int t_ms = (frame_index * 1000) / (int)TELEMETRY_HZ;

    fprintf(st->csv, "%.3f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.6f\n",
            t_s, st->temp_c, st->detune_c, hz[0], hz[1], hz[2], hz[3], rms);
    fprintf(st->json,
            "{\"t\":%d,\"temp_c\":%.2f,\"synth_hz\":%.3f,\"detune_c\":%.3f,\"voices\":[{\"hz\":%.3f,\"g\":%.3f},{\"hz\":%.3f,\"g\":%.3f},{\"hz\":%.3f,\"g\":%.3f},{\"hz\":%.3f,\"g\":%.3f}]}\n",
            t_ms, st->temp_c, st->synth_hz, st->detune_c,
            hz[0], gain[0], hz[1], gain[1], hz[2], gain[2], hz[3], gain[3]);

    st->rms_accum = 0.0;
    st->rms_count = 0;
}

int main(int argc, char **argv) {
    const char *timeline_path = argc > 1 ? argv[1] : "sim/timelines/s1_temp_sweep.txt";
    const char *wav_path = argc > 2 ? argv[2] : "out.wav";
    Event events[MAX_EVENTS];
    double duration_s = 0.0;
    int event_count = load_events(timeline_path, events, MAX_EVENTS, &duration_s);
    if (event_count < 0) {
        fprintf(stderr, "failed to load timeline %s (%d)\n", timeline_path, event_count);
        return 2;
    }

    int sample_count = (int)ceil(duration_s * (double)SAMPLE_RATE_HZ);
    if (sample_count <= 0) return 3;

    float *audio = calloc((size_t)sample_count, sizeof(float));
    if (!audio) return 4;

    audio_init(SAMPLE_RATE_HZ);
    fx_detune_init();
    TelemetryState telemetry = {
        .temp_c = 25.0f,
        .detune_c = 0.0f,
        .synth_hz = 0.0f,
        .rms_accum = 0.0,
        .rms_count = 0,
        .csv = fopen("out.csv", "w"),
        .json = fopen("telemetry.ndjson", "w")
    };
    if (!telemetry.csv || !telemetry.json) {
        free(audio);
        return 5;
    }
    fprintf(telemetry.csv, "t_s,temp_c,detune_c,v0_hz,v1_hz,v2_hz,v3_hz,rms\n");

    int next_event = 0;
    int next_frame_sample = 0;
    int frame_index = 0;
    int next_fx_sample = 0;
    int fx_index = 0;
    int samples_per_frame_floor = (int)(SAMPLE_RATE_HZ / TELEMETRY_HZ);

    for (int i = 0; i < sample_count; ++i) {
        double now_s = (double)i / (double)SAMPLE_RATE_HZ;
        while (next_event < event_count && events[next_event].t_s <= now_s) {
            apply_event(&events[next_event], &telemetry);
            next_event++;
        }

        if (i >= next_fx_sample) {
            fx_detune_update(telemetry.temp_c, 1.0f / (float)TELEMETRY_HZ);
            telemetry.detune_c = fx_detune_get_cents();
            fx_index++;
            next_fx_sample = (int)((double)fx_index * (double)SAMPLE_RATE_HZ / (double)TELEMETRY_HZ);
            if (next_fx_sample <= i) next_fx_sample = i + samples_per_frame_floor;
        }

        sim_render(&audio[i], 1);
        telemetry.rms_accum += (double)audio[i] * (double)audio[i];
        telemetry.rms_count++;

        if (i >= next_frame_sample) {
            emit_telemetry(&telemetry, frame_index);
            frame_index++;
            next_frame_sample = (int)((double)frame_index * (double)SAMPLE_RATE_HZ / (double)TELEMETRY_HZ);
            if (next_frame_sample <= i) next_frame_sample = i + samples_per_frame_floor;
        }
    }

    if (telemetry.rms_count > 0) emit_telemetry(&telemetry, frame_index);

    fclose(telemetry.csv);
    fclose(telemetry.json);

    if (!wav_write_mono16(wav_path, audio, (size_t)sample_count, SAMPLE_RATE_HZ)) {
        free(audio);
        return 6;
    }

    free(audio);
    return 0;
}
