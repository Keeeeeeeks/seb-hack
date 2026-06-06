# Track B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Track B host-simulated showpiece layer for Air-Synth: S1 temp-detune is the committed deliverable; S2 sequencer/arp and S3 polyphony are stretch items gated behind S1 green.

**Architecture:** Track B never edits `src/core/*` or audio-engine internals. All sound-control work talks through the frozen `audio_engine.h`; `fx_detune` is the pitch front-end that owns base frequencies, applies global detune, and forwards adjusted frequencies to `voice_set_freq`. A host simulator implements the frozen API for native tests and emits WAV/CSV/NDJSON artifacts; frontend showpiece panels replay the same NDJSON that the board will eventually stream.

**Tech Stack:** Plain C11, native clang on macOS, CMake/CTest/Ninja for host sim/tests, Vite + React + TypeScript for showpiece panels, Web Serial-compatible telemetry shape, no backend, no hardware dependency until Phase 6 integration.

---

## Scope and gate discipline

- **Current branch:** work is continuing on `keeks-tracka-appA-test` per user instruction. The original `track-b` branch also contains the spec commits. Stage only Track B files: `include/`, `sim/`, `fx_detune/`, `sequencer/`, `polyphony/`, `tests/`, `src/showpiece/`, and `docs/track-b-*`.
- **Do not stage or modify:** `docs/track-a-*`, `src/core/*`, any Track A audio-engine implementation, pin maps, or firmware internals.
- **Phase 0:** host simulator first, no review gate, commit when it builds and produces artifacts.
- **Phase 3:** write S1 tests before S1 production code, commit tests, then stop at **REVIEW GATE 3**.
- **Phase 4:** implement S1, run tests, regenerate golden WAV/CSV/NDJSON, commit, then stop for **S2 go/no-go**.
- **Phase 5:** implement S1 frontend only after S1 code is green, commit, then stop at **REVIEW GATE 4**.
- **Stretch:** S2 only after S1 green + explicit go; S3 only after S2 green + explicit go.

---

## File map

### Shared seam
- Create: `include/audio_engine.h` — verbatim frozen API from `SEB.MD`; no extra functions.

### Host simulator and harness
- Create: `sim/CMakeLists.txt` — isolated Track B host build; avoids colliding with Track A firmware root build.
- Create: `sim/audio_engine_sim.h` — sim-only helpers: render, reset, voice introspection.
- Create: `sim/audio_engine_sim.c` — frozen API implementation: 4 sine voices + RBJ biquad LP + master gain.
- Create: `sim/wav.h`, `sim/wav.c` — 32 kHz / 16-bit / mono WAV writer.
- Create: `sim/harness.c` — timeline driver; writes `out.wav`, `out.csv`, `telemetry.ndjson`.
- Create: `sim/timelines/s1_temp_sweep.txt` — deterministic S1 demo and golden timeline.

### S1 module
- Create: `fx_detune/fx_detune.h`
- Create: `fx_detune/fx_detune.c`

### Tests
- Create: `tests/check.h` — tiny assertions.
- Create: `tests/goertzel.h`, `tests/goertzel.c` — golden frequency detector.
- Create: `tests/test_detune.c` — S1 unit tests.
- Create: `tests/test_golden.c` — run harness + analyze artifacts.

### Frontend S1 panel
- Create: `src/showpiece/telemetry/types.ts`
- Create: `src/showpiece/telemetry/useFixtureSource.ts`
- Create: `src/showpiece/telemetry/IfField.tsx`
- Create: `src/showpiece/TempDetune/CentsGauge.tsx`
- Create: `src/showpiece/TempDetune/TempDetune.tsx`
- Create: `src/showpiece/TempDetune/index.ts`
- Create: `src/showpiece/dev/fixture.ts`
- Create: `src/showpiece/dev/ShowpieceDevApp.tsx`

### Stretch modules
- Create later if approved: `sequencer/sequencer.{h,c}` and `tests/test_sequencer.c`.
- Create later if approved: `polyphony/poly.{h,c}` and `tests/test_polyphony.c`.
- Create later if approved: `src/showpiece/SequencerGrid/*`, `src/showpiece/VoicesPanel/*`.

---

## Track A data needed at integration

S1 needs only existing Track A data and one call-site redirect:
- `temp_c` from MCP9808 in the ~30 Hz sensor/telemetry loop.
- `dt_s` for that update loop. If Track A does not expose a measured delta, use `1.0f / 30.0f` for S1.
- Track A's voice-0 intended pitch (`synth_hz` or the variable passed to `voice_set_freq(0, hz)`). Redirect that one call to `fx_detune_set_base(0, hz)`.
- Telemetry emit adds `detune_c: fx_detune_get_cents()`.
- If `status.mcp != 0` or `temp_c` is not finite, Track A still calls `fx_detune_update(temp_c, dt_s)`; `fx_detune` holds the last smoothed cents and does not recapture baseline.

S2 integration needs:
- Roll/pitch from existing telemetry for scale/pattern selection.
- JSON command path to dispatch `{"cmd":"seq",...}`.
- **TIM7** for clock: PSC=169 (1 MHz), ARR from `bpm * 24 / 60`, NVIC priority 8, ISR sets a flag only.

S3 integration needs:
- No new hardware. It consumes note events from S2/chord mode and emits `voices[]` telemetry.

---

# Implementation tasks

## Task 0: Guardrails and branch hygiene

**Files:** none modified.

- [ ] **Step 1: Confirm branch and dirty state**

Run:
```bash
git branch --show-current
git status --short
```
Expected:
```text
keeks-tracka-appA-test
?? docs/track-a-agentlog.md
?? docs/track-a-plan.md
?? docs/track-a-spec.md
```
Track A files may exist as untracked files from the parallel session. Leave them alone.

- [ ] **Step 2: Add a shell alias for host build commands in the session only**

Run commands explicitly in later steps; do not commit shell aliases. Use:
```bash
cmake -S sim -B build-track-b -G Ninja
cmake --build build-track-b
ctest --test-dir build-track-b --output-on-failure
```
Expected: before code exists, configure fails. After Task 1, configure succeeds.

---

## Task 1: Materialize frozen API + host build scaffold

