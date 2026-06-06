# Track A — Agent Log (prompts & decisions)

Per the contract, key prompts/decisions are logged here for the judges' Q&A.

## Phase 1-2 — Spec + Plan (REVIEW GATES 1 & 2)

**Context loaded:** `SEB.MD` (shared contract), `track-a.md`, `track-b.md`, existing
`docs/track-b-spec.md`. Branch `keeks-tracka-appA-test` created from `main` (clean Track A base; does
not carry Track B's spec commit). Repo was greenfield (docs only, no source).

**Environment probe (decisive for scope):** Mac with Homebrew, node/npm/pnpm, cmake, clang, make,
python3 present. NO arm-none-eabi-gcc, ninja, openocd/st-flash/STM32_Programmer_CLI; no STM32CubeG4
package; no board attached. => Firmware build/flash needs a toolchain bring-up (plan Step 0) + the
physical board; host tests + Vite frontend build on this machine now.

**Background research (4 parallel librarian agents):**
- Audio out: STM32G4 CORDIC SINE (q1.31, PRECISION 6) -> precompute 1024-entry q1.15 wavetable; DAC1
  PA4 fed by TIM6 TRGO @32 kHz (PSC=0, ARR=5311) via DMA1_Ch3 circular double-buffer in SRAM1 (CCM is
  NOT DMA-accessible); audio DMA at NVIC pri 0. Refs: CORDIC_CosSin / CORDIC_Sin_DMA, RM0440, AN5325.
- FMAC IIR LPF: FUNC=IIR Direct Form 1, single biquad; RBJ LPF -> normalize -> negate a-coeffs ->
  prescale /2 with gain R=1 (handles |a1|>1) -> q1.15; runtime sweep via Stop/Config/Preload/Start;
  fallback arm_biquad_cascade_df1_f32. Refs: AN5305, FMAC_IIR_ITToPolling, VictorTagayun repo.
- Sensors/servo: HC-SR04 TIM4 PSC=169 (1us)/ARR=65535, dist_mm=echo_us*10/58, 38 ms timeout;
  MPU-6050 0x68 (WHO_AM_I 0x68, wake 0x6B, 16384 LSB/g, roll/pitch via CORDIC PHASE); MCP9808 0x18
  (reg 0x05, 0.0625 C/LSB, sign at 0x1000); SG90 TIM3 PSC=169/ARR=19999 (50 Hz), CCR 1000/1500/2000.
- Frontend: Vite+React+TS + uPlot (hand-rolled useRef, not a wrapper, for 30 Hz); Web Serial
  requestPort (user gesture) -> TextDecoderStream + LineBreakTransformer -> JSON.parse; ring buffer
  WINDOW=1800; uPlot.sync shared cursor; IfField presence guard; writer for commands. Refs: MDN,
  developer.chrome.com/docs/capabilities/serial, leeoniya/uPlot.

**Decisions (defaults, override at review):**
- D1 - Distance->pitch: 50-500 mm window, 2-octave exponential map from A3=220 Hz, closer=lower,
  tau=0.06 s smoothing.
- D2 - Tilt mappings: pitch_deg -> cutoff 200 Hz-8 kHz (M2); roll_deg -> octave bins + vibrato; gate
  on extreme nose-down. Clamp-then-smooth throughout.
- D3 - Audio: 32 kHz, 1024-entry q1.15 wavetable, 64-sample DMA double-buffer in SRAM1.
- D4 - FMAC: single biquad, prescale /2 + R=1; CMSIS-DSP float biquad as documented fallback.
- D5 - Build order layered & always-demoable; M1 "Reliable Hum" locked as fallback before M2 DSP.
- D6 - TIM7 left free for Track B sequencer; S1 detune integration = one call-site change at mapping.
- Process: review gates noted; per user direction this session runs research -> Spec -> Plan and
  STOPS at the code boundary for handoff to a dedicated implementation (deep-work) agent.

**Status:** `docs/track-a-spec.md` (Phase 1) and `docs/track-a-plan.md` (Phase 2) drafted ->
awaiting review; Phase 3 (tests = code) deferred to implementation agent.
