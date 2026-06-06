# Track B — Agent Log (prompts & decisions)

Per the contract, key prompts/decisions are logged here for the judges' Q&A.

## Phase 1 — Spec / Brainstorming (REVIEW GATE 1)

**Context loaded:** `SEB.MD` (shared contract), `track-a.md`, `track-b.md`. Repo was greenfield
(docs only). Created branch `track-b` from `main`.

**Background research (librarian agents):**
- Host-sim C: 44-byte WAV writer; `double` phase-accumulator sine (click-free on freq change); RBJ
  biquad low-pass; Goertzel single-bin DFT for golden-output freq checks (cents = 1200*log2(f/f0)).
- STM32G474 timer budget: TIM7 free (TIM6/3/4 taken) for the S2 sequencer tick; NVIC pri 8
  (audio DMA stays 0); flag-only ISR; 24 PPQN, PSC=169, ARR from BPM.
- Frontend: one TelemetrySource interface so panels are agnostic to live Web Serial vs replayed
  NDJSON fixture; hand-rolled SVG center-zero gauge; r3f pose cube; IfField presence guard.

**Decisions (from clarifying Q&A):**
- D1 - Detune range: +/-75 cents (three-quarter-tone), warmer = sharper. (User chose +/-75 over
  +/-50 / +/-100 / +/-200.)
- D2 - Zero point: calibrated baseline at startup ("tare to the room"); slope 15 cents/degC
  (rails at +/-5 degC from baseline). (User chose calibrated over fixed-25C / auto-recenter.)
- D3 - FX seam: Approach 1 - a fx_detune front-end owns base_hz[MAX_VOICES] + smoothed cents and
  re-pushes voice_set_freq; all note sources set pitch through it. Rejected engine-side
  audio_set_detune_cents() (would touch the frozen header / engine internals - forbidden).
- D4 - Defaults: header at include/audio_engine.h; smoothing tau = 0.4 s clamp-then-smooth; tests
  plain C + CTest; sequencer defaults 16 steps / A3=220 / {major, min-pentatonic, chromatic} / 120 BPM.
- Process: visual companion offered -> declined (text-only). Design presented in sections ->
  approved. Spec written to docs/track-b-spec.md.

**Status:** Spec drafted -> entering spec-review loop, then user review, then Phase 2 plan.

## Phase 1 — review pass + branch note

- **Branch reconciliation:** the shared working tree was switched to `keeks-tracka-appA-test` by a
  parallel session (same team, confirmed by user). Per user instruction, Track B continues on
  `keeks-tracka-appA-test`; the `track-b` spec commit (74125ed) was fast-forwarded onto it so `docs/`
  is present. `track-b` branch retains the work too.
- **Spec review:** automated critic agent timed out (infra). Manual review against `SEB.MD` done —
  contract fidelity / S1 math / scope / consistency OK. Refinements applied (spec §11): AC3 hardened
  (per-voice introspection + distinct-frequency chord, avoids false-pass & clipping); baseline NaN/
  dropout handling; disabled-voice + sim-normalization rules; precision (+75¢ → 459.5 Hz).

## Phase 2 — implementation plan (REVIEW GATE 2)

- User approved `docs/track-b-spec.md` and requested immediate move to implementation plan because time is tight.
- Used `writing-plans` skill. Wrote `docs/track-b-plan.md` (user-required location overrides default).
- Plan locks execution order: Task 1 frozen header/build scaffold; Task 2 Phase 0 host sim + WAV/CSV/NDJSON harness; Task 3 S1 tests before code and REVIEW GATE 3; Task 4 S1 implementation + golden artifacts and S2 go/no-go; Task 5 TempDetune frontend and REVIEW GATE 4; Tasks 6-7 stretch S2/S3; Task 8 integration/rollback.
- Plan records Track A data needs: `temp_c`, voice-0 intended `synth_hz` call-site redirect through `fx_detune_set_base`, `dt_s`, telemetry append of `detune_c`; S2 uses TIM7 at NVIC priority 8 with flag-only ISR.
- Placeholder scan clean. Track A docs remain untracked and untouched by Track B.

## Phase 0 — host simulator

- Implemented `include/audio_engine.h` from the frozen contract.
- Added isolated host-sim CMake build under `sim/`; Ninja was unavailable locally, so verified with CMake Makefiles (`cmake -S sim -B build-track-b-make`).
- Implemented 4-voice sine phase-accumulator host simulator, RBJ biquad low-pass, master gain, manual little-endian WAV writer, and timestamped timeline harness.
- Ran `sim/timelines/s1_temp_sweep.txt` through the harness. Outputs: `sim/artifacts/phase0/out.wav` (512044 bytes), `sim/artifacts/phase0/out.csv`, `sim/artifacts/phase0/telemetry.ndjson` (241 frames). S1 detune is still stubbed as 0.0; real detune comes after REVIEW GATE 3 tests.

## Phase 4 shortcut — S1 functionality first

- User requested skipping the full planned test-writing/golden-test phase due time pressure.
- Kept a minimal smoke verification: CMake Makefile build, run harness, inspect telemetry at key timestamps.
- Implemented `fx_detune`: first finite temp captures baseline; slope 15 cents/degC; clamp +/-75 cents; tau 0.4 s smoothing; invalid temps hold last cents; base_hz==0 inactive.
- Harness now routes `base` events through `fx_detune_set_base`, samples temp at 30 Hz through `fx_detune_update`, and emits real `detune_c`.
- Smoke run: t=0 detune 0.0/v0 440.0; t=3000ms detune ~69.34/v0 ~457.98; t=5000ms detune ~-63.71/v0 ~424.10; t=8000ms detune ~-0.50/v0 ~439.87. Artifacts saved under `sim/artifacts/s1/`.

## Phase 5 shortcut — TempDetune frontend source

- Added `src/showpiece/*` only: telemetry types, fixture replay hook, field guard, accessible SVG cents gauge, TempDetune panel, and standalone dev component source.
- Clean Track B worktree has no `package.json` / Track A Vite app shell, so no npm build was run and no app scaffold was created. Components are ready to drop into Track A's app behind `IfField` when the shell exists.