**Files:**
- Create: `include/audio_engine.h`
- Create: `sim/CMakeLists.txt`
- Create: `sim/audio_engine_sim.h`
- Create: `sim/audio_engine_sim.c` (stub compiles; full rendering in Task 2)

- [ ] **Step 1: Write the frozen header**

Create `include/audio_engine.h` exactly:
```c
#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <stdint.h>

#define MAX_VOICES 4

void  audio_init(uint32_t sample_rate_hz);
void  voice_set_freq(int v, float hz);
void  voice_set_gain(int v, float g);
void  filter_set_cutoff(float hz);
void  filter_set_q(float q);
void  audio_set_master_gain(float g);

#endif
```

- [ ] **Step 2: Write sim-only helper header**

Create `sim/audio_engine_sim.h`:
```c
#ifndef AUDIO_ENGINE_SIM_H
#define AUDIO_ENGINE_SIM_H

#include <stdint.h>
#include "audio_engine.h"

void sim_reset(void);
void sim_render(float *out, int nframes);
void sim_get_voice(int v, float *hz, float *gain);
uint32_t sim_sample_rate_hz(void);

#endif
```

- [ ] **Step 3: Write stub simulator**

Create `sim/audio_engine_sim.c`:
```c
#include "audio_engine_sim.h"

static uint32_t g_sample_rate_hz = 32000;
static float g_voice_hz[MAX_VOICES];
static float g_voice_gain[MAX_VOICES];
static float g_cutoff_hz = 8000.0f;
static float g_q = 0.707f;
static float g_master_gain = 1.0f;

void sim_reset(void) {
    g_sample_rate_hz = 32000;
    g_cutoff_hz = 8000.0f;
    g_q = 0.707f;
    g_master_gain = 1.0f;
    for (int v = 0; v < MAX_VOICES; ++v) {
        g_voice_hz[v] = 0.0f;
        g_voice_gain[v] = 0.0f;
    }
}

void audio_init(uint32_t sample_rate_hz) {
    sim_reset();
    if (sample_rate_hz > 0) g_sample_rate_hz = sample_rate_hz;
}

void voice_set_freq(int v, float hz) {
    if (v < 0 || v >= MAX_VOICES) return;
    g_voice_hz[v] = hz > 0.0f ? hz : 0.0f;
}

void voice_set_gain(int v, float g) {
    if (v < 0 || v >= MAX_VOICES) return;
    if (g < 0.0f) g = 0.0f;
    if (g > 1.0f) g = 1.0f;
    g_voice_gain[v] = g;
}

void filter_set_cutoff(float hz) { g_cutoff_hz = hz; }
void filter_set_q(float q) { g_q = q; }
void audio_set_master_gain(float g) { g_master_gain = g; }

void sim_get_voice(int v, float *hz, float *gain) {
    if (hz) *hz = 0.0f;
    if (gain) *gain = 0.0f;
    if (v < 0 || v >= MAX_VOICES) return;
    if (hz) *hz = g_voice_hz[v];
    if (gain) *gain = g_voice_gain[v];
}

uint32_t sim_sample_rate_hz(void) { return g_sample_rate_hz; }

void sim_render(float *out, int nframes) {
    (void)g_cutoff_hz;
    (void)g_q;
    (void)g_master_gain;
    if (!out || nframes <= 0) return;
    for (int i = 0; i < nframes; ++i) out[i] = 0.0f;
}
```

- [ ] **Step 4: Write isolated host CMake**

Create `sim/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(track_b_host_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../fx_detune
  ${CMAKE_CURRENT_SOURCE_DIR}/../tests
)

add_library(audio_engine_sim audio_engine_sim.c)

enable_testing()
```

- [ ] **Step 5: Configure and build**

Run:
```bash
cmake -S sim -B build-track-b -G Ninja
cmake --build build-track-b
```
Expected: configure succeeds; build creates `libaudio_engine_sim.a`.

- [ ] **Step 6: Commit scaffold**

Run:
```bash
git add include/audio_engine.h sim/CMakeLists.txt sim/audio_engine_sim.h sim/audio_engine_sim.c
git commit -m "Track B host scaffold: frozen audio API and simulator target"
```

---

## Task 2: Phase 0 host simulator: audio render + WAV/CSV/NDJSON harness

**Files:**
- Modify: `sim/audio_engine_sim.c`
- Create: `sim/wav.h`, `sim/wav.c`
- Create: `sim/harness.c`
- Create: `sim/timelines/s1_temp_sweep.txt`
- Modify: `sim/CMakeLists.txt`

- [ ] **Step 1: Replace simulator stub with real sine + biquad render**

Replace `sim/audio_engine_sim.c` with the full implementation pattern:
```c
#include "audio_engine_sim.h"
#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float b0,b1,b2,a1,a2,x1,x2,y1,y2; } Biquad;

static uint32_t g_sample_rate_hz = 32000;
static double g_phase[MAX_VOICES];
static float g_voice_hz[MAX_VOICES];
static float g_voice_gain[MAX_VOICES];
static float g_cutoff_hz = 8000.0f;
static float g_q = 0.707f;
static float g_master_gain = 1.0f;
static Biquad g_lpf;

static float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static void biquad_update(void) {
    float fc = clampf(g_cutoff_hz, 20.0f, (float)g_sample_rate_hz * 0.45f);
    float q = clampf(g_q, 0.1f, 10.0f);
    float w0 = 2.0f * (float)M_PI * fc / (float)g_sample_rate_hz;
    float c = cosf(w0);
    float s = sinf(w0);
    float alpha = s / (2.0f * q);
    float a0 = 1.0f + alpha;
    g_lpf.b0 = ((1.0f - c) * 0.5f) / a0;
    g_lpf.b1 = (1.0f - c) / a0;
    g_lpf.b2 = g_lpf.b0;
    g_lpf.a1 = (-2.0f * c) / a0;
    g_lpf.a2 = (1.0f - alpha) / a0;
}

static float biquad_tick(Biquad *f, float x) {
    float y = f->b0*x + f->b1*f->x1 + f->b2*f->x2 - f->a1*f->y1 - f->a2*f->y2;
    f->x2 = f->x1; f->x1 = x; f->y2 = f->y1; f->y1 = y;
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
    if (sample_rate_hz > 0) g_sample_rate_hz = sample_rate_hz;
    biquad_update();
}

void voice_set_freq(int v, float hz) {
    if (v < 0 || v >= MAX_VOICES) return;
    g_voice_hz[v] = hz > 0.0f ? hz : 0.0f;
}

void voice_set_gain(int v, float g) {
    if (v < 0 || v >= MAX_VOICES) return;
    g_voice_gain[v] = clampf(g, 0.0f, 1.0f);
}

void filter_set_cutoff(float hz) { g_cutoff_hz = hz; biquad_update(); }
void filter_set_q(float q) { g_q = q; biquad_update(); }
void audio_set_master_gain(float g) { g_master_gain = clampf(g, 0.0f, 1.0f); }

void sim_get_voice(int v, float *hz, float *gain) {
    if (hz) *hz = 0.0f;
    if (gain) *gain = 0.0f;
    if (v < 0 || v >= MAX_VOICES) return;
    if (hz) *hz = g_voice_hz[v];
    if (gain) *gain = g_voice_gain[v];
}

uint32_t sim_sample_rate_hz(void) { return g_sample_rate_hz; }

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
        float y = biquad_tick(&g_lpf, mix) * g_master_gain;
        out[i] = clampf(y, -1.0f, 1.0f);
    }
}
```

