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
