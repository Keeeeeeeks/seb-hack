# Track A — Air-Synth Implementation Plan (Phase 2)

> Derived from the approved `docs/track-a-spec.md`. Binding parent: `SEB.MD`. This document is the
> artifact for **REVIEW GATE 2**. It defines module layout, ISR/DMA/timer/NVIC budget, buffer
> placement, the layered (always-demoable) build order, the frontend plan, integration with Track B,
> and risks + fallbacks.
>
> **Environment reality (verified on this machine):** clean Mac (Homebrew present; node/npm/pnpm,
> cmake, clang, make, python3 present). **No `arm-none-eabi-gcc`, no `ninja`, no `openocd`/`st-flash`/
> `STM32_Programmer_CLI`, no STM32CubeG4 package, no board attached.** Firmware build/flash therefore
> requires a toolchain bring-up step (Step 0 below) and physical hardware. The host test bench and the
> Vite frontend build on this machine as-is.

---

## 0. Toolchain & repo bring-up (prerequisite for firmware)

**Install (macOS / Homebrew):**
```
brew install --cask gcc-arm-embedded     # arm-none-eabi-gcc toolchain (or: brew install arm-none-eabi-gcc)
brew install ninja openocd stlink dfu-util
# STM32CubeG4 HAL/LL + CMSIS: clone pinned, or add as submodule
git clone --depth 1 https://github.com/STMicroelectronics/STM32CubeG4 third_party/STM32CubeG4
```
Verify: `arm-none-eabi-gcc --version`, `openocd --version`, `st-info --probe` (with board).

**Flash/debug:** `openocd -f interface/stlink.cfg -f target/stm32g4x.cfg` (GDB :3333) or
`STM32_Programmer_CLI -c port=SWD -w build/air-synth.elf -rst`. ELF: `build/air-synth.elf`.

---

## 1. Repository layout (target)

```
include/
  audio_engine.h        # FROZEN seam (verbatim from SEB.MD)
firmware/
  CMakeLists.txt        # arm-none-eabi cross build (Ninja), -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2
  stm32g474.ld          # linker: FLASH 512K @0x08000000, SRAM1 96K, CCM 32K
  startup_stm32g474.s
  src/
    main.c              # super-loop: init -> while{ sensor tick, mapping, telemetry, bist svc }
    bsp/                # clock(170MHz+boost), gpio, nvic, hal msp
    audio_engine/       # audio_engine.c (impl of frozen API): cordic_wt.c, dac_dma.c, fmac_iir.c
    sensors/            # mpu6050.c, mcp9808.c, hcsr04.c (TIM4 IC), i2c_bus.c
    mapping/            # mapping.c (distance->hz, tilt->cutoff/octave/gate/vibrato, smoothing)
    servo/              # servo.c (TIM3 PWM)
    telemetry/          # telemetry.c (NDJSON build + LPUART1 non-blocking TX ring), command.c (parse)
    bist/               # selftest.c
host/                   # host build (clang, no hardware) - mirrors Track B's pattern
  CMakeLists.txt        # native target tree, CTest
  tests/                # test_mapping.c, test_wavetable.c, test_fmac_coeff.c, test_telemetry.c, check.h
src/                    # FRONTEND (Vite+React+TS)
  core/                 # Track A OWNS: useSerialSource, ndjson parser, StatusPanel, charts, SynthScope, CommandBox
  showpiece/            # Track B OWNS (do not touch)
docs/                   # specs, plans, agentlogs
```
`include/audio_engine.h` is shared by firmware, host sim/tests, and (conceptually) Track B. Keep the
signature frozen.

---

## 2. Module breakdown & responsibilities

| Module | Files | Responsibility | Depends on |
|--------|-------|----------------|------------|
| **bsp** | clock, gpio, nvic, msp | 170 MHz PLL + boost, pin AF, NVIC priorities | HAL/LL |
| **audio_engine** | audio_engine.c, cordic_wt.c, dac_dma.c, fmac_iir.c | implement frozen API; CORDIC wavetable, DAC/TIM6/DMA render, FMAC LPF | CORDIC, DAC, DMA, TIM6, FMAC |
| **sensors** | i2c_bus.c, mpu6050.c, mcp9808.c, hcsr04.c | I2C reads + HC-SR04 TIM4 input capture; raw->engineering units; status flags | I2C1, TIM4 |
| **mapping** | mapping.c | distance->hz, tilt->cutoff/octave/gate/vibrato; clamps + one-pole smoothing (pure, host-testable) | (none - pure) |
| **servo** | servo.c | TIM3_CH1 PWM, deg->CCR, slew limit | TIM3 |
| **telemetry** | telemetry.c, command.c | build NDJSON line; non-blocking LPUART1 TX ring (DMA/IRQ); parse inbound commands | LPUART1, DMA |
| **bist** | selftest.c | `selftest` subsystem checks, PASS/FAIL, set status | all drivers |

**Purity rule:** `mapping/` and the coefficient/wavetable math are pure C with no HAL deps so they
compile and unit-test on host (Phase 3a).