- [ ] **Step 2: Write WAV writer**

Create `sim/wav.h`:
```c
#ifndef TRACK_B_WAV_H
#define TRACK_B_WAV_H

#include <stddef.h>
#include <stdint.h>

int wav_write_mono16(const char *path, const float *samples, size_t count, uint32_t sample_rate_hz);

#endif
```

Create `sim/wav.c`:
```c
#include "wav.h"
#include <stdio.h>
#include <stdint.h>

typedef struct {
    char riff[4]; uint32_t chunk_size; char wave[4]; char fmt[4];
    uint32_t subchunk1_size; uint16_t audio_format; uint16_t num_channels;
    uint32_t sample_rate; uint32_t byte_rate; uint16_t block_align;
    uint16_t bits_per_sample; char data[4]; uint32_t data_bytes;
} WavHeader;

static int16_t f32_to_i16(float x) {
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return (int16_t)(x * 32767.0f);
}

int wav_write_mono16(const char *path, const float *samples, size_t count, uint32_t sample_rate_hz) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    WavHeader h = {{'R','I','F','F'}, 36u + (uint32_t)(count * 2u), {'W','A','V','E'}, {'f','m','t',' '},
        16u, 1u, 1u, sample_rate_hz, sample_rate_hz * 2u, 2u, 16u, {'d','a','t','a'}, (uint32_t)(count * 2u)};
    if (fwrite(&h, sizeof h, 1, fp) != 1) { fclose(fp); return 0; }
    for (size_t i = 0; i < count; ++i) {
        int16_t s = f32_to_i16(samples[i]);
        if (fwrite(&s, sizeof s, 1, fp) != 1) { fclose(fp); return 0; }
    }
    fclose(fp);
    return 1;
}
```

- [ ] **Step 3: Write deterministic timeline**

Create `sim/timelines/s1_temp_sweep.txt`:
```text
# t_seconds command args
0.0 master 0.80
0.0 cutoff 8000
0.0 q 0.707
0.0 base 0 440.00
0.0 gain 0 0.20
0.0 temp 25.00
2.0 temp 30.00
4.0 temp 20.00
6.0 end
```

- [ ] **Step 4: Write minimal harness (S1 calls are inert until fx_detune exists)**

Create `sim/harness.c` with direct engine driving for Phase 0. S1 integration is added in Task 4.
```c
#include "audio_engine.h"
#include "audio_engine_sim.h"
#include "wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 32000u
#define FRAME_HZ 30u
#define MAX_SECONDS 8u

static float g_temp_c = 25.0f;

static void apply_line(char *line) {
    double t = 0.0; char cmd[32] = {0};
    if (sscanf(line, "%lf %31s", &t, cmd) != 2) return;
    if (cmd[0] == '#') return;
    if (strcmp(cmd, "master") == 0) { float v; if (sscanf(line, "%lf %31s %f", &t, cmd, &v)==3) audio_set_master_gain(v); }
    else if (strcmp(cmd, "cutoff") == 0) { float v; if (sscanf(line, "%lf %31s %f", &t, cmd, &v)==3) filter_set_cutoff(v); }
    else if (strcmp(cmd, "q") == 0) { float v; if (sscanf(line, "%lf %31s %f", &t, cmd, &v)==3) filter_set_q(v); }
    else if (strcmp(cmd, "base") == 0) { int voice; float hz; if (sscanf(line, "%lf %31s %d %f", &t, cmd, &voice, &hz)==4) voice_set_freq(voice, hz); }
    else if (strcmp(cmd, "gain") == 0) { int voice; float gain; if (sscanf(line, "%lf %31s %d %f", &t, cmd, &voice, &gain)==4) voice_set_gain(voice, gain); }
    else if (strcmp(cmd, "temp") == 0) { float v; if (sscanf(line, "%lf %31s %f", &t, cmd, &v)==3) g_temp_c = v; }
}

int main(int argc, char **argv) {
    const char *timeline = argc > 1 ? argv[1] : "sim/timelines/s1_temp_sweep.txt";
    const char *out_wav = argc > 2 ? argv[2] : "out.wav";
    FILE *fp = fopen(timeline, "r");
    if (!fp) { fprintf(stderr, "missing timeline: %s\n", timeline); return 2; }

    audio_init(SAMPLE_RATE);
    float *audio = calloc(SAMPLE_RATE * MAX_SECONDS, sizeof(float));
    if (!audio) return 3;

    char line[256];
    while (fgets(line, sizeof line, fp)) apply_line(line);
    fclose(fp);

    sim_render(audio, SAMPLE_RATE * MAX_SECONDS);
    if (!wav_write_mono16(out_wav, audio, SAMPLE_RATE * MAX_SECONDS, SAMPLE_RATE)) return 4;

    FILE *csv = fopen("out.csv", "w");
    FILE *json = fopen("telemetry.ndjson", "w");
    if (!csv || !json) return 5;
    fprintf(csv, "t_s,temp_c,detune_c,v0_hz,v1_hz,v2_hz,v3_hz,rms\n");
    for (int frame = 0; frame < (int)(MAX_SECONDS * FRAME_HZ); ++frame) {
        float hz0=0, g0=0, hz1=0, g1=0, hz2=0, g2=0, hz3=0, g3=0;
        sim_get_voice(0, &hz0, &g0); sim_get_voice(1, &hz1, &g1); sim_get_voice(2, &hz2, &g2); sim_get_voice(3, &hz3, &g3);
        float t = (float)frame / (float)FRAME_HZ;
        fprintf(csv, "%.3f,%.2f,0.0,%.3f,%.3f,%.3f,%.3f,0.0\n", t, g_temp_c, hz0, hz1, hz2, hz3);
        fprintf(json, "{\"t\":%d,\"temp_c\":%.2f,\"synth_hz\":%.3f,\"detune_c\":0.0,\"voices\":[{\"hz\":%.3f,\"g\":%.3f}]}\n", frame * 1000 / (int)FRAME_HZ, g_temp_c, hz0, hz0, g0);
    }
    fclose(csv); fclose(json); free(audio);
    return 0;
}
```

