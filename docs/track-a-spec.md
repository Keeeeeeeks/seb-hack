# Track A — Air-Synth Firmware Specification
**Branch:** `track-a` | **Phase 1 / Gate 1**
**Target:** STM32G474RET6 on NUCLEO-G474RE (MB1367)
**Author:** Seb agent

---

## Overview

Air-Synth is a gesture/distance-controlled musical instrument. Hand height (HC-SR04 ultrasonic)
and IMU tilt (MPU-6050) drive a real-time audio synthesiser rendered on the STM32G4's DSP
silicon (CORDIC oscillator + FMAC IIR filter + DAC/DMA). A servo and LED provide visual
expression. Telemetry streams over LPUART1 to a Web Serial browser dashboard.

Firmware is delivered in two milestones, each independently demoable:

| Milestone | Codename | Audio engine | Gate |
|-----------|----------|--------------|------|
| M1 | "Reliable Hum" | Basic DAC tone (TIM-driven PWM/DAC) | Gate 1 |
| M2 | "CORDIC Synth" | CORDIC wavetable → DAC DMA + FMAC IIR | Gate 2 |

---

## Hardware Reference (authoritative pin map — do not reassign)

| Signal | Pin | Peripheral | Notes |
|--------|-----|------------|-------|
| LPUART1 TX | PA2 | LPUART1 | 115 200 8N1 → ST-LINK VCP |
| LPUART1 RX | PA3 | LPUART1 | |
| DAC audio out | PA4 | DAC1_OUT1 | ⚠ NEVER PA5 (LD2 LED) |
| Status LED | PA5 | GPIO out | On-board LD2 |
| Servo PWM | PB4 | TIM3_CH1 | 50 Hz, 1.0–2.0 ms |
| HC-SR04 TRIG | PB5 | GPIO out | 10 µs pulse |
| HC-SR04 ECHO | PB6 | TIM4_CH1 | Input capture; 5V→3V3 divider |
| I2C1 SCL | PB8 | I2C1 | 400 kHz Fast Mode |
| I2C1 SDA | PB9 | I2C1 | MPU-6050 (0x68) + MCP9808 (0x18) |

---

## Milestone 1 — "Reliable Hum"

### 1.1 Scope

Bring up all sensors, produce an audible tone whose pitch tracks hand distance, add octave-gate
from tilt, drive the servo as a VU meter, and stream full telemetry JSON at ~30 Hz.

### 1.2 I2C1 — 400 kHz Fast Mode

**Bus:** SCL = PB8, SDA = PB9. External 4.7 kΩ pull-ups to 3V3 required.
HAL I2C1 configured for 400 kHz (TIMINGR computed by CubeMX for 170 MHz PCLK1).

#### MPU-6050 (address 0x68)

| Step | Register | Addr | Expected / Action |
|------|----------|------|-------------------|
| Verify | WHO_AM_I | 0x75 | Read → must equal 0x68 |
| Wake | PWR_MGMT_1 | 0x6B | Write 0x00 (clear SLEEP bit) |
| Config | ACCEL_CONFIG | 0x1C | Write 0x00 (±2 g, AFS_SEL=0, 16384 LSB/g) |
| Read | ACCEL_XOUT_H | 0x3B | Burst-read 14 bytes: AX_H, AX_L, AY_H, AY_L, AZ_H, AZ_L, TEMP_H, TEMP_L, GX_H, GX_L, GY_H, GY_L, GZ_H, GZ_L |

**Burst-read protocol:** I2C repeated-start, device address 0x68 + write, register 0x3B, repeated-start,
device address 0x68 + read, 14 bytes, NACK on last byte, STOP.

**Accel → roll/pitch (small-angle, no gyro fusion for M1):**

```
ax_g = (int16_t)((buf[0]<<8)|buf[1]) / 16384.0f   // ±2 g range
ay_g = (int16_t)((buf[2]<<8)|buf[3]) / 16384.0f
az_g = (int16_t)((buf[4]<<8)|buf[5]) / 16384.0f

roll_deg  = atan2f(ay_g, az_g) * (180.0f / M_PI)
pitch_deg = atan2f(-ax_g, sqrtf(ay_g*ay_g + az_g*az_g)) * (180.0f / M_PI)
```

