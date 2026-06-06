# Track B — Showpiece Spec (Phase 1)

> Binding parent: `SEB.MD` SHARED CONTRACT (pin map, frozen `audio_engine.h`, telemetry/command
> schema, git rules). This document specifies the Track B "showpiece" layer. It is the artifact for
> **REVIEW GATE 1**.
>
> **Scope & priority:** **S1 (Temp-Detune FX) is the COMMITTED deliverable.** S2 (Sequencer/Arp) and
> S3 (Polyphony) are **stretch / nice-to-have**, attempted **only if S1 is merged green and time
> remains** (S2 only after S1 green; S3 only after S2 green).
>
> **Hard boundaries (from the contract):** Track B builds entirely against a **host simulator** of
> `audio_engine.h` plus its own frontend panels under `src/showpiece/*`, on branch `track-b`. Track B
> **never** edits `src/core/*` or the audio-engine internals, and adds **no new firmware API** to the
> frozen seam.

---

## 0. Design backbone — the `fx_detune` front-end (Approach 1)

`audio_engine.h` is **set-only**: there is no `voice_get_freq`. Any global pitch effect must own the
*intended* (base) note frequencies and re-push them. Therefore Track B introduces one thin module,
`fx_detune`, that is the **single front-end for all voice frequencies**:

```
note sources ──set base hz──▶ fx_detune ──voice_set_freq(v, base·2^(cents/1200))──▶ audio_engine.h
(sim harness now;            (owns base_hz[MAX_VOICES]
 Track A mapping +            + smoothed detune cents)
 sequencer/poly later)
```

Nothing calls `voice_set_freq` directly except `fx_detune`. This is the backbone that lets S1, S2,
and S3 **stack** without touching engine internals, and it is fully exercisable on the host.

**Rejected alternative (recorded for the judges):** an engine-side `audio_set_detune_cents()` would
be sample-accurate and cheaper, but it **adds API to the frozen header and touches engine internals**
— forbidden for Track B. That call belongs to Track A, not us.

---

## 1. Frozen seam reused (no additions)

From `SEB.MD` — Track B calls only these, never modifies them:

```c
// audio_engine.h  (MAX_VOICES = 4)
void audio_init(uint32_t sample_rate_hz);
void voice_set_freq(int v, float hz);   // 0..MAX_VOICES-1
void voice_set_gain(int v, float g);    // 0..1
void filter_set_cutoff(float hz);
void filter_set_q(float q);
void audio_set_master_gain(float g);
```

Sample rate is **32 kHz** (`audio_init(32000)`), matching the on-target TIM6 TRGO DAC clock.

**Header location (default, override if Track A already publishes it):** Track B materializes the
header verbatim at **`include/audio_engine.h`** as the shared seam. At merge it is reconciled with
Track A's copy; because the signature is frozen, this is a trivial de-dupe.

---

## 2. Host simulator rig (Phase 0 — the test bench)

The rig lets every Track B behavior be built and verified on the host with **no hardware**.

### 2.1 `sim/audio_engine_sim.c` — host implementation of the frozen API
- 4 voices, each a **`double` phase accumulator** sine (`phase += hz/sr; wrap [0,1)`), summed with
  per-voice gain. `double` phase avoids long-term drift; phase is **never reset** on a frequency
  change, so detune sweeps and step changes are click-free.
- One **RBJ biquad low-pass** (Audio EQ Cookbook) driven by `filter_set_cutoff`/`filter_set_q`.
- `audio_set_master_gain` scales the mix; the mix is clamped to `[-1, 1]` before quantization.
- **Sim-only introspection** (declared in `sim/audio_engine_sim.h`, **not** part of the frozen
  header): `void sim_render(float *out, int nframes);` and
  `void sim_get_voice(int v, float *hz, float *gain);` — used by tests and the telemetry writer to
  read back engine state. These do not exist on-target and are never called by firmware modules.

### 2.2 `sim/wav.{c,h}` — output writer
- 44-byte canonical RIFF/WAVE header, **32 kHz / 16-bit signed PCM / mono**, little-endian; fields
  back-patched after render.

### 2.3 `sim/harness.c` — timeline driver
Reads a **timeline script**, advances a software clock by sample count, drives `fx_detune`
(and later `sequencer`/`poly`), renders audio, and writes three artifacts:
- **`out.wav`** — the rendered audio (for golden-output feature checks).
- **`out.csv`** — one row per ~30 Hz telemetry frame:
  `t_s, temp_c, detune_c, v0_hz, v1_hz, v2_hz, v3_hz, rms`.