- [ ] **Step 5: Update CMake for WAV + harness**

Append to `sim/CMakeLists.txt`:
```cmake
add_library(wav wav.c)
add_executable(track_b_harness harness.c)
target_link_libraries(track_b_harness PRIVATE audio_engine_sim wav m)
```
On macOS, if `-lm` is not needed and causes an issue, remove `m` and rebuild.

- [ ] **Step 6: Build and run harness**

Run:
```bash
cmake -S sim -B build-track-b -G Ninja
cmake --build build-track-b
./build-track-b/track_b_harness sim/timelines/s1_temp_sweep.txt out.wav
ls -l out.wav out.csv telemetry.ndjson
```
Expected: non-empty `out.wav`, `out.csv`, `telemetry.ndjson`.

- [ ] **Step 7: Commit Phase 0 host sim**

Run:
```bash
git add sim/CMakeLists.txt sim/audio_engine_sim.c sim/wav.h sim/wav.c sim/harness.c sim/timelines/s1_temp_sweep.txt out.wav out.csv telemetry.ndjson
git commit -m "Track B Phase 0: host audio simulator and timeline harness"
```

---

## Task 3: Phase 3 S1 tests before S1 code

**Files:**
- Create: `tests/check.h`
- Create: `tests/goertzel.h`, `tests/goertzel.c`
- Create: `tests/test_detune.c`
- Create: `tests/test_golden.c`
- Modify: `sim/CMakeLists.txt`
- Create stub only: `fx_detune/fx_detune.h`, `fx_detune/fx_detune.c` so tests compile-fail or assertion-fail in a controlled way.

- [ ] **Step 1: Write assertion helpers**

Create `tests/check.h`:
```c
#ifndef TRACK_B_CHECK_H
#define TRACK_B_CHECK_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define CHECK_NEAR(actual, expected, eps) do { \
    float _a = (float)(actual); float _e = (float)(expected); float _d = fabsf(_a - _e); \
    if (_d > (float)(eps)) { fprintf(stderr, "CHECK_NEAR failed %s:%d: actual=%f expected=%f eps=%f\n", __FILE__, __LINE__, _a, _e, (float)(eps)); exit(1); } \
} while (0)

#endif
```

- [ ] **Step 2: Write Goertzel helper**

Create `tests/goertzel.h`:
```c
#ifndef TRACK_B_GOERTZEL_H
#define TRACK_B_GOERTZEL_H
float goertzel_power(const float *buf, int n, float freq_hz, float sample_rate_hz);
float cents_error(float measured_hz, float target_hz);
#endif
```

Create `tests/goertzel.c`:
```c
#include "goertzel.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float goertzel_power(const float *buf, int n, float freq_hz, float sample_rate_hz) {
    float k = freq_hz * (float)n / sample_rate_hz;
    float w = 2.0f * (float)M_PI * k / (float)n;
    float coeff = 2.0f * cosf(w);
    float s1 = 0.0f, s2 = 0.0f;
    for (int i = 0; i < n; ++i) {
        float s = buf[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    float re = s1 - s2 * cosf(w);
    float im = s2 * sinf(w);
    return re * re + im * im;
}

float cents_error(float measured_hz, float target_hz) {
    return 1200.0f * log2f(measured_hz / target_hz);
}
```

- [ ] **Step 3: Write S1 header contract**

Create `fx_detune/fx_detune.h`:
```c
#ifndef FX_DETUNE_H
#define FX_DETUNE_H

void  fx_detune_init(void);
void  fx_detune_set_base(int v, float hz);
void  fx_detune_update(float temp_c, float dt_s);
float fx_detune_get_cents(void);

#endif
```

Create `fx_detune/fx_detune.c` as a stub that intentionally fails behavior tests:
```c
#include "fx_detune.h"
void  fx_detune_init(void) {}
void  fx_detune_set_base(int v, float hz) { (void)v; (void)hz; }
void  fx_detune_update(float temp_c, float dt_s) { (void)temp_c; (void)dt_s; }
float fx_detune_get_cents(void) { return 999.0f; }
```

- [ ] **Step 4: Write unit tests**