> **Note:** CORDIC atan2 (hardware) replaces `atan2f` in M2. For M1, use FPU `atan2f`.

**MPU-6050 temperature (internal sensor, informational only):**
```
temp_c = (int16_t)((buf[6]<<8)|buf[7]) / 340.0f + 36.53f
```
This is the die temperature, not ambient. MCP9808 provides ambient.

#### MCP9808 (address 0x18)

| Step | Register | Addr | Expected |
|------|----------|------|----------|
| Verify Mfr ID | Manufacturer ID | 0x06 | Read 2 bytes → 0x0054 |
| Verify Device ID | Device ID | 0x07 | Read 2 bytes → 0x0400 (upper byte = 0x04, lower = 0x00) |
| Read temp | Ambient Temp | 0x05 | Read 2 bytes, parse below |

**Tamb parse (register 0x05, 2 bytes MSB first):**
```
uint16_t raw = (buf[0] << 8) | buf[1];
int sign     = (raw & 0x1000) ? -1 : 1;
float temp_c = sign * (raw & 0x0FFF) / 16.0f;
// Upper nibble bits [15:13] are alert flags — mask them off
```

**Failure handling (both sensors):**
- If WHO_AM_I ≠ 0x68 or Mfr ID ≠ 0x0054: set `status.mpu` / `status.mcp` = 1, log error, continue.
- On any HAL I2C error: increment `status.i2c_err`, retry once, then hold last valid reading.
- Audio continues with last safe pitch/roll values — no crash, no mute.

### 1.3 HC-SR04 — TIM4_CH1 Input Capture

**Pins:** TRIG = PB5 (GPIO out, push-pull), ECHO = PB6 (TIM4_CH1 input, 5V→3V3 resistor divider).

**Echo voltage divider:** 1 kΩ (HC-SR04 side) + 2 kΩ (GND side) → 3.33 V max on PB6. ✓

**TIM4 configuration:**
- Clock source: internal (APB1 timer clock = 170 MHz)
- Prescaler: 169 → timer clock = 1 MHz (1 µs resolution)
- Period (ARR): 0xFFFF (65535 µs max, covers 400 cm range)
- Channel 1: input capture, rising edge → record `t_rise`; falling edge → record `t_fall`
- Alternatively: configure CC1 for rising, CC2 (same pin, indirect) for falling — use TI1FP1/TI1FP2
- IRQ: TIM4_IRQn, NVIC priority 1

**Recommended IC approach (single channel, polarity toggle):**
```c
// On rising edge ISR: save CCR1, reconfigure for falling edge
// On falling edge ISR: compute width = CCR1_fall - CCR1_rise (handle wrap)
uint32_t echo_us = (t_fall >= t_rise) ? (t_fall - t_rise)
                                       : (0xFFFF - t_rise + t_fall + 1);
```

**Trigger sequence (main loop, ≥60 ms between pings):**
```
PB5 HIGH for 10 µs → PB5 LOW → start TIM4 capture → wait for ISR
```

**Distance formula:**
```
distance_mm = (echo_us * 10) / 58   // integer arithmetic, result in mm
// Equivalent: distance_cm = echo_us / 58; distance_mm = distance_cm * 10
// Valid range: 20 mm – 4000 mm (HC-SR04 spec: 2 cm – 400 cm)
```

**Clamp:** `distance_mm = CLAMP(distance_mm, 20, 4000)`

**Failure handling:**
- No echo within 38 ms (timeout): set `status.sr04` = 1, hold last valid `distance_mm`.
- On timeout: TIM4 capture disabled, re-enabled on next trigger cycle.
- Audio holds last safe pitch — no crash.

**Smoothing (1st-order IIR, α = 0.1):**
```c
dist_filt_mm = α * distance_mm + (1 - α) * dist_filt_mm;  // α = 0.1f
```

### 1.4 Servo — TIM3_CH1 (PB4)

**Configuration:**
- TIM3_CH1 = PB4 (AF2), PWM Mode 1
- Prescaler: 169 → 1 MHz timer clock
- ARR: 19999 → 50 Hz (20 ms period)
- CCR1 range: 1000 (1.0 ms = 0°) … 2000 (2.0 ms = 180°)

