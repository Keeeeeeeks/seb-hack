# Track A — Air-Synth core instrument (Approaches 1 → 2) + core dashboard

> Paste this into a Seb session. It assumes `SEB.MD` (the SHARED CONTRACT) is loaded as project context.
> Track A OWNS the physical board and the integration trunk (branch `track-a`).

---

You are Seb, building firmware on a STM32 Nucleo-G474RE for a 3-hour hackathon. Read the SHARED
CONTRACT in SEB.MD and treat it as binding (pin map, `audio_engine.h` API, telemetry/command schema,
git rules). You OWN the physical board and the integration trunk (branch `track-a`). Goal: a
gesture/distance-controlled audio synth ("Air-Synth") plus a Web Serial dashboard, delivered in
always-demoable layers.

Work in strict phases. STOP at each ⛔ REVIEW GATE, post the artifact, and wait for me to reply
"approved" before continuing. Use `/plan`. Set up a test bench (build + flash + read-serial) early
and build+verify after every change. Commit at every green step.

## PHASE 0 — Bring-up (do first, no review gate)
Blink LD2 (PA5), then bring up LPUART1 VCP telemetry at 115200 emitting a heartbeat JSON line.
Confirm on serial. Commit.

## PHASE 1 — SPEC  → write `docs/track-a-spec.md`
Cover, in milestones:
- **M1 (Approach 1, "Reliable Hum")**: I2C1 up; read MPU-6050 (accel/gyro) + MCP9808 temp;
  HC-SR04 distance via TIM4_CH1 input-capture IRQ; servo on TIM3_CH1; a simple timer/PWM (or basic
  DAC) TONE whose pitch = f(distance) and octave/gate = f(tilt); LEDs + servo as VU; full telemetry
  JSON; serial `selftest` BIST.
- **M2 (Approach 2, "CORDIC Synth")**: replace tone with CORDIC-sine wavetable → DAC1_OUT1 via
  TIM6-triggered DMA; FMAC IIR low-pass with cutoff = f(tilt); vibrato/volume = f(tilt); clean
  parameterized voice via `audio_engine.h` (implement the MAX_VOICES struct, drive voice 0).
Include: explicit sensor→sound mapping math (ranges, clamps, smoothing), failure behavior per sensor
(telemetry status flags, audio mutes safely), and **acceptance criteria per milestone**.
⛔ **REVIEW GATE 1.**

## PHASE 2 — IMPLEMENTATION PLAN  → write `docs/track-a-plan.md`
From the approved spec: module breakdown (`sensors/`, `audio_engine/`, `mapping/`, `telemetry/`,
`bist/`), interrupt & DMA plan with NVIC priorities (audio DMA highest, sensors mid, telemetry
lowest), timer allocation, buffer sizes/placement, and the build order (each step independently
flashable & demoable). Note risks + fallbacks (e.g., if FMAC unstable, ship CORDIC sine without
filter).
⛔ **REVIEW GATE 2.**

## PHASE 3 — E2E TESTS (write BEFORE code, derived from the plan's acceptance criteria)
- **(a) Host unit tests** (compile pure logic on host, no hardware): distance→Hz and
  tilt→cutoff/vibrato mapping (ranges/clamps); CORDIC angle scaling & wavetable correctness vs
  reference sine; FMAC q15 coeff generation; telemetry serialize + command parse round-trip.
- **(b) On-target BIST**: `selftest` over serial → I2C scan (MPU WHO_AM_I==0x68, MCP9808 mfr id),
  DAC 440 Hz / 200 ms burst, servo sweep min/center/max, one HC-SR04 ping prints mm, LED test;
  prints PASS/FAIL per subsystem and sets telemetry status flags.
- Provide a **TEST MATRIX** mapping each acceptance criterion → test.
⛔ **REVIEW GATE 3.**

## PHASE 4 — CODE
Implement M1 fully (build+verify+commit each module), then M2. After each milestone run host tests
+ on-target BIST; paste results. Keep audio glitch-free: telemetry TX must be non-blocking; no
HAL_Delay/heavy work in ISRs; shared vars `volatile`.

## PHASE 5 — CORE FRONTEND (`src/core/*`, Vite + React + uPlot, Web Serial, zero backend)
- Connect/disconnect (user gesture), NDJSON line parser.
- **StatusPanel**: per-sensor present/OK/last-update-age + i2c_err + audio/servo state (M1 scope).
- **Charts**: roll/pitch, distance_mm, temp_c, synth_hz, servo_deg.
- **M2 additions**: SynthScope (render current wavetable/params), filter cutoff gauge,
  "CORDIC/FMAC active" indicators.
- A command box to send `{"cmd":"set",...}` and `{"cmd":"selftest"}`.
Keep panels modular; do NOT touch `src/showpiece/*` (Track B owns it). App renders showpiece panels
only if those telemetry fields exist.

## PHASE 6 — QA (run, don't assert)
- **HARDWARE REVIEW** checklist (verify against the board before/while driving externals): pin map
  matches wiring; servo on 5V + common GND + bulk cap; HC-SR04 echo divider present; I2C pull-ups;
  PA4 audio not shared with PA5 LED; speaker amp powered/gain set; no two drivers per pin; LD4
  overcurrent not lit (else external 5V).
- **CODE REVIEW** checklist: NVIC priorities correct; DMA buffer size/alignment/RAM region; no
  blocking in ISRs; HAL returns checked; fixed-point scaling correct; non-blocking serial TX; safe
  audio mute on sensor loss.
- **Demo rehearsal**: 60–90 s script hitting distance→pitch, tilt→filter, servo VU, live FE
  dashboard. Confirm graceful degradation (unplug HC-SR04 → status flag red, audio holds last/safe).
- Output a **Definition-of-Done report**: tests pass, BIST green, checklists signed, demo script.

Throughout: log key prompts/decisions to `docs/track-a-agentlog.md` for the judges' Q&A. Stop at
every gate.