- **`telemetry.ndjson`** — newline-delimited JSON in the **exact contract schema**, populated for
  whichever S-items are active. This single file is what golden tests assert against **and** what the
  frontend replays for a no-board demo.

### 2.4 Timeline script format (line-based, `#` comments)
```
# t_seconds  command  args
0.0  master 0.8
0.0  cutoff 8000
0.0  base   0 440.0
0.0  gain   0 0.9
0.0  temp   25.0      # first temp → captures T_ref
2.0  temp   30.0      # +5 °C → +75 cents (rail)
4.0  temp   20.0      # −5 °C → −75 cents (rail)
6.0  end
```
S1 commands: `master`, `cutoff`, `q`, `base v hz`, `gain v g`, `temp c`, `end`. S2/S3 add
`bpm`, `seq on|off`, `scale`, `pattern`, `note ...` (defined in their sections).

---

## 3. S1 — TEMP-DETUNE FX  *(COMMITTED)*

**Theme:** "the room controls the instrument." Ambient temperature continuously detunes every voice.

### 3.1 Behavior
Module `fx_detune/fx_detune.{c,h}`. Public API (host + on-target identical):
```c
void  fx_detune_init(void);                 // clears base table, marks T_ref uncaptured
void  fx_detune_set_base(int v, float hz);  // set the INTENDED note for voice v (0 disables voice)
void  fx_detune_update(float temp_c, float dt_s); // recompute cents, smooth, re-push all voices
float fx_detune_get_cents(void);            // current smoothed detune → telemetry `detune_c`
```

Transfer (the committed curve):
```
on first fx_detune_update:  T_ref = temp_c            # calibrated baseline ("tare to the room")
raw_c    = 15.0f * (temp_c - T_ref)                   # slope = 15 cents / °C
target_c = clamp(raw_c, -75.0f, +75.0f)               # rails at ±5 °C from baseline
# one-pole smoothing (clamp-then-smooth, τ = 0.4 s):
a        = 1 - expf(-dt_s / 0.4f)
cents   += a * (target_c - cents)
factor   = exp2f(cents / 1200.0f)                     # cents → frequency ratio
for each voice v where base_hz[v] > 0:
    voice_set_freq(v, base_hz[v] * factor)            # warmer = sharper
```
Direction is **warmer = sharper**. Smoothing is **clamp-then-smooth**, so the smoothed value never
exceeds the ±75 rails. `detune_c` telemetry = `cents` rounded to 0.1.

### 3.2 How it layers on `audio_engine.h` without modifying Track A
- `fx_detune` calls **only** `voice_set_freq` (public seam). No engine internals, no new API.
- At integration, Track A's mapping changes a **single call site**: voice-0 pitch goes through
  `fx_detune_set_base(0, hz)` instead of `voice_set_freq(0, hz)`, and the ~30 Hz sensor loop calls
  `fx_detune_update(temp_c, dt)`. Track A then appends `detune_c` to its telemetry emit. No other
  Track A change.

### 3.3 Acceptance criteria
| ID | Criterion |
|----|-----------|
| **S1-AC1** | First `fx_detune_update` captures `T_ref`; `detune_c ≈ 0` (±0.5¢) at that instant. |
| **S1-AC2** | `target_c = clamp(15·(temp−T_ref), −75, +75)` exact at sampled temps incl. beyond ±5 °C (rails hold at ±75 within 0.1¢). |
| **S1-AC3** | Applied to **all** voices: with base 440 Hz on voices 0–3 and steady `detune_c=+75`, Goertzel on `out.wav` measures `440·2^(75/1200) ≈ 459.4 Hz` on each active voice within **±3¢**. |
| **S1-AC4** | Step in `temp_c` produces a smoothed glide reaching ~63% of the delta within **τ=0.4 s**, monotonic, no overshoot. |
| **S1-AC5** | `detune_c` present in every telemetry frame and equals the applied cents (±0.1¢). |
| **S1-AC6** | `fx_detune` references **only** `audio_engine.h` public functions (enforced by include boundary + code review). |
| **S1-AC7** | No clicks/zipper: rendered audio has no sample discontinuity at temp changes (phase continuity guaranteed by construction; spot-checked in golden test). |

---

## 4. Telemetry & control (contract schema)

Track B only **adds** fields already reserved in the contract; older FE ignores them.
- **S1:** `"detune_c": <float cents>`.
- **S2:** `"seq": {"bpm":<f>,"step":<i>,"len":<i>,"on":<bool>}`; command `{"cmd":"seq","bpm":120,"on":true}`.
- **S3:** `"voices": [{"hz":<f>,"g":<f>}, ...]`.