---

## 3. Interrupt / DMA / timer / NVIC budget

**Timers**
| Timer | Use | Config |
|-------|-----|--------|
| TIM6 | audio sample clock | PSC=0, ARR=5311 -> 32.012 kHz TRGO (MMS=update) |
| TIM3 | servo PWM | PSC=169, ARR=19999 -> 50 Hz; CH1 PWM |
| TIM4 | HC-SR04 echo capture | PSC=169 (1 us), ARR=65535; CH1 input capture |
| (TIM7) | **reserved for Track B sequencer** | not used by Track A |
| SysTick | 1 ms tick / loop timing | HAL default |

**DMA (DMA1 + DMAMUX)**
| Channel | Stream | Mode | Note |
|---------|--------|------|------|
| DMA1_Ch3 | DAC1_CH1 (DMAMUX req) | circular, 16-bit, mem->periph | audio; dest `&DAC1->DHR12R1` |
| DMA1_ChX | LPUART1_TX (optional) | normal, 8-bit, mem->periph | non-blocking telemetry TX |
| (CORDIC DMA) | startup only | burst | one-shot wavetable precompute |

**NVIC priorities (lower number = higher; per `SEB.MD`)**
| IRQ | Priority | Rationale |
|-----|----------|-----------|
| DMA1_Channel3 (audio DAC) | **0** (highest) | must never be starved; fills idle buffer half |
| TIM4_CC (echo capture) | **1** | precise edge timing, brief ISR |
| I2C1 ev/er | **2** | sensor reads (or polled in loop) |
| LPUART1 TX (DMA) | **3** (lowest functional) | telemetry must never preempt audio |
| SysTick | 15 | timekeeping only |

**ISR rules (enforced in code review):** no `HAL_Delay`/heavy work/float `printf` in ISRs; audio ISR
is pure integer (phase accum + wavetable lookup + gain shift + DAC code write); shared vars `volatile`;
single 32-bit param writes are atomic on M4 (guard multi-word updates with brief IRQ mask).

---

## 4. Buffers & memory placement

| Buffer | Size | Region | Notes |
|--------|------|--------|-------|
| `audio_buf[2*64]` uint16 | 256 B | **SRAM1** (0x20000000) | DMA double-buffer, `aligned(4)`; CCM is NOT DMA-accessible |
| `wavetable[1024]` int16 | 2 KB | SRAM1 | DMA-readable at precompute |
| FMAC coeff/state | small | SRAM1 (or CCM for non-DMA scratch) | q1.15 coeff tables |
| LPUART TX ring | 512 B-1 KB | SRAM1 | NDJSON line staging for non-blocking TX |
| DSP hot state | small | CCM (0x10000000) | non-DMA compute only |

Linker (`stm32g474.ld`): FLASH 512K @0x08000000; RAM = SRAM1/2 96K @0x20000000; CCM 32K @0x10000000.

---

## 5. Build order (each step independently flashable & demoable)

> Mirrors `SEB.MD` layering: blink -> telemetry -> sensors -> tone -> CORDIC/DAC synth -> FMAC -> garnish.
> Commit at each green step (see Phase 4 in track-a.md).

**M0 — bring-up**
1. Project skeleton + clock (170 MHz + boost) + LD2 blink. *Demo: blink.*
2. LPUART1 @115200 non-blocking TX; emit `{"t":ms,"hb":1}` heartbeat. *Demo: serial heartbeat.*

**M1 — Reliable Hum**
3. I2C1 + MPU-6050 bring-up (WHO_AM_I, wake, read accel; roll/pitch). *Demo: tilt in telemetry.*
4. MCP9808 temp read. *Demo: temp_c in telemetry.*
5. HC-SR04 on TIM4 input capture -> dist_mm + timeout. *Demo: distance in telemetry.*
6. Servo on TIM3 PWM + LED VU. *Demo: servo/LED move with input.*
7. Simple tone (TIM/PWM or basic DAC) with pitch=f(distance), octave/gate=f(tilt) via mapping module.
   *Demo: playable hum.*
8. Full base telemetry JSON @30 Hz + `selftest` BIST. *Demo: dashboard-ready + BIST.*
   **-> M1 complete: always-demoable fallback locked.**

**M2 — CORDIC Synth**
9. CORDIC wavetable precompute (host-tested vs sin()). 
10. DAC1 + TIM6 + circular DMA render of voice 0 (replaces tone internals behind frozen API).
    *Demo: clean sine, pitch tracks distance.*
11. FMAC IIR LPF, cutoff=f(tilt) with runtime coeff update. *Demo: filter sweeps with tilt.*
12. Vibrato/volume=f(tilt) garnish. *Demo: expressive voice.*
    **-> M2 complete.**

**Frontend (Phase 5, parallelizable after step 8 telemetry schema is live)**
13. Vite+React+TS scaffold; `useSerialSource` (Web Serial + NDJSON); StatusPanel; charts.
14. M2 SynthScope + cutoff gauge; CommandBox (`set`/`selftest`). Render showpiece panels only if
    their fields appear.