**Angle → CCR1:**
```c
uint32_t ccr = 1000 + (uint32_t)(angle_deg * (1000.0f / 180.0f));
ccr = CLAMP(ccr, 1000, 2000);
TIM3->CCR1 = ccr;
```

**Servo as VU meter (M1):**
```
servo_deg = CLAMP(dist_filt_mm / 4000.0f * 180.0f, 0.0f, 180.0f)
```
Closer hand → lower angle; farther → higher angle.

**Failure handling:** If MPU or HC-SR04 fails, servo holds last position. No jitter.

### 1.5 Basic DAC Tone (M1 Audio Engine)

**Method:** TIM6 TRGO triggers DAC1_OUT1 (PA4) at a fixed sample rate. A simple square/sawtooth
waveform is generated by software counter in the TIM6 IRQ (no DMA in M1 — DMA added in M2).

**Alternatively (simpler M1):** Use TIM2 in PWM mode on a spare pin with RC filter, or use DAC
with TIM6 IRQ and a simple counter-based sine approximation. The key deliverable is an audible
tone at the correct pitch.

**Pitch mapping — distance → frequency (log scale):**

```
// Input:  dist_filt_mm in [20, 4000] mm
// Output: synth_hz in [110, 880] Hz
// Log mapping: f = f_min * (f_max/f_min)^((d - d_min)/(d_max - d_min))

float t = (dist_filt_mm - 20.0f) / (4000.0f - 20.0f);   // 0..1
t = CLAMP(t, 0.0f, 1.0f);
float synth_hz = 110.0f * powf(8.0f, t);                 // 110 * 8^t = 110..880 Hz
synth_hz = CLAMP(synth_hz, 110.0f, 880.0f);
```

> Rationale: 880/110 = 8 = 2³ (three octaves). `powf(8, t)` gives perceptually uniform pitch sweep.

**Octave gate — roll → pitch doubling:**
```c
if (fabsf(roll_deg) > 30.0f) {
    synth_hz *= 2.0f;
    synth_hz = CLAMP(synth_hz, 110.0f, 1760.0f);
}
```

**Frequency smoothing (1st-order IIR, α = 0.1):**
```c
filt_hz = 0.1f * synth_hz + 0.9f * filt_hz;
```

**DAC output (M1 simple implementation):**
- TIM6 configured at `filt_hz * N` where N = samples per cycle (e.g. N=32 for a wavetable).
- In TIM6 IRQ: increment phase counter, output `wavetable[phase % N]` to DAC1->DHR12R1.
- For M1 a simple 32-sample sine approximation (pre-computed at compile time) is sufficient.

**Audio failure handling:**
- If both MPU and HC-SR04 fail: `synth_hz` holds last safe value, audio continues at that pitch.
- `status.audio` = 1 if DAC/TIM6 init fails.

### 1.6 Telemetry — LPUART1 @ 115 200 8N1

**Format:** Newline-delimited JSON (NDJSON), ~30 Hz, non-blocking TX.

**JSON schema (M1 base):**
```json
{
  "t": 12345,
  "roll": -12.3,
  "pitch": 4.5,
  "ax": 0.01,
  "ay": -0.98,
  "az": 0.12,
  "dist_mm": 350,
  "temp_c": 23.4,
  "synth_hz": 220.0,
  "filt_hz": 215.3,
  "servo_deg": 15.75,
  "status": {"mpu": 0, "mcp": 0, "sr04": 0, "audio": 0, "servo": 0, "i2c_err": 0}
}
```

**Field definitions:**

| Field | Type | Description |
|-------|------|-------------|
| `t` | uint32 ms | `HAL_GetTick()` timestamp |
| `roll` | float °  | Roll from accel (±180°) |
| `pitch` | float °  | Pitch from accel (±90°) |
| `ax/ay/az` | float g | Raw accel in g |
| `dist_mm` | int mm | Filtered HC-SR04 distance |
| `temp_c` | float °C | MCP9808 ambient temperature |
| `synth_hz` | float Hz | Pre-smoothing pitch |
| `filt_hz` | float Hz | Post-smoothing pitch (drives DAC) |
| `servo_deg` | float °  | Current servo angle |
| `status.*` | int 0/1 | 0 = OK, 1 = fault |
| `status.i2c_err` | int | Cumulative I2C error count |