Create `tests/test_detune.c`:
```c
#include "check.h"
#include "audio_engine.h"
#include "audio_engine_sim.h"
#include "fx_detune.h"
#include <math.h>

static float ratio_for_cents(float cents) { return exp2f(cents / 1200.0f); }

static void baseline_capture(void) {
    audio_init(32000);
    fx_detune_init();
    fx_detune_set_base(0, 440.0f);
    voice_set_gain(0, 0.2f);
    fx_detune_update(25.0f, 1.0f / 30.0f);
    CHECK_NEAR(fx_detune_get_cents(), 0.0f, 0.5f);
}

static void curve_and_clamp(void) {
    audio_init(32000);
    fx_detune_init();
    fx_detune_update(25.0f, 0.033333f);
    for (int i = 0; i < 60; ++i) fx_detune_update(30.0f, 0.033333f);
    CHECK(fx_detune_get_cents() > 70.0f);
    CHECK(fx_detune_get_cents() <= 75.0f);
    for (int i = 0; i < 120; ++i) fx_detune_update(40.0f, 0.033333f);
    CHECK_NEAR(fx_detune_get_cents(), 75.0f, 0.5f);
}

static void all_voices_detuned(void) {
    audio_init(32000);
    fx_detune_init();
    const float base[4] = {220.0f, 277.18f, 329.63f, 440.0f};
    for (int v = 0; v < MAX_VOICES; ++v) {
        fx_detune_set_base(v, base[v]);
        voice_set_gain(v, 0.15f);
    }
    fx_detune_update(25.0f, 0.033333f);
    for (int i = 0; i < 180; ++i) fx_detune_update(30.0f, 0.033333f);
    float cents = fx_detune_get_cents();
    CHECK_NEAR(cents, 75.0f, 0.5f);
    for (int v = 0; v < MAX_VOICES; ++v) {
        float hz = 0.0f, gain = 0.0f;
        sim_get_voice(v, &hz, &gain);
        CHECK_NEAR(hz, base[v] * ratio_for_cents(cents), 0.1f);
    }
}

static void invalid_temp_holds(void) {
    audio_init(32000);
    fx_detune_init();
    fx_detune_update(NAN, 0.033333f);
    CHECK_NEAR(fx_detune_get_cents(), 0.0f, 0.1f);
    fx_detune_update(25.0f, 0.033333f);
    for (int i = 0; i < 180; ++i) fx_detune_update(30.0f, 0.033333f);
    float before = fx_detune_get_cents();
    fx_detune_update(NAN, 0.033333f);
    CHECK_NEAR(fx_detune_get_cents(), before, 0.1f);
}

int main(void) {
    baseline_capture();
    curve_and_clamp();
    all_voices_detuned();
    invalid_temp_holds();
    return 0;
}
```

- [ ] **Step 5: Wire tests into CMake**

Append to `sim/CMakeLists.txt`:
```cmake
add_library(fx_detune ../fx_detune/fx_detune.c)
target_link_libraries(fx_detune PRIVATE audio_engine_sim m)

add_library(goertzel ../tests/goertzel.c)

add_executable(test_detune ../tests/test_detune.c)
target_link_libraries(test_detune PRIVATE fx_detune audio_engine_sim m)
add_test(NAME test_detune COMMAND test_detune)
```

- [ ] **Step 6: Run tests and verify failure**

Run:
```bash
cmake -S sim -B build-track-b -G Ninja
cmake --build build-track-b
ctest --test-dir build-track-b --output-on-failure
```
Expected: build succeeds, `test_detune` fails because `fx_detune_get_cents()` returns `999.0` and voices are not retuned.

- [ ] **Step 7: Commit failing S1 tests and stop at REVIEW GATE 3**

Run:
```bash
git add tests/check.h tests/goertzel.h tests/goertzel.c tests/test_detune.c fx_detune/fx_detune.h fx_detune/fx_detune.c sim/CMakeLists.txt
git commit -m "Track B Phase 3: S1 detune tests before implementation"
```
Then stop and post: tests compile and fail for the expected reason. Await **approved** before Task 4.

---

## Task 4: Phase 4 S1 implementation against sim

**Files:**
- Modify: `fx_detune/fx_detune.c`
- Modify: `sim/harness.c` to route base/temp through `fx_detune` and emit real `detune_c`.
- Modify: `tests/test_golden.c` if added in the gate-approved test pass.

- [ ] **Step 1: Replace S1 stub with implementation**

Replace `fx_detune/fx_detune.c`:
```c
#include "fx_detune.h"
#include "audio_engine.h"
#include <math.h>

#define DETUNE_SLOPE_C_PER_DEGC 15.0f
#define DETUNE_LIMIT_CENTS 75.0f
#define DETUNE_TAU_S 0.4f

static float g_base_hz[MAX_VOICES];
static float g_ref_temp_c = 0.0f;
static int g_has_ref = 0;
static float g_cents = 0.0f;

static float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static int finitef_local(float x) { return isfinite(x); }

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
    if (!finitef_local(temp_c)) {
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
    float target = clampf(DETUNE_SLOPE_C_PER_DEGC * (temp_c - g_ref_temp_c), -DETUNE_LIMIT_CENTS, DETUNE_LIMIT_CENTS);
    if (dt_s > 0.0f && finitef_local(dt_s)) {
        float a = 1.0f - expf(-dt_s / DETUNE_TAU_S);
        g_cents += a * (target - g_cents);
    }
    push_all();
}

float fx_detune_get_cents(void) { return g_cents; }
```

- [ ] **Step 2: Run unit tests**

Run:
```bash
cmake --build build-track-b
ctest --test-dir build-track-b --output-on-failure
```
Expected: `test_detune` passes.

- [ ] **Step 3: Update harness to use `fx_detune`**

In `sim/harness.c`:
- include `fx_detune.h`.
- replace `voice_set_freq(voice, hz)` in `base` command with `fx_detune_set_base(voice, hz)`.
- call `fx_detune_init()` after `audio_init(SAMPLE_RATE)`.
- after `temp` commands, call `fx_detune_update(g_temp_c, 1.0f / 30.0f)` repeatedly during frame emission or render in 30 Hz chunks so smoothing evolves over time.
- emit `fx_detune_get_cents()` instead of `0.0` for CSV/NDJSON.

The final frame emission line must follow this shape:
```c
float detune_c = fx_detune_get_cents();
fprintf(csv, "%.3f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.6f\n", t, g_temp_c, detune_c, hz0, hz1, hz2, hz3, rms);
fprintf(json, "{\"t\":%d,\"temp_c\":%.2f,\"synth_hz\":%.3f,\"detune_c\":%.3f,\"voices\":[{\"hz\":%.3f,\"g\":%.3f}]}\n", frame * 1000 / (int)FRAME_HZ, g_temp_c, hz0, detune_c, hz0, g0);
```

- [ ] **Step 4: Add golden test if not already added during Gate 3**