---

## 6. Frontend plan (`src/core/*`)

- **Scaffold:** `npm create vite@latest . -- --template react-ts`; `npm i uplot`.
- **`useSerialSource` hook:** `requestPort()` on user gesture -> `open({baudRate:115200})` ->
  `port.readable.pipeThrough(TextDecoderStream)` -> `LineBreakTransformer` (buffer partial lines on
  `\n`) -> `JSON.parse`. Cleanup: `reader.cancel()` -> `await readableStreamClosed.catch()` ->
  `port.close()`. Listen for `navigator.serial` `disconnect`.
- **Charts:** hand-rolled uPlot via `useRef`+`useEffect` (NOT a React wrapper) for 30 Hz; ring buffer
  WINDOW=1800 (60 s); `u.setData()` each frame; `uPlot.sync("telemetry")` for shared cursor; one chart
  each for roll/pitch, dist_mm, temp_c, synth_hz, servo_deg; `ResizeObserver` -> `u.setSize`.
- **StatusPanel:** per-sensor present/OK/last-update-age, i2c_err, audio/servo state from `status`.
- **CommandBox:** `port.writable.getWriter()` -> write `JSON.stringify(cmd)+"\n"` -> `releaseLock()`.
- **`IfField`:** presence guard renders a panel only when its telemetry field is non-null (this is how
  Track B's showpiece panels light up post-merge without Track A editing them).
- **Gotchas:** Web Serial needs https/localhost; only one consumer owns the port (FE owns VCP at demo,
  Seb owns it during dev); baud must match; Chrome/Edge only.

---

## 7. Host test bench (Phase 3a) & CTest

- Native CMake tree under `host/` (clang), no cross toolchain. Tiny `CHECK`/`CHECK_NEAR` macros,
  one `main()` per test, registered with CTest.
- Tests: `test_mapping` (distance->hz, tilt->cutoff/octave/gate, clamps, smoothing step-response),
  `test_wavetable` (CORDIC angle scaling & sine table vs reference), `test_fmac_coeff` (RBJ ->
  normalize -> negate-a -> prescale/R), `test_telemetry` (NDJSON serialize + command parse round-trip).
- This shares the `mapping/` and coefficient/wavetable pure-C sources with firmware (no HAL).

---

## 8. Integration with Track B (T-45 window)

- Track B merges `track-b` -> `track-a` only if `track-a` is green. Track A stays always-demoable.
- **S1 detune (committed):** Track A changes ONE call site at the mapping layer — voice-0 pitch goes
  through `fx_detune_set_base(0, hz)` instead of `voice_set_freq(0, hz)`, and the ~30 Hz loop calls
  `fx_detune_update(temp_c, dt)`; then append `detune_c` to the telemetry emit. (Exact diff to be
  handed to Track B per their spec section 9.)
- **S2 sequencer (stretch):** allocate **TIM7** (free), NVIC pri 8, flag-only ISR; super-loop consumes
  tick. **S3 polyphony (stretch):** voice allocator drives voices 1..3 via the same front-end.
- Reconcile `include/audio_engine.h` (frozen -> trivial de-dupe).

---

## 9. Risks & fallbacks

| Risk | Mitigation / fallback |
|------|----------------------|
| FMAC IIR unstable / coeff range (`|a1|>1`) | prescale /2 + R=1; if still bad, ship CORDIC sine **without** filter, or `arm_biquad_cascade_df1_f32` (CMSIS-DSP) in the 32 kHz ISR |
| Audio glitches / DMA underrun | audio DMA NVIC pri 0; fill idle half only; integer-only ISR; keep telemetry TX DMA/non-blocking |
| CCM used for DMA by mistake | lint/review: all DMA buffers in SRAM1, `aligned(4)` |
| HC-SR04 5V echo damages 3V3 pin | mandatory 1k/2k divider on ECHO (hardware review) |
| Servo brown-out / LD4 overcurrent | external 5V for servo+speaker, common GND, bulk 470uF + 100nF |
| Sensor missing at demo | graceful degradation per spec section 7; headline kit (IMU->servo+LED) still demos |
| No board / toolchain at dev time | host tests + frontend (replay fixture) progress without hardware; firmware gated on Step 0 + board |
| M2 DSP runs long | M1 "Reliable Hum" is the locked, always-demoable fallback |
| Web Serial port contention | only one consumer; FE owns VCP at demo, Seb's serial only during dev |

---

## 10. Definition of Done (Phase 6 targets)

Host tests pass (CTest green); on-target BIST green; HARDWARE + CODE review checklists signed;
M1 demo script rehearsed (distance->pitch, tilt gate/octave, servo VU, live FE); M2 adds CORDIC sine +
FMAC sweep; graceful degradation demonstrated (unplug HC-SR04 -> red flag, audio holds).

---

**REVIEW GATE 2 - review/approve this plan before Phase 3 (tests, = code) begins.**