**TX implementation:**
- 256-byte static ring buffer in SRAM.
- `snprintf` into a local stack buffer (≤128 bytes), then copy to ring buffer.
- LPUART1 TX DMA (or TX-empty IRQ) drains ring buffer — never blocks main loop.
- If ring buffer full: drop frame (increment overflow counter, do not stall).

**Command parser (RX):**
- Line-buffered RX (LPUART1 RX IRQ or DMA).
- Parse `{"cmd":"selftest"}` → trigger BIST.
- Parse `{"cmd":"set","filt_hz":1200}` → override filter cutoff (M2).
- Unknown commands: silently ignored.

### 1.7 BIST — Serial Selftest

Command: `{"cmd":"selftest"}` over LPUART1.

**Sequence:**
1. **I2C scan:** Probe 0x68 (MPU-6050) and 0x18 (MCP9808). Report found/not-found.
2. **MPU WHO_AM_I:** Read reg 0x75 → verify 0x68. PASS/FAIL.
3. **MCP9808 Mfr ID:** Read reg 0x06 → verify 0x0054. PASS/FAIL.
4. **HC-SR04 ping:** Fire one trigger, wait up to 38 ms for echo. Report distance_mm or TIMEOUT.
5. **DAC tone burst:** Output 440 Hz for 200 ms. Report PASS (no way to verify acoustically in SW).
6. **Servo sweep:** Move to 0°, 90°, 180°, back to 0°. Report PASS.
7. **LED test:** Blink PA5 three times.

**Output format (one line per test):**
```
{"bist":"mpu_whoami","result":"PASS","val":"0x68"}
{"bist":"mcp_mfrid","result":"PASS","val":"0x0054"}
{"bist":"hcsr04","result":"PASS","val":"342mm"}
{"bist":"dac_tone","result":"PASS"}
{"bist":"servo","result":"PASS"}
{"bist":"led","result":"PASS"}
{"bist":"summary","pass":6,"fail":0}
```

**Status flags:** BIST sets `status.*` fields in telemetry for any FAIL result.

### 1.8 M1 Acceptance Criteria

| # | Criterion | Verification |
|---|-----------|--------------|
| AC-M1-1 | WHO_AM_I register reads 0x68 | BIST output + telemetry `status.mpu=0` |
| AC-M1-2 | MCP9808 Mfr ID reads 0x0054 | BIST output + telemetry `status.mcp=0` |
| AC-M1-3 | HC-SR04 returns plausible distance (20–4000 mm) | BIST ping + telemetry `dist_mm` |
| AC-M1-4 | Servo sweeps 0°→90°→180° without stall | BIST servo test + visual |
| AC-M1-5 | Audible tone changes pitch as hand moves | Acoustic + `synth_hz` in telemetry |
| AC-M1-6 | Octave gate fires when |roll| > 30° | Tilt board, observe `synth_hz` doubles |
| AC-M1-7 | Telemetry JSON flows at ~30 Hz | Count lines/sec on host |
| AC-M1-8 | Sensor fault → status flag set, no crash | Unplug HC-SR04, observe `status.sr04=1` |
| AC-M1-9 | `selftest` command triggers BIST | Send `{"cmd":"selftest"}`, read output |
| AC-M1-10 | `temp_c` plausible (15–40 °C) | Telemetry field |

---

## Milestone 2 — "CORDIC Synth"

### 2.1 Scope

Replace the M1 basic tone with a CORDIC-sine wavetable rendered via TIM6 TRGO + DAC DMA
(circular). Add FMAC IIR low-pass filter with tilt-swept cutoff, vibrato from CORDIC LFO, and
volume from distance. Implement the full `audio_engine.h` API.

### 2.2 CORDIC Sine Wavetable

**Wavetable:** 256 samples, `uint16_t`, 12-bit DAC range (0–4095).