Frontend renders each showpiece panel **only when its field is present** (`IfField` guard), exactly
per the contract's "render showpiece panels only when their telemetry fields appear."

---

## 5. Host test strategy (Phase 3 — written before code)

- **Framework:** plain C, tiny `CHECK(cond)` / `CHECK_NEAR(a,b,eps)` macros (`tests/check.h`); one
  `main()` per test file; registered with **CTest** (built into CMake) — **no external dependency**.
- **Build:** a **host-only** CMake target tree (`CMakeLists.txt`) compiled with native clang on
  macOS, fully separate from the ARM firmware build (no cross-toolchain needed for Track B dev).
- **Unit tests (`tests/test_detune.c`) — committed:** curve points; clamp at/beyond rails; baseline
  capture; smoothing step-response (τ); multi-voice application math; `detune_c` serialize
  round-trip.
- **Golden-output test (`tests/test_golden.c`) — committed:** run `harness` on a scripted timeline →
  `out.wav` → **Goertzel** single-bin detection per voice at checkpoints → assert detune offset in
  cents within **±3–5¢**, and assert `out.csv` / `telemetry.ndjson` `detune_c` agree. Cents error =
  `1200·log2(f_measured / f_target)`.
- **Stretch tests:** sequencer step-advance & BPM timing accuracy; scale/pattern selection; voice
  allocation/stealing; telemetry extension round-trip; "FE renders panel only when field present."

### 5.1 Test matrix (criterion → test)
| Criterion | Test |
|-----------|------|
| S1-AC1 baseline capture | `test_detune::baseline_capture` |
| S1-AC2 curve + clamp | `test_detune::curve_and_clamp` |
| S1-AC3 all-voices freq | `test_golden::all_voices_detuned` |
| S1-AC4 smoothing τ | `test_detune::smoothing_step` |
| S1-AC5 telemetry field | `test_detune::telemetry_roundtrip` |
| S1-AC6 seam boundary | build include-guard + review |
| S1-AC7 no clicks | `test_golden::no_discontinuity` |

---

## 6. Showpiece frontend (Phase 5 — `src/showpiece/*` only)

- **`TelemetrySource` interface** (`src/showpiece/telemetry/`): `useFixtureSource(frames, 30)` replays
  a recorded `telemetry.ndjson`; `useSerialSource()` reads live Web Serial (readable stream →
  `TextDecoderStream` → line split → `JSON.parse`). **Both return the same shape**, so panels are
  agnostic to live-vs-replay.
- **TempDetune panel (committed):** hand-rolled **center-zero SVG gauge**, scale **±75¢**, needle at
  0 center, plus a `temp_c` readout and a small detune sparkline. Driven by replay now; by Track A's
  live stream at the demo.
- **Standalone dev entry (`src/showpiece/dev/`):** a minimal Vite page that mounts the showpiece
  panels off the fixture — enables build/demo with **no board and without Track A's app shell**. At
  merge, the *same* components drop into Track A's app behind `IfField`.
- Panel→milestone map: **TempDetune → S1**, SequencerGrid → S2, VoicesPanel → S3, optional pose cube
  (roll/pitch) → garnish.

---

## 7. S2 — STEP SEQUENCER / ARP  *(STRETCH — attempt only if S1 merged green + time remains)*

### 7.1 Behavior
Module `sequencer/sequencer.{c,h}`. A BPM clock advances a step pattern; each step calls
`fx_detune_set_base(v, note_hz)`, so **detune continues to bend the sequence**. Monophonic on voice 0
until S3. Defaults: **16 steps**, **24 PPQN**, root **A3 = 220 Hz**, default **120 BPM**, scales
`{major, minor-pentatonic, chromatic}`. Tilt selects performance params (roll → scale, pitch →
pattern, quantized into bins) **or** `{"cmd":"seq",...}` sets them explicitly.

### 7.2 Timing
- **Now (host):** sample-accurate **software tick** — the harness derives ticks from BPM and sample
  count (24 PPQN; super-loop divides PPQN to fire step events).
- **At integration:** maps to hardware **TIM7** (free basic timer; PSC=169 → 1 MHz, ARR from BPM);
  NVIC priority **8** (audio DMA stays **0**); **flag-only ISR** (`tick_flag=1`), the super-loop does
  the frequency work — never starves audio.

### 7.3 Telemetry / control
`"seq":{"bpm","step","len","on"}`; `{"cmd":"seq","bpm":120,"on":true}` (optional `scale`,`pattern`).