Create `tests/test_golden.c` with this acceptance intent:
```c
#include "check.h"
#include "goertzel.h"
#include "audio_engine.h"
#include "audio_engine_sim.h"
#include "fx_detune.h"
#include <math.h>
#include <stdlib.h>

static float ratio(float cents) { return exp2f(cents / 1200.0f); }

static void chord_detuned(void) {
    audio_init(32000);
    fx_detune_init();
    const float base[4] = {220.0f, 277.18f, 329.63f, 440.0f};
    for (int v = 0; v < MAX_VOICES; ++v) { fx_detune_set_base(v, base[v]); voice_set_gain(v, 0.12f); }
    fx_detune_update(25.0f, 1.0f/30.0f);
    for (int i = 0; i < 180; ++i) fx_detune_update(30.0f, 1.0f/30.0f);
    const int n = 32000;
    float *buf = calloc((size_t)n, sizeof(float));
    CHECK(buf != 0);
    sim_render(buf, n);
    float cents = fx_detune_get_cents();
    CHECK_NEAR(cents, 75.0f, 0.5f);
    for (int v = 0; v < MAX_VOICES; ++v) {
        float target = base[v] * ratio(cents);
        float p0 = goertzel_power(buf, n, target, 32000.0f);
        float p_low = goertzel_power(buf, n, target / ratio(10.0f), 32000.0f);
        float p_high = goertzel_power(buf, n, target * ratio(10.0f), 32000.0f);
        CHECK(p0 > p_low);
        CHECK(p0 > p_high);
    }
    free(buf);
}

int main(void) { chord_detuned(); return 0; }
```
Append CMake:
```cmake
add_executable(test_golden ../tests/test_golden.c)
target_link_libraries(test_golden PRIVATE fx_detune audio_engine_sim goertzel m)
add_test(NAME test_golden COMMAND test_golden)
```

- [ ] **Step 5: Run full S1 host tests and regenerate artifacts**

Run:
```bash
cmake --build build-track-b
ctest --test-dir build-track-b --output-on-failure
./build-track-b/track_b_harness sim/timelines/s1_temp_sweep.txt out.wav
python3 - <<'PY'
from pathlib import Path
print('out.wav bytes', Path('out.wav').stat().st_size)
print('csv lines', sum(1 for _ in open('out.csv')))
print('ndjson lines', sum(1 for _ in open('telemetry.ndjson')))
print('last telemetry', list(open('telemetry.ndjson'))[-1].strip())
PY
```
Expected: tests pass; artifacts non-empty; telemetry contains `detune_c`, and `out.csv` line count is greater than 1.

- [ ] **Step 6: Commit S1 implementation and artifacts**

Run:
```bash
git add fx_detune/fx_detune.c sim/harness.c tests/test_golden.c sim/CMakeLists.txt out.wav out.csv telemetry.ndjson
git commit -m "Track B S1: temp-detune FX with host tests and golden artifacts"
```
Then post a summary: test names passing, final `detune_c` observed, artifact sizes, and exact commit hash. Stop for **S2 go/no-go** before touching `sequencer/`.

---

## Task 5: Phase 5 S1 showpiece frontend (`src/showpiece/*` only)

**Files:**
- Create: `src/showpiece/telemetry/types.ts`
- Create: `src/showpiece/telemetry/useFixtureSource.ts`
- Create: `src/showpiece/telemetry/IfField.tsx`
- Create: `src/showpiece/TempDetune/CentsGauge.tsx`
- Create: `src/showpiece/TempDetune/TempDetune.tsx`
- Create: `src/showpiece/TempDetune/index.ts`
- Create: `src/showpiece/dev/fixture.ts`
- Create: `src/showpiece/dev/ShowpieceDevApp.tsx`

- [ ] **Step 1: Define telemetry types**

Create `src/showpiece/telemetry/types.ts`:
```ts
export type VoiceTelemetry = { hz: number; g: number }

export type SeqTelemetry = {
  bpm: number
  step: number
  len: number
  on: boolean
}

export type TelemetryFrame = {
  t?: number
  temp_c?: number
  synth_hz?: number
  detune_c?: number
  voices?: VoiceTelemetry[]
  seq?: SeqTelemetry
  roll?: number
  pitch?: number
}
```

- [ ] **Step 2: Create replay source**

Create `src/showpiece/telemetry/useFixtureSource.ts`:
```ts
import { useEffect, useMemo, useRef, useState } from 'react'
import type { TelemetryFrame } from './types'

export function useFixtureSource(frames: TelemetryFrame[], hz = 30): TelemetryFrame | null {
  const safeFrames = useMemo(() => frames.filter(Boolean), [frames])
  const [frame, setFrame] = useState<TelemetryFrame | null>(safeFrames[0] ?? null)
  const idx = useRef(0)

  useEffect(() => {
    if (safeFrames.length === 0) return
    const id = window.setInterval(() => {
      setFrame(safeFrames[idx.current % safeFrames.length])
      idx.current += 1
    }, 1000 / hz)
    return () => window.clearInterval(id)
  }, [safeFrames, hz])

  return frame
}
```

- [ ] **Step 3: Create field guard**

Create `src/showpiece/telemetry/IfField.tsx`:
```tsx
import type { ReactNode } from 'react'
import type { TelemetryFrame } from './types'

type Props<K extends keyof TelemetryFrame> = {
  frame: TelemetryFrame | null
  field: K
  children: ReactNode
}

export function IfField<K extends keyof TelemetryFrame>({ frame, field, children }: Props<K>) {
  if (!frame || !(field in frame)) return null
  return <>{children}</>
}
```

- [ ] **Step 4: Create center-zero SVG gauge**