**Generation (at init, using STM32G4 CORDIC hardware):**
```c
// CORDIC configured: function=SINE, precision=6 iterations, width=32-bit, scale=0
// Input: q1.31 angle in [-1, 1] representing [-π, π]
// Output: q1.31 sine value in [-1, 1]

for (int i = 0; i < 256; i++) {
    int32_t angle_q31 = (int32_t)((i * 2 - 256) * (INT32_MAX / 256));
    CORDIC->WDATA = (uint32_t)angle_q31;
    int32_t sine_q31 = (int32_t)CORDIC->RDATA;
    // Map [-1,1] q1.31 → [0, 4095] uint16_t
    wavetable[i] = (uint16_t)((sine_q31 + INT32_MAX) >> 20);  // 12-bit
}
```

**Placement:** `uint16_t wavetable[256]` in `.dma_buf` section (SRAM1, 4-byte aligned, accessible by DMA).

**Linker section:**
```ld
.dma_buf (NOLOAD) :
{
    . = ALIGN(4);
    *(.dma_buf)
    . = ALIGN(4);
} >SRAM1
```

### 2.3 TIM6 + DAC DMA (Circular)

**TIM6 configuration:**
- Clock: APB1 timer = 170 MHz
- Prescaler: 0 (no prescale)
- ARR: `(170000000 / 32000) - 1 = 5312 - 1 = 5311`
- TRGO: Update event → triggers DAC1 conversion
- Sample rate: 32 000 Hz (32 kHz)

**DAC1_OUT1 (PA4) configuration:**
- Trigger: TIM6 TRGO
- DMA: DMA1 Channel (DAC1_CH1 request), circular mode, memory→peripheral
- Transfer: `uint16_t` (half-word), 12-bit right-aligned (DHR12R1)
- Buffer: `wavetable[256]` — DMA wraps continuously

**Phase accumulator (updated in main loop or TIM6 IRQ — NOT in DMA TC IRQ):**
```c
// Phase step for desired frequency:
// phase_step = (freq_hz * 256) / 32000
// Updated atomically (32-bit write is atomic on Cortex-M4)
volatile uint32_t phase_accum = 0;   // Q16.16 fixed-point
volatile uint32_t phase_step  = 0;   // updated by audio_engine

// In DMA half/complete callback (or TIM6 IRQ):
// Refill output buffer from wavetable using phase_accum
// For circular DMA of the raw wavetable, frequency is set by adjusting
// the effective playback rate via a double-buffer or by recomputing
// wavetable content at frequency change.
```

**Practical approach for frequency control with circular DMA:**
- Use a **ping-pong (double) buffer** of 64 samples each.
- DMA half-complete and complete callbacks refill the inactive half.
- In the callback, advance `phase_accum += phase_step` per sample, index `wavetable[phase_accum >> 8]`.
- `phase_step = (uint32_t)((freq_hz / 32000.0f) * 256.0f * 65536.0f)` (Q16.16).
- Apply volume: `sample = (uint16_t)(wavetable[idx] * volume + 2048 * (1 - volume))` (centre at 2048).

**NVIC:** DMA1 DAC audio = priority 0 (highest).

### 2.4 FMAC IIR Low-Pass Filter

**Hardware:** STM32G4 FMAC (Filter Math Accelerator), Direct Form 1, q15 coefficients.

**Filter order:** 2nd-order Biquad (IIR DF1), sufficient for a smooth low-pass sweep.

**Coefficient generation (Butterworth, computed on-the-fly in `filter_set_cutoff`):**
```c
// Bilinear transform, 2nd-order Butterworth low-pass
// fs = 32000 Hz, fc = cutoff_hz
// Pre-warp: wc = 2 * fs * tan(π * fc / fs)
// Compute b0, b1, b2, a1, a2 in float, then convert to q15

float wc = 2.0f * 32000.0f * tanf(M_PI * fc / 32000.0f);
float K  = wc / 32000.0f;
float K2 = K * K;
float Q  = filter_q;   // default 0.707 (Butterworth)
float norm = 1.0f + K/Q + K2;

float b0 =  K2 / norm;
float b1 =  2.0f * b0;
float b2 =  b0;
float a1 = (2.0f * (K2 - 1.0f)) / norm;
float a2 = (1.0f - K/Q + K2) / norm;

// Convert to q15 (scale by 32767, clamp)
fmac_coeffs[0] = (int16_t)(b0 * 32767.0f);
fmac_coeffs[1] = (int16_t)(b1 * 32767.0f);
fmac_coeffs[2] = (int16_t)(b2 * 32767.0f);
fmac_coeffs[3] = (int16_t)(-a1 * 32767.0f);  // FMAC uses negated a coeffs
fmac_coeffs[4] = (int16_t)(-a2 * 32767.0f);
```

