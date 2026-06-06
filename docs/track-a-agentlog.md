# Track A — Agent Decision Log
**Branch:** `track-a`
**Author:** Seb agent (Root Access)

This log records key prompts, decisions, and architectural rationale for judges' Q&A.

---

## Session 1 — Spec + Plan (Phase 1 + Phase 2)

### Context gathered

| Document | Key facts extracted |
|----------|-------------------|
| `SEB.MD` | Pin map (authoritative), audio_engine.h API, telemetry schema, NVIC priority order, build system (CMake + Ninja), compiler flags |
| `imu.pdf` (MPU-6050) | WHO_AM_I reg 0x75 = 0x68; PWR_MGMT_1 0x6B; ACCEL_CONFIG 0x1C; ACCEL_XOUT_H 0x3B burst 14 bytes; ±2 g → 16384 LSB/g; die temp formula |
| `speaker.pdf` (STEMMA) | Class-D amp, 1W 8Ω, 3–5V power, analog audio in (AC-coupled on board), 3-pin JST |
| `stm.pdf` (NUCLEO-G474RE) | STM32G474RET6: Cortex-M4F @ 170 MHz, 512 KB Flash, 128 KB SRAM, CORDIC + FMAC, LQFP64 |

MCP9808 register map (Tamb 0x05, Mfr ID 0x06 = 0x0054, Device ID 0x07 = 0x0400) sourced from SEB.MD and standard MCP9808 datasheet knowledge.

---

### Key architectural decisions

#### D-001: Two-milestone structure (M1 "Reliable Hum" → M2 "CORDIC Synth")
**Decision:** Split into M1 (basic DAC tone, all sensors, telemetry) and M2 (CORDIC wavetable + DMA + FMAC).
**Rationale:** Hackathon rule — always-demoable. M1 is independently flashable and audible. M2 adds DSP quality. If M2 is not reached, M1 still wins the demo.

#### D-002: HC-SR04 echo capture — polarity-toggle vs dual-channel
**Decision:** Spec primary approach = polarity toggle (CC1 rising → reconfigure for falling). Fallback noted = dual CC channels (CC1 rising, CC2 falling on TI1FP2).
**Rationale:** Polarity toggle is simpler (one channel, one IRQ flag). Dual-channel is more robust against race conditions. Fallback documented in risks table.

#### D-003: FMAC bypass fallback
**Decision:** If FMAC init or coefficient computation fails, bypass filter and ship raw CORDIC sine. Set `status.audio=1`.
**Rationale:** FMAC q15 coefficient generation involves bilinear transform math that can overflow at extreme cutoff values. Audio must never go silent due to filter failure.

#### D-004: DMA buffer placement — SRAM1 only, not CCM
**Decision:** All DMA-accessible buffers (`wavetable`, `dac_buf`, `fmac_in/out`) placed in `.dma_buf` section mapped to SRAM1 (0x20000000).
**Rationale:** STM32G4 CCM SRAM (0x10000000) is NOT accessible by DMA1. Placing DMA buffers in CCM causes a bus error (HardFault). This is a common STM32G4 gotcha.

#### D-005: Phase accumulator — Q16.16 fixed-point
**Decision:** Voice phase accumulator and step are Q16.16 `uint32_t`. Top 8 bits index the 256-sample wavetable.
**Rationale:** 32-bit write is atomic on Cortex-M4 (single STR instruction). No critical section needed for `voice_set_freq`. Avoids mutex overhead in audio path.

#### D-006: Telemetry TX — ring buffer + DMA, never blocks
**Decision:** 256-byte static ring buffer. `snprintf` into 128-byte stack buffer, copy to ring. DMA drains ring. Drop frame if full.
**Rationale:** SEB.MD contract: "LPUART TX must NEVER stall audio." DMA-driven TX means the main loop never waits for UART. Frame drop is acceptable at 30 Hz; the dashboard will show a gap.