Create `src/showpiece/TempDetune/CentsGauge.tsx`:
```tsx
const LIMIT = 75
const ARC = 220
const CX = 90
const CY = 88
const R = 64

type CentsGaugeProps = { cents: number }

function clamp(n: number, lo: number, hi: number) {
  return Math.min(hi, Math.max(lo, n))
}

function polar(deg: number, radius = R) {
  const rad = (deg * Math.PI) / 180
  return { x: CX + radius * Math.cos(rad), y: CY + radius * Math.sin(rad) }
}

function arcPath(startDeg: number, endDeg: number) {
  const start = polar(startDeg)
  const end = polar(endDeg)
  const largeArc = Math.abs(endDeg - startDeg) > 180 ? 1 : 0
  return `M ${start.x} ${start.y} A ${R} ${R} 0 ${largeArc} 1 ${end.x} ${end.y}`
}

export function CentsGauge({ cents }: CentsGaugeProps) {
  const shown = clamp(cents, -LIMIT, LIMIT)
  const start = 160
  const end = start + ARC
  const needleDeg = start + ((shown + LIMIT) / (LIMIT * 2)) * ARC
  const needle = polar(needleDeg, R - 12)
  const zero = polar(start + ARC / 2, R)
  const zeroInner = polar(start + ARC / 2, R - 12)

  return (
    <svg viewBox="0 0 180 130" role="img" aria-label={`Detune ${shown.toFixed(1)} cents`}>
      <path d={arcPath(start, end)} fill="none" stroke="#233047" strokeWidth="10" strokeLinecap="round" />
      <line x1={zeroInner.x} y1={zeroInner.y} x2={zero.x} y2={zero.y} stroke="#7dd3fc" strokeWidth="3" />
      <line x1={CX} y1={CY} x2={needle.x} y2={needle.y} stroke="#f97316" strokeWidth="3" strokeLinecap="round" />
      <circle cx={CX} cy={CY} r="5" fill="#f97316" />
      <text x={CX} y="116" textAnchor="middle" fontSize="18" fill="#f8fafc">{shown.toFixed(1)}¢</text>
      <text x="28" y="124" fontSize="10" fill="#94a3b8">-75¢</text>
      <text x="128" y="124" fontSize="10" fill="#94a3b8">+75¢</text>
    </svg>
  )
}
```

- [ ] **Step 5: Create TempDetune panel**

Create `src/showpiece/TempDetune/TempDetune.tsx`:
```tsx
import type { TelemetryFrame } from '../telemetry/types'
import { CentsGauge } from './CentsGauge'

type Props = { frame: TelemetryFrame }

export function TempDetune({ frame }: Props) {
  const detune = typeof frame.detune_c === 'number' ? frame.detune_c : 0
  const temp = typeof frame.temp_c === 'number' ? frame.temp_c.toFixed(1) : '—'
  const synth = typeof frame.synth_hz === 'number' ? frame.synth_hz.toFixed(1) : '—'

  return (
    <section className="showpiece-card temp-detune">
      <header>
        <p className="eyebrow">S1 committed</p>
        <h2>Room Detune</h2>
        <p>The room controls the instrument: warmer air bends every voice sharper.</p>
      </header>
      <CentsGauge cents={detune} />
      <dl>
        <div><dt>Temp</dt><dd>{temp} °C</dd></div>
        <div><dt>Detune</dt><dd>{detune.toFixed(1)} cents</dd></div>
        <div><dt>Voice 0</dt><dd>{synth} Hz</dd></div>
      </dl>
    </section>
  )
}
```

Create `src/showpiece/TempDetune/index.ts`:
```ts
export { TempDetune } from './TempDetune'
export { CentsGauge } from './CentsGauge'
```

- [ ] **Step 6: Create replay fixture and dev app**

Create `src/showpiece/dev/fixture.ts`:
```ts
import type { TelemetryFrame } from '../telemetry/types'

export const fixtureFrames: TelemetryFrame[] = Array.from({ length: 180 }, (_, i) => {
  const t = i * (1000 / 30)
  const temp_c = i < 60 ? 25 + (i / 60) * 5 : i < 120 ? 30 - ((i - 60) / 60) * 10 : 20 + ((i - 120) / 60) * 5
  const raw = Math.max(-75, Math.min(75, (temp_c - 25) * 15))
  return { t, temp_c, synth_hz: 440 * Math.pow(2, raw / 1200), detune_c: raw }
})
```

Create `src/showpiece/dev/ShowpieceDevApp.tsx`:
```tsx
import { TempDetune } from '../TempDetune'
import { IfField } from '../telemetry/IfField'
import { useFixtureSource } from '../telemetry/useFixtureSource'
import { fixtureFrames } from './fixture'

export function ShowpieceDevApp() {
  const frame = useFixtureSource(fixtureFrames, 30)
  return (
    <main style={{ minHeight: '100vh', background: '#020617', color: '#f8fafc', padding: 24 }}>
      <IfField frame={frame} field="detune_c">
        {frame && <TempDetune frame={frame} />}
      </IfField>
    </main>
  )
}
```

- [ ] **Step 7: Build or typecheck if a frontend project exists**

Run:
```bash
ls package.json && npm run build
```
Expected: if `package.json` exists, build passes. If it does not exist yet, record: "FE source written; Track A app shell not present in this branch yet." Do not scaffold a full app unless Track A has not created one and the gate explicitly approves it.

- [ ] **Step 8: Commit S1 frontend**

Run:
```bash
git add src/showpiece
git commit -m "Track B showpiece: TempDetune replay panel"
```
Then stop at **REVIEW GATE 4**.

---

## Task 6: S2 sequencer / arp stretch implementation (only after S1 green + explicit go)

**Files:**
- Create: `sequencer/sequencer.h`, `sequencer/sequencer.c`
- Create: `tests/test_sequencer.c`
- Modify: `sim/harness.c`, `sim/CMakeLists.txt`
- Create: `src/showpiece/SequencerGrid/SequencerGrid.tsx`, `src/showpiece/SequencerGrid/index.ts`

- [ ] **Step 1: Public API**

Create `sequencer/sequencer.h`:
```c
#ifndef SEQUENCER_H
#define SEQUENCER_H

typedef enum { SEQ_SCALE_MAJOR = 0, SEQ_SCALE_MINOR_PENT = 1, SEQ_SCALE_CHROMATIC = 2 } SeqScale;

typedef struct {
    float bpm;
    int step;
    int len;
    int on;
    SeqScale scale;
} SeqState;

void sequencer_init(void);
void sequencer_set(float bpm, int on, SeqScale scale);
void sequencer_tick_ppqn(void);
SeqState sequencer_get_state(void);

#endif
```

- [ ] **Step 2: Tests first**

Create `tests/test_sequencer.c` with checks for BPM ticks, step wrap at 16, scale-to-Hz mapping, and composition with `fx_detune_set_base`. Wire it into CTest and confirm it fails before implementation.

- [ ] **Step 3: Implementation rule**