**FMAC buffers (SRAM1, q15, 64 samples each):**
```c
__attribute__((section(".dma_buf"), aligned(4)))
int16_t fmac_input_buf[64];
int16_t fmac_output_buf[64];
int16_t fmac_coeff_buf[5];   // b0, b1, b2, -a1, -a2
```

**Cutoff mapping — pitch_deg → cutoff_hz:**
```
// pitch_deg in [-45°, +45°] → cutoff_hz in [400, 4000] Hz
float t = (pitch_deg + 45.0f) / 90.0f;   // 0..1
t = CLAMP(t, 0.0f, 1.0f);
float cutoff_hz = 400.0f + t * (4000.0f - 400.0f);   // linear sweep
cutoff_hz = CLAMP(cutoff_hz, 400.0f, 4000.0f);
```

**FMAC failure fallback:** If FMAC init returns error or produces NaN output, bypass filter
(pass audio through unfiltered), set `status.audio` = 1. Ship CORDIC sine without filter.

### 2.5 Vibrato

**LFO:** 5 Hz sine via CORDIC (polled in main loop, not ISR).

```c
// LFO phase advances at 5 Hz in main loop (~30 Hz tick)
lfo_phase += (5.0f / 30.0f) * 256.0f;   // advance through 256-sample table
if (lfo_phase >= 256.0f) lfo_phase -= 256.0f;

// CORDIC sine of lfo_phase → lfo_sine in [-1, 1]
float lfo_sine = cordic_sine_fast(lfo_phase);

// Vibrato depth: depth_cents = |roll_deg| * (10.0f / 45.0f)
float depth_cents = fabsf(roll_deg) * (10.0f / 45.0f);
depth_cents = CLAMP(depth_cents, 0.0f, 10.0f);

// Apply vibrato to frequency (cents → ratio: 2^(cents/1200))
float vibrato_ratio = powf(2.0f, (depth_cents * lfo_sine) / 1200.0f);
float vibrato_hz = synth_hz * vibrato_ratio;
voice_set_freq(0, vibrato_hz);
```

### 2.6 Volume — Distance Mapping

```c
// Closer → louder (inverse, clamped 0..1)
// dist_filt_mm in [20, 4000] mm
float volume = 1.0f - (dist_filt_mm - 20.0f) / (4000.0f - 20.0f);
volume = CLAMP(volume, 0.0f, 1.0f);
voice_set_gain(0, volume);
```

### 2.7 Audio Engine API (frozen seam — implements `audio_engine.h`)

```c
// audio_engine.h  — shared contract, do not modify signatures
#define MAX_VOICES 4

void  audio_init(uint32_t sample_rate_hz);
// Initialises CORDIC wavetable, TIM6, DAC1, DMA, FMAC.
// sample_rate_hz must be 32000.

void  voice_set_freq(int v, float hz);
// Sets voice v frequency. v=0 is the main voice (Track A drives v=0).
// Thread-safe: updates volatile phase_step atomically.

void  voice_set_gain(int v, float g);
// Sets voice v gain [0..1]. Applied per-sample in DMA callback.

void  filter_set_cutoff(float hz);
// Recomputes FMAC biquad coefficients for new cutoff frequency.
// Called from main loop only (not ISR).

void  filter_set_q(float q);
// Sets filter Q factor. Default 0.707 (Butterworth).

void  audio_set_master_gain(float g);
// Master gain [0..1] applied after mixing all voices.
```

**Voice struct (internal):**
```c
typedef struct {
    volatile uint32_t phase_accum;   // Q16.16
    volatile uint32_t phase_step;    // Q16.16, updated atomically
    volatile float    gain;          // 0..1
    volatile uint8_t  active;
} Voice;

static Voice voices[MAX_VOICES];
```