#### D-007: Pitch mapping — log scale (110 Hz × 8^t)
**Decision:** `synth_hz = 110 * powf(8, t)` where t = normalised distance [0,1].
**Rationale:** 880/110 = 8 = 2³ (three octaves). Log mapping gives perceptually uniform pitch sweep — linear distance maps to linear musical pitch. `powf` is fast on the FPU.

#### D-008: Octave gate threshold — |roll| > 30°
**Decision:** If |roll_deg| > 30°, double `synth_hz`.
**Rationale:** 30° is a natural wrist tilt that's easy to hit intentionally but hard to trigger accidentally. Provides a clear musical gesture (octave jump) without requiring precise control.

#### D-009: Servo as VU meter — distance-driven
**Decision:** `servo_deg = dist_filt_mm / 4000 * 180`. Closer hand → lower angle; farther → higher.
**Rationale:** Intuitive visual: hand far away → servo points up (high). Provides live visual feedback of the distance sensor reading without any extra computation.

#### D-010: IIR smoothing α = 0.1 for both distance and frequency
**Decision:** Single α = 0.1 for both `dist_filt_mm` and `filt_hz`.
**Rationale:** α = 0.1 gives ~10-sample time constant (≈300 ms at 30 Hz sensor rate). Smooth enough to avoid jitter, fast enough to track hand movement. Same value for both simplifies tuning.

#### D-011: NVIC priority assignment
**Decision:** Audio DMA = 0, TIM4 echo = 1, I2C = 2, LPUART = 3.
**Rationale:** Matches SEB.MD contract exactly. Audio DMA must never be preempted by sensor or telemetry work. TIM4 echo capture is timing-critical (1 µs resolution) so it preempts I2C and UART.

#### D-012: No RTOS — bare-metal super-loop
**Decision:** Bare-metal super-loop with interrupts and DMA. No FreeRTOS.
**Rationale:** SEB.MD specifies "none — bare-metal super-loop + interrupts/DMA." Adding an RTOS would increase complexity and stack usage without benefit for this sensor-read → map → audio-set pattern.

#### D-013: `mapping.c` — no HAL dependencies
**Decision:** `mapping.c` / `mapping.h` contain only pure math (no HAL includes).
**Rationale:** Enables host-side unit testing (compile on macOS/Linux with standard gcc). The mapping logic (distance→Hz, tilt→cutoff, etc.) is the most testable part of the firmware and should be verified without hardware.

#### D-014: Build order — 13 incremental steps
**Decision:** 13 steps from blink to full BIST, each independently flashable.
**Rationale:** Hackathon constraint: always-demoable. If time runs out at step 8 (basic tone), the demo still works. Each step adds one capability without breaking previous ones.

---

### Open questions for hardware bring-up

1. **HC-SR04 echo divider**: Confirm 1kΩ + 2kΩ resistors are wired before powering on. PB6 is 3V3-tolerant only.
2. **I2C pull-ups**: Confirm 4.7kΩ pull-ups to 3V3 on PB8/PB9. MPU-6050 and MCP9808 both need them.
3. **Servo power**: Confirm SG90 is on 5V rail (not 3V3) with 470µF + 100nF decoupling. Check LD4 (overcurrent LED).
4. **PA4 vs PA5**: Double-check GPIO init — PA4 = DAC only, PA5 = LED only. Never swap.
5. **DMA channel assignment**: Verify DMAMUX1 request IDs for DAC1_CH1 and LPUART1_TX against STM32G474 RM Table 80.

---

### Files produced this session

| File | Phase | Description |
|------|-------|-------------|
| `docs/track-a-spec.md` | Phase 1 | Full M1+M2 specification with register maps, math, failure modes, acceptance criteria |
| `docs/track-a-plan.md` | Phase 2 | Module breakdown, ISR/DMA plan, timer allocation, buffer sizes, build order, risks |
| `docs/track-a-agentlog.md` | Both | This log |

---

*Next session: Phase 3 — E2E tests (host unit tests + on-target BIST test matrix). Awaiting REVIEW GATE 1 + 2 approval.*