### 7.4 Acceptance criteria
| ID | Criterion |
|----|-----------|
| S2-AC1 | Step index advances at the configured BPM within **±1%** timing error (host, sample-derived). |
| S2-AC2 | Selected scale maps each step to the correct frequency (table-verified). |
| S2-AC3 | Detune **composes**: with a non-zero `detune_c`, golden test detects each step at `note·2^(cents/1200)` (±3¢). |
| S2-AC4 | `seq` telemetry serialize/parse round-trips; `{"cmd":"seq",...}` updates state. |
| S2-AC5 | FE SequencerGrid renders only when `seq` field present. |
| S2-AC6 | Calls only `audio_engine.h` (via `fx_detune_set_base` / `voice_set_gain`); no engine edits. |

---

## 8. S3 — POLYPHONY  *(STRETCH — attempt only if S2 green + time remains)*

### 8.1 Behavior
Module `polyphony/poly.{c,h}`. Voice allocator across `MAX_VOICES = 4` for chords/arpeggios.
Note-on → first free voice; if none free, **steal oldest**. Note-off frees the voice. Each voice's
base is set via `fx_detune_set_base(v, hz)` (detune applies to all); gain via `voice_set_gain`.
Emits `"voices":[{"hz","g"}, ...]`.

### 8.2 Acceptance criteria
| ID | Criterion |
|----|-----------|
| S3-AC1 | ≤4 simultaneous note-ons map to **distinct** voices. |
| S3-AC2 | A 5th note-on **steals oldest**; freed/released voices return to the pool. |
| S3-AC3 | `voices[]` telemetry matches the live allocation (hz/gain per voice). |
| S3-AC4 | Golden test Goertzel-detects each concurrent voice frequency (with detune applied). |
| S3-AC5 | Calls only `audio_engine.h` via the `fx_detune` front-end; no engine edits. |

---

## 9. Integration & rollback (Phase 6 — T-45 window, with Track A)

**Merge `track-b` → `track-a` only if `track-a` is green.** Steps:
1. **S1 (committed):** redirect Track A's voice-0 pitch through `fx_detune_set_base(0, hz)` (**1-line**
   call-site change at the mapping layer); call `fx_detune_update(temp_c, dt)` from the existing
   ~30 Hz sensor loop using the real MCP9808 reading; append `detune_c` to the telemetry emit. FE
   TempDetune lights up from the live `detune_c`.
2. **S2 (if reached):** allocate **TIM7** for the step tick (NVIC pri 8, flag-only ISR); super-loop
   consumes the tick → `sequencer_step()` → `fx_detune_set_base`.
3. **S3 (if reached):** allocator drives voices via the same front-end + `voice_set_gain`.
4. Re-run **Track A BIST** + Track B **host tests**.
5. **HARDWARE REVIEW:** S1 adds **no pins**; S2 uses **TIM7 only** (no GPIO); timer/IRQ budget OK.
6. **CODE REVIEW:** no ISR audio starvation (sequencer ISR sets a flag only); Track B math is float,
   the engine converts to its own fixed-point internally; non-blocking telemetry preserved.

**Rollback:** if not green by **T-30** → ship **S1 only**. If S1 not green → **drop `track-b`
cleanly**; `track-a` stays the demo. Each S-item is a separate module + commit, independently
revertable.

---

## 10. Non-goals, risks, defaults

**Non-goals:** no engine internals; no `src/core/*`; no additions to the frozen header; no new
hardware for S1.

**Risks & mitigations:**
- *Track A 1-line redirect needs coordination* → hand Track A the exact diff; keep
  `fx_detune_set_base` signature trivial.
- *MCP9808 self-heating / slow response* → calibrated baseline + 0.4 s smoothing absorb it; demo uses
  breath/finger for a fast, visible delta.
- *Header reconciliation at merge* → frozen + tiny → trivial de-dupe.
- *Float detune recompute cost on-target* → 4 voices × one `exp2`/mul per update at ~30 Hz →
  negligible.

**Defaults chosen (override any):**
1. `audio_engine.h` canonical at **`include/audio_engine.h`** (reconcile with Track A at merge).
2. Smoothing one-pole **τ = 0.4 s**, clamp-then-smooth.
3. Tests: **plain C + CTest**, no external framework.
4. Sequencer defaults (if reached): **16 steps, root A3 = 220 Hz, scales {major, minor-pentatonic,
   chromatic}, 120 BPM**.

**Golden numbers for quick reference:** +75¢ → ×1.04427 (440 → 459.4 Hz); −75¢ → ×0.95762
(440 → 421.4 Hz); slope 15¢/°C; rails at ±5 °C from baseline.