**Rendering (DMA half/complete callback):**
```c
// Mix MAX_VOICES voices, apply FMAC filter, apply master gain
for (int s = 0; s < HALF_BUF; s++) {
    int32_t mix = 0;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].active) continue;
        uint8_t idx = voices[v].phase_accum >> 16;  // top 8 bits of Q16.16
        mix += (int32_t)(wavetable[idx] - 2048) * (int32_t)(voices[v].gain * 32767);
        voices[v].phase_accum += voices[v].phase_step;
    }
    mix = mix / 32767;   // normalise
    mix = (int32_t)(mix * master_gain);
    fmac_input_buf[s] = (int16_t)CLAMP(mix, -2048, 2047);
}
// Submit fmac_input_buf to FMAC, read fmac_output_buf, write to dac_out_buf
// (or bypass FMAC if not ready)
```

### 2.8 M2 Acceptance Criteria

| # | Criterion | Verification |
|---|-----------|--------------|
| AC-M2-1 | 440 Hz sine audible from STEMMA speaker | Acoustic + `synth_hz≈440` in telemetry |
| AC-M2-2 | FMAC filter cutoff sweeps with pitch tilt | Tilt board ±45°, observe tone brightness change |
| AC-M2-3 | No audio glitches during telemetry TX | Monitor `filt_hz` continuity while streaming |
| AC-M2-4 | Vibrato depth increases with |roll| | Roll board, listen for vibrato depth change |
| AC-M2-5 | Volume increases as hand approaches | Move hand 20→4000 mm, observe loudness |
| AC-M2-6 | `audio_engine.h` API compiles and links | Build succeeds, no undefined symbols |
| AC-M2-7 | BIST 440 Hz burst passes | `{"cmd":"selftest"}` → `"dac_tone":"PASS"` |
| AC-M2-8 | FMAC fallback: audio continues if FMAC fails | Force FMAC error in debug, verify audio continues |

---

## Sensor Failure Matrix

| Sensor | Failure Mode | Detection | Audio Behaviour | Telemetry |
|--------|-------------|-----------|-----------------|-----------|
| MPU-6050 | WHO_AM_I ≠ 0x68 | Init check | Hold last roll/pitch; no octave gate | `status.mpu=1` |
| MPU-6050 | I2C NACK/timeout | HAL error | Hold last accel values | `status.i2c_err++` |
| MCP9808 | Mfr ID ≠ 0x0054 | Init check | No effect on audio | `status.mcp=1` |
| MCP9808 | I2C NACK/timeout | HAL error | No effect on audio | `status.i2c_err++` |
| HC-SR04 | No echo (38 ms timeout) | TIM4 timeout | Hold last `dist_filt_mm` | `status.sr04=1` |
| HC-SR04 | Out-of-range reading | Clamp check | Clamp to [20, 4000] mm | Normal |
| DAC/TIM6 | Init failure | HAL error | Silent (muted) | `status.audio=1` |
| FMAC | Init/coeff error | HAL error | Bypass filter, raw CORDIC sine | `status.audio=1` |
| Servo | TIM3 init failure | HAL error | No effect on audio | `status.servo=1` |

---

## Smoothing Summary

| Signal | Filter | α | Notes |
|--------|--------|---|-------|
| `dist_filt_mm` | 1st-order IIR | 0.1 | Applied after clamp |
| `filt_hz` | 1st-order IIR | 0.1 | Applied after octave gate |
| `roll_deg` | None (M1) | — | Raw atan2 output |
| `pitch_deg` | None (M1) | — | Raw atan2 output |
| FMAC cutoff | Immediate | — | Coefficient recomputed on change |

---

## Telemetry Rate Budget

| Task | Period | Notes |
|------|--------|-------|
| Sensor read (I2C) | ~10 ms | MPU + MCP back-to-back |
| HC-SR04 ping | 60 ms | Min cycle per datasheet |
| Mapping + audio update | ~1 ms | Pure math |
| Telemetry TX | ~33 ms | ~30 Hz, non-blocking |
| Servo update | 20 ms | TIM3 PWM auto |
| Audio DMA | 8 ms | 256 samples @ 32 kHz |

All tasks run in the main super-loop except audio (DMA/IRQ) and echo capture (TIM4 IRQ).
No HAL_Delay in ISRs. No blocking I2C in audio callbacks.
