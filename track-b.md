# Track B — Air-Synth showpiece (Approach 3) on host-sim + FE panels

> Paste this into a SECOND Seb session. It assumes `SEB.MD` (the SHARED CONTRACT) is loaded.
> Track B does NOT get the physical board (Track A owns it) and must NOT block on it.
> Branch `track-b`. **Scope: S1 (temp-detune) is the COMMITTED deliverable; S2 (sequencer) and
> S3 (polyphony) are NICE-TO-HAVES**, attempted only after S1 is merged green and time remains.

---

You are Seb, building the STRETCH "showpiece" layer for the Air-Synth hackathon project. Read the
SHARED CONTRACT in SEB.MD and treat it as binding. CRITICAL: you build entirely against a HOST
SIMULATOR of `audio_engine.h` and your OWN frontend panels (`src/showpiece/*`), on branch `track-b`,
ready to merge onto `track-a` at the T-45 min window. You never touch `src/core/*` or the audio
engine internals.

**Priority order (do in this order; each is independently shippable/mergeable):**
1. **S1 — TEMP-DETUNE FX (COMMITTED).** Cheap, unique, on-theme ("the room controls the instrument").
2. S2 — STEP SEQUENCER / ARP (nice-to-have).
3. S3 — POLYPHONY (nice-to-have).

Work in strict phases. STOP at each ⛔ REVIEW GATE and wait for "approved". Use `/plan`. Commit at
every green step.

## PHASE 0 — HOST SIM (do first, no gate)
Implement `sim/audio_engine_sim.c` against the frozen `audio_engine.h` that synthesizes voices to a
32 kHz WAV/CSV (plain C sine + simple IIR), plus a tiny harness that drives it from a fake telemetry
timeline. This is your test rig — no hardware needed. Commit.

## PHASE 1 — SPEC  → write `docs/track-b-spec.md` (prioritized)
- **S1 TEMP-DETUNE FX (committed)**: MCP9808 `temp_c` → detune in cents applied to all voice
  frequencies; emits `detune_c` telemetry. Define transfer curve, range, smoothing.
- **S2 STEP SEQUENCER / ARP (stretch)**: BPM-clocked step pattern driving `voice_set_freq` over
  time; gesture/tilt selects scale or pattern; emits `seq` telemetry; `{"cmd":"seq",...}` control.
  Define timing source, patterns, scales.
- **S3 POLYPHONY (stretch)**: voice allocator across MAX_VOICES (chord/arp); emits `voices`
  telemetry. Define allocation/stealing.
Each item: behavior, acceptance criteria, and how it layers on `audio_engine.h` WITHOUT modifying
Track A internals. Mark S2/S3 explicitly as "attempt only if S1 merged green + time remains."
⛔ **REVIEW GATE 1.**

## PHASE 2 — IMPLEMENTATION PLAN  → write `docs/track-b-plan.md`
Modules (`fx_detune/`, then `sequencer/`, `polyphony/`) that ONLY call `audio_engine.h`; timing
approach (a software tick now, mapping to a hardware timer at integration); data needed from Track
A's telemetry; FE panel plan (`src/showpiece/TempDetune`, then `SequencerGrid`, `VoicesPanel`,
optional Three.js pose cube from roll/pitch). Integration plan: exact merge steps onto `track-a`,
what hardware timer the sequencer will use, and a rollback (if not green by T-30, ship S1 only or
none).
⛔ **REVIEW GATE 2.**

## PHASE 3 — E2E TESTS (before code, against the plan; all runnable on host via the sim)
- `temp_c` → `detune_c` cents (curve, clamps, smoothing) — **this is the committed test set**.
- (stretch) sequencer step advance & BPM timing accuracy; scale/pattern selection; voice
  allocation/stealing; telemetry extension serialize round-trip; FE renders panels only when fields
  present.
- **Golden-output test**: drive sim with a scripted timeline → assert WAV/CSV features (detected
  step frequencies, detune offset).
- TEST MATRIX: criterion → test.
⛔ **REVIEW GATE 3.**

## PHASE 4 — CODE (against sim)
Implement **S1 fully**, run host tests, regenerate golden WAV/CSV, paste a summary, then **STOP for
a go/no-go** before attempting S2, and again before S3. Never edit `src/core/*` or audio engine
internals.

## PHASE 5 — SHOWPIECE FRONTEND (`src/showpiece/*` only)
- **TempDetune gauge (committed)** driven from a recorded/simulated telemetry replay (no board).
- (stretch) SequencerGrid (step/BPM/pattern), VoicesPanel (per-voice hz/gain), optional pose cube.
Match each panel to its milestone (S1 / S2 / S3).
⛔ **REVIEW GATE 4.**

## PHASE 6 — INTEGRATION + QA (at the T-45 window, with the board, coordinated with Track A)
- Merge `track-b` → `track-a`; bind MCP9808 real temp to `detune_c` (S1). For stretch items, wire
  sequencer timing to a real hardware timer and voices to the real CORDIC/DAC engine.
- Re-run Track A BIST + your host tests; **HARDWARE REVIEW** (no new pin conflicts; timer budget OK)
  and **CODE REVIEW** (no audio glitches/ISR starvation from sequencer tick; voice math fixed-point
  safe).
- **If not green by T-30: ship S1 only, or drop cleanly** (`track-a` stays the demo).
- Output a Definition-of-Done report + which S-items merged.

Throughout: log key prompts/decisions to `docs/track-b-agentlog.md`. Stop at every gate.