`sequencer_tick_ppqn()` increments a 24 PPQN counter; every 6 PPQN ticks advances one 16th-note step at the configured BPM. On each step, compute note Hz from root A3=220 and call `fx_detune_set_base(0, hz)`. The module never calls engine internals.

- [ ] **Step 4: Host + telemetry**

Extend harness timeline with:
```text
0.0 seq on 120 major
```
Emit:
```json
"seq":{"bpm":120,"step":3,"len":16,"on":true}
```

- [ ] **Step 5: TIM7 integration note**

At Track A integration, use TIM7 with PSC=169 and:
```c
float tick_hz = bpm * 24.0f / 60.0f;
uint32_t arr = (uint32_t)((1000000.0f / tick_hz) + 0.5f) - 1u;
```
TIM7 ISR only sets `sequencer_tick_flag = 1`; super-loop calls `sequencer_tick_ppqn()`.

- [ ] **Step 6: Commit and stop before S3**

Run tests, regenerate artifacts, commit:
```bash
git add sequencer tests/test_sequencer.c sim/harness.c sim/CMakeLists.txt src/showpiece/SequencerGrid
git commit -m "Track B stretch S2: sequencer over detune front-end"
```
Stop for **S3 go/no-go**.

---

## Task 7: S3 polyphony stretch implementation (only after S2 green + explicit go)

**Files:**
- Create: `polyphony/poly.h`, `polyphony/poly.c`
- Create: `tests/test_polyphony.c`
- Modify: `sim/harness.c`, `sim/CMakeLists.txt`
- Create: `src/showpiece/VoicesPanel/VoicesPanel.tsx`, `src/showpiece/VoicesPanel/index.ts`

- [ ] **Step 1: Public API**

Create `polyphony/poly.h`:
```c
#ifndef POLYPHONY_H
#define POLYPHONY_H

void poly_init(void);
int  poly_note_on(float hz, float gain);
void poly_note_off(int voice);
void poly_get_voice(int voice, float *hz, float *gain, int *active);

#endif
```

- [ ] **Step 2: Tests first**

`tests/test_polyphony.c` verifies: first 4 note-ons allocate voices 0..3; 5th steals oldest; note-off frees; `poly_get_voice` matches `voices[]`; all pitch writes go through `fx_detune_set_base`.

- [ ] **Step 3: Implementation rule**

Use arrays `active[MAX_VOICES]`, `hz[MAX_VOICES]`, `gain[MAX_VOICES]`, `age[MAX_VOICES]`. Free voice wins; otherwise steal smallest `age`. On note-on: set active, call `fx_detune_set_base(v, hz)`, call `voice_set_gain(v, gain)`. On note-off: mark inactive, call `fx_detune_set_base(v, 0)`, call `voice_set_gain(v, 0)`.

- [ ] **Step 4: Commit**

Run tests, regenerate artifacts, commit:
```bash
git add polyphony tests/test_polyphony.c sim/harness.c sim/CMakeLists.txt src/showpiece/VoicesPanel
git commit -m "Track B stretch S3: four-voice polyphony allocator"
```

---

## Task 8: Integration + QA plan for T-45 window

**Files modified during integration on Track A only after merge is approved:** Track A mapping/telemetry files, not engine internals.

- [ ] **Step 1: Pre-merge checks**

Run on Track A branch:
```bash
git status --short
cmake -B build -G Ninja
cmake --build build
```
Expected: Track A build green. If Track A is not green, do not merge Track B.

- [ ] **Step 2: Merge Track B**

Run:
```bash
git merge --no-ff keeks-tracka-appA-test
```
If `include/audio_engine.h` duplicates Track A's header, keep the frozen signature exactly once and adjust include paths. Do not change the function list.

- [ ] **Step 3: S1 wiring**

Track A mapping call site changes from:
```c
voice_set_freq(0, synth_hz);
```
to:
```c
fx_detune_set_base(0, synth_hz);
```
Track A sensor loop adds:
```c
fx_detune_update(temp_c, 1.0f / 30.0f);
```
Telemetry adds:
```json
"detune_c": <fx_detune_get_cents()>
```

- [ ] **Step 4: Validation**

Run:
```bash
cmake --build build
ctest --test-dir build-track-b --output-on-failure
```
Then run Track A BIST over serial and verify: no pin changes, no PA4/PA5 conflict, MCP9808 status green, audio still runs, `detune_c` appears in NDJSON.

- [ ] **Step 5: Rollback rule**

If not green by T-30:
```bash
git revert -m 1 <merge-commit>
```
Or, if S1 is green but S2/S3 are not, revert only the S2/S3 commits and ship S1.

---

## Self-review against `docs/track-b-spec.md`

- **Spec coverage:** Phase 0 host sim covered in Tasks 1-2; S1 committed behavior and tests covered in Tasks 3-4; frontend S1 covered in Task 5; S2/S3 stretch covered in Tasks 6-7; integration/rollback covered in Task 8.
- **Contract fidelity:** The plan creates the frozen `audio_engine.h` exactly and adds no functions. `fx_detune` calls public `voice_set_freq`; S2/S3 also route through `fx_detune` or public gain functions. `src/core/*` is not touched.
- **Test-first discipline:** S1 tests are written and committed before S1 implementation. Stretch modules also require tests before implementation.
- **No hidden dependency:** S1 host sim + detune + TempDetune panel stand alone. S2/S3 are gated and independently revertable.
- **Placeholder scan:** No unresolved placeholder markers, no unresolved file paths, no unspecified timers. TIM7, PSC, ARR, NVIC priority, telemetry fields, and rollback commands are concrete.

---

## REVIEW GATE 2 checklist

Before implementation begins, confirm:
- `docs/track-b-plan.md` is committed.
- `docs/track-b-agentlog.md` notes Phase 2 plan creation and current branch choice.
- Track A untracked files remain unmodified/uncommitted by Track B.
- User replies **approved** for REVIEW GATE 2.

After approval, execution options:
1. **Subagent-driven (recommended):** dispatch a fresh worker per task, review outputs between tasks.
2. **Inline execution:** execute tasks in this session with strict checkpoint stops.

Given the hackathon time pressure, choose **inline execution** unless parallel workers are already coordinated into separate worktrees.
