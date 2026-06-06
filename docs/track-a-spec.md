# Track A — Air-Synth Core Spec (Phase 1)

> Binding parent: `SEB.MD` SHARED CONTRACT (pin map, frozen `audio_engine.h`, telemetry/command
> schema, git rules). This document is the artifact for **REVIEW GATE 1**.
>
> **Track A owns** the physical board (NUCLEO-G474RE), the integration trunk, and `src/core/*` in the
> frontend. It delivers the Air-Synth instrument in always-demoable layers:
> **M1 "Reliable Hum"** (Approach 1) -> **M2 "CORDIC Synth"** (Approach 2).
>
> **Frozen seam:** Track A *implements* `audio_engine.h` on-target. Track B builds on top of it
> (host-sim + showpiece panels) and merges in at T-45. Track A must keep the seam signature frozen.

---

## 0. Milestone overview

| Milestone | Headline | Audio path | Demoable result |
|-----------|----------|------------|-----------------|
| **M0** (bring-up, no gate) | Blink + heartbeat | none | LD2 blinks; `{"t":..,"hb":1}` JSON on serial @115200 |
| **M1** "Reliable Hum" | Sensors + simple tone | TIM/PWM or basic DAC tone, pitch=f(distance), octave/gate=f(tilt) | Hand height bends pitch; tilt gates/octaves; servo+LED VU; full telemetry; `selftest` BIST |
| **M2** "CORDIC Synth" | DSP-silicon voice | CORDIC sine wavetable -> DAC1/TIM6/DMA; FMAC IIR LPF cutoff=f(tilt); vibrato/vol=f(tilt) | Clean parameterized voice via `audio_engine.h` (voice 0), filter sweeps with tilt |

Each milestone is independently flashable and demoable. M1 ships first and remains the fallback demo
if M2 DSP work runs long.

---

## 1. Frozen seam (from `SEB.MD` - implemented here, never re-signed)

```c
// audio_engine.h  (MAX_VOICES = 4)
void  audio_init(uint32_t sample_rate_hz);   // 32000 on-target (TIM6 TRGO DAC clock)
void  voice_set_freq(int v, float hz);       // A drives v=0; B uses 0..MAX_VOICES-1
void  voice_set_gain(int v, float g);        // 0..1
void  filter_set_cutoff(float hz);           // FMAC
void  filter_set_q(float q);
void  audio_set_master_gain(float g);
// rendering is timer/DMA driven internally (CORDIC sine -> wavetable -> DAC DMA)
```

- **M1** may back this with a simple timer/PWM tone or basic DAC write (the "Reliable Hum"), as long
  as the public signatures hold.
- **M2** replaces the internals with the CORDIC->wavetable->DAC/DMA engine + FMAC filter. No caller
  changes; only `audio_engine.c` internals change.
- Header lives canonically at `include/audio_engine.h` (matches Track B's materialized copy; trivial
  de-dupe at merge since the signature is frozen).

---

## 2. Pin / peripheral map (authoritative, from `SEB.MD`)

| Function | Peripheral | Pin(s) | Notes |
|----------|-----------|--------|-------|
| Telemetry UART | LPUART1 | PA2 TX / PA3 RX | 115200 8N1 -> ST-LINK VCP, **non-blocking TX** |
| Audio out | DAC1_OUT1 | **PA4** | -> STEMMA speaker amp. NEVER PA5 |
| Status LED | GPIO | PA5 (LD2) | heartbeat / VU |
| I2C sensors | I2C1 | SCL PB8 / SDA PB9 | 400 kHz, 3V3, MPU-6050 @0x68 + MCP9808 @0x18 |
| Servo | TIM3_CH1 | PB4 | 50 Hz PWM, 1.0-2.0 ms |
| HC-SR04 trig | GPIO | PB5 | 10 us pulse |
| HC-SR04 echo | TIM4_CH1 | PB6 | input capture IRQ, 5V->3V3 divider |
| Extra LEDs (opt) | GPIO | PC7, PB3 | VU garnish |

**Peripheral allocation:** TIM6 = audio sample clock (32 kHz TRGO); TIM3 = servo PWM; TIM4 = echo
capture; DAC1 = audio; DMA1 = audio (+ optional LPUART TX); CORDIC = sine + atan2 (PHASE);
FMAC = IIR LPF. TIM7 is intentionally **left free** for Track B's sequencer at integration.

---

## 3. Sensor -> sound mapping math (ranges, clamps, smoothing)

All mappings clamp inputs to a working range, then apply a one-pole smoother
`y += a*(target - y)` with `a = 1 - expf(-dt/tau)` (per-mapping `tau` below). `dt` is the sensor-loop
period (~33 ms at 30 Hz). All smoothing is **clamp-then-smooth** so smoothed values never exceed
rails.

### 3.1 Distance -> pitch  (`synth_hz`, M1 + M2)
- Source: HC-SR04 echo width -> `distance_mm` (see section 5.1).
- Working range: **`D_MIN = 50 mm` ... `D_MAX = 500 mm`** (clamp outside).
- Musical (exponential) map over **2 octaves**, base **A3 = 220 Hz**:
  ```
  d   = clamp(distance_mm, D_MIN, D_MAX)
  u   = (d - D_MIN) / (D_MAX - D_MIN)        # 0..1, near hand = low by default
  hz  = 220.0f * exp2f(OCTAVES * u)          # OCTAVES = 2.0 -> 220..880 Hz
  ```
  (Direction configurable; default: closer hand = lower pitch.)
- Smoothing: `tau_pitch = 0.06 s` (responsive but de-jittered). Applied to `hz` (or to `u`).

### 3.2 Tilt -> octave / gate  (M1)
- Source: MPU-6050 accel -> roll/pitch (see section 5.2).
- **Gate:** if `pitch_deg < GATE_OFF_DEG` (e.g. nose-down past -60deg) -> mute
  (`audio_set_master_gain(0)` ramped); else gate on. Provides an intentional "silence" gesture and a
  safe default.
- **Octave shift:** quantize `roll_deg` into bins -> `octave in {-1, 0, +1}`; multiply `hz` by
  `2^octave`. Hysteresis on bin edges to avoid flutter.

### 3.3 Tilt -> filter cutoff (M2)
- `pitch_deg` mapped to log-frequency cutoff:
  ```
  p    = clamp(pitch_deg, -45.0f, +45.0f)
  uc   = (p + 45.0f) / 90.0f                 # 0..1
  fc   = 200.0f * exp2f(uc * 5.32f)          # 200 Hz .. ~8 kHz (5.32 = log2(8000/200))
  filter_set_cutoff(fc)
  ```
- Smoothing: `tau_cutoff = 0.10 s`. FMAC coefficients recomputed only when `fc` moves > 1 % (avoids
  needless stop/restart, see section 6.3).

### 3.4 Tilt -> vibrato / volume (M2)
- `roll_deg` -> vibrato depth (0..~30 cents) and rate (~5 Hz LFO) and/or master gain trim. Vibrato
  modulates voice-0 frequency around the mapped `hz`. `tau_vib = 0.12 s`.
- Master gain stays <= 1.0; clamp to avoid DAC clipping.

### 3.5 VU expression (M1)
- Servo angle = f(amplitude or distance): `servo_deg = map(level, 0..1, 0..180)`, slew-limited.
- LD2 / extra LEDs brightness or blink rate = f(level/gate).

---

## 4. Telemetry & command schema (contract - base set is Track A's responsibility)

Newline-delimited JSON, ~30 Hz, **non-blocking TX** (DMA/IRQ ring buffer; must never stall audio).

```json
{"t":ms,"roll":f,"pitch":f,"ax":f,"ay":f,"az":f,"dist_mm":i,"temp_c":f,
 "synth_hz":f,"filt_hz":f,"servo_deg":f,
 "status":{"mpu":0,"mcp":0,"sr04":0,"audio":0,"servo":0,"i2c_err":0}}
```
- `status` flags: `0 = OK/present`, non-zero = fault/absent (also drives FE red indicators).
- Track B extensions (`detune_c`, `seq`, `voices`) are appended later; older FE ignores unknown
  fields. Track A leaves room but does not emit them in M1/M2.

Commands (FE -> board, one JSON object per line):
- `{"cmd":"selftest"}` -> run BIST, print PASS/FAIL per subsystem, set status flags.
- `{"cmd":"set","filt_hz":1200}` / `{"cmd":"set","master":0.8}` -> parameter override.
- (reserved) `{"cmd":"seq",...}` handled post-integration.

---

## 5. Sensor drivers - concrete config (from research)

### 5.1 HC-SR04 (TIM4_CH1 input capture)
- TIM4: **PSC = 169** -> 1 MHz (1 us/tick) at 170 MHz timer clock; **ARR = 65535** -> 65.5 ms window
  (> 38 ms HC-SR04 timeout).
- TRIG (PB5): drive a **10 us** high pulse to start a ping.
- ECHO (PB6 = TIM4_CH1, AF2): capture rising edge (`t_rise`), flip polarity, capture falling edge
  (`t_fall`). `echo_us = (t_fall - t_rise) & 0xFFFF`.
- Distance: `distance_mm = (echo_us * 10) / 58` (58 us/cm). Valid 20-4000 mm.
- **Timeout / no-echo:** no falling edge within 38 ms -> `status.sr04 = 1`, hold last value (audio
  does not jump).
- NVIC: `TIM4_IRQn` priority **1** (below audio DMA, above telemetry).

### 5.2 MPU-6050 (I2C1 @ 0x68)
- Init: `WHO_AM_I (0x75) == 0x68`; write `PWR_MGMT_1 (0x6B) = 0x00` (clear sleep);
  `ACCEL_CONFIG (0x1C) = 0x00` (+-2 g, **16384 LSB/g**); `GYRO_CONFIG (0x1B) = 0x00` (+-250 deg/s).
- Read: burst 14 bytes from `ACCEL_XOUT_H (0x3B)` -> ax,ay,az,temp,gx,gy,gz.
- Tilt: `roll = atan2(ay, az)`, `pitch = atan2(-ax, sqrt(ay^2+az^2))`. **CORDIC PHASE** function
  computes atan2 in hardware (q1.31) - preferred over software `atan2f`.
- Fault: WHO_AM_I mismatch or NAK -> `status.mpu = 1`, freeze tilt at neutral (0deg), gate audio safe.

### 5.3 MCP9808 (I2C1 @ 0x18)
- Verify `MFR_ID (0x06) == 0x0054`, `DEV_ID (0x07)` high byte `== 0x04`.
- Read `T_AMBIENT (0x05)` 16-bit: mask `& 0x1FFF`; if `& 0x1000` -> `temp_c = (w & 0x0FFF)*0.0625 - 256`
  else `temp_c = w * 0.0625`. (0.0625 C/LSB.)
- Fault: ID mismatch/NAK -> `status.mcp = 1`, `temp_c` holds last; Track B detune disabled downstream.

### 5.4 SG90 servo (TIM3_CH1)
- TIM3: **PSC = 169** (1 MHz), **ARR = 19999** -> exactly 50 Hz.
- CCR (pulse us): 0deg -> **1000**, 90deg -> **1500**, 180deg -> **2000**.
  `CCR = 1000 + (uint32_t)(angle_deg * 1000.0f / 180.0f)`, `angle in [0,180]`.

---

## 6. Audio engine - concrete config (M2, from research)

### 6.1 CORDIC sine wavetable
- CORDIC `FUNC = SINE`, `PRECISION = 6 cycles` (~20-bit), q1.31 in/out.
- **Precompute once at startup** a **1024-entry `int16_t` (q1.15)** wavetable via CORDIC (DMA burst),
  not per-sample. Cost 2 KB SRAM1. Phase angle scaling: map table index `i in [0,N)` to q1.31
  `[-pi,pi)`.
- Runtime oscillator = phase accumulator; `phase_inc = freq * N / 32000`; sample = `wavetable[phase >>
  PHASE_SHIFT]`. (Pure integer in ISR.)

### 6.2 DAC1 + TIM6 + DMA (32 kHz)
- TIM6 TRGO: **PSC = 0, ARR = 5311** -> 32,012 Hz (0.04 % error); MMS = update->TRGO.
- DAC1 CH1 (PA4): `TSEL = TIM6 TRGO`, `TEN1`, `DMAEN1`, `WAVE = 0` (manual feed), `EN1`.
- DMA1 **Channel 3** (DMAMUX DAC1_CH1), circular, 16-bit, mem->periph, dest `&DAC1->DHR12R1`.
- Double-buffer: `uint16_t audio_buf[2*64] __attribute__((aligned(4)))` in **SRAM1** (CCM is **not**
  DMA-accessible). 64 samples/half = 2 ms latency. Half-transfer IRQ refills `[0..63]`,
  transfer-complete IRQ refills `[64..127]`.
- q1.15 -> 12-bit DAC: `dac_code = (uint16_t)((sample + 32768) >> 4)`.

### 6.3 FMAC IIR low-pass (cutoff = f(tilt))
- `FMAC_FUNC_IIR_DIRECT_FORM_1`, single biquad (B taps N=3, A taps M=2), `Clip = ENABLED`.
- Coeffs: RBJ LPF at fs=32 k -> normalize by a0 -> **negate a1,a2** (FMAC wants -a) -> **prescale /2 with
  gain R=1** (handles `|a1|>1` at low cutoff) -> q1.15.
- Headroom: keep input <= -6 dB (<= 50 % full scale) since passband gain ~= 2^R.
- Runtime sweep: `FilterStop -> FilterConfig(new coeffs) -> FilterPreload -> FilterStart`. 2-3 sample
  glitch, inaudible at tilt update rates. Only recompute when `fc` changes > 1 %.
- **Fallback (if FMAC unstable):** `arm_biquad_cascade_df1_f32` (CMSIS-DSP), same `{b0,b1,b2,-a1,-a2}`
  convention, ~15-20 cycles/sample on M4F - ship CORDIC sine *without* hardware filter if needed.

---

## 7. Failure behavior (graceful degradation - per `SEB.MD`)

| Condition | Detection | Telemetry | Audio/actuator response |
|-----------|-----------|-----------|-------------------------|
| MPU-6050 absent/err | WHO_AM_I!=0x68 / I2C NAK | `status.mpu=1`, `i2c_err++` | tilt = neutral; cutoff/octave use safe defaults; instrument still plays distance->pitch |
| MCP9808 absent/err | ID mismatch / NAK | `status.mcp=1`, `i2c_err++` | `temp_c` holds last; no effect on M1/M2 core; Track-B detune disabled |
| HC-SR04 no echo | no falling edge <=38 ms | `status.sr04=1` | hold last `dist_mm`; pitch holds (no jump); optionally fade gate |
| I2C bus stuck | HAL error / timeout | `i2c_err++` | bus recovery (clock pulses / reinit); sensors flagged until back |
| Audio fault | DMA error / init fail | `status.audio=1` | `audio_set_master_gain(0)`; never emit garbage to DAC |
| Servo unpowered | (no electrical sense) | `status.servo` best-effort | command continues; LD4 overcurrent -> external 5V (hardware note) |

**Invariant:** any sensor loss -> status flag red + audio holds last/safe; the headline kit
(IMU -> servo + LED) still demos even with no speaker.

---

## 8. On-target BIST (`selftest` command)

Runs on `{"cmd":"selftest"}`; prints `PASS/FAIL` per subsystem and sets `status` flags:
1. **I2C scan** - MPU `WHO_AM_I==0x68`; MCP `MFR_ID==0x0054`.
2. **DAC tone** - 440 Hz / 200 ms burst on PA4.
3. **Servo sweep** - min -> center -> max -> center.
4. **HC-SR04 ping** - one measurement, print mm.
5. **LED test** - LD2 (+ extra LEDs) blink.

---

## 9. Acceptance criteria

### 9.1 M0 (bring-up)
| ID | Criterion |
|----|-----------|
| M0-AC1 | LD2 blinks at a visible rate. |
| M0-AC2 | `{"t":<ms>,"hb":1}` NDJSON appears on VCP @115200, ~1 Hz, parseable. |

### 9.2 M1 "Reliable Hum"
| ID | Criterion |
|----|-----------|
| M1-AC1 | I2C1 brings up MPU-6050 + MCP9808; `selftest` reports both present. |
| M1-AC2 | `dist_mm` tracks hand height 50-500 mm; out-of-range clamps; no-echo sets `status.sr04=1`. |
| M1-AC3 | Audible tone whose pitch follows distance per 3.1 (220-880 Hz), de-jittered (smoothing). |
| M1-AC4 | Tilt gates audio and shifts octave per 3.2 with hysteresis (no flutter at bin edges). |
| M1-AC5 | Servo + LED act as VU per 3.5. |
| M1-AC6 | Full base telemetry JSON emitted ~30 Hz, non-blocking (audio/tone never stalls during TX). |
| M1-AC7 | `selftest` BIST runs all 5 checks and sets status flags correctly. |
| M1-AC8 | Unplug HC-SR04 -> `status.sr04` red, audio holds last/safe (graceful degradation). |

### 9.3 M2 "CORDIC Synth"
| ID | Criterion |
|----|-----------|
| M2-AC1 | DAC1 outputs a CORDIC-wavetable sine at 32 kHz via TIM6/DMA; measured pitch = mapped `synth_hz` within +-3 cents. |
| M2-AC2 | Wavetable matches a reference sine (host unit test, section 10) within tolerance; no audible aliasing at top of range. |
| M2-AC3 | FMAC IIR LPF cutoff follows tilt per 3.3 (200 Hz-8 kHz); sweep is click-free at tilt rates. |
| M2-AC4 | Vibrato/volume respond to roll per 3.4. |
| M2-AC5 | Voice 0 fully driven through frozen `audio_engine.h` API (no internal pokes from mapping layer). |
| M2-AC6 | Audio glitch-free: telemetry TX non-blocking; no `HAL_Delay`/heavy work in ISRs; shared vars `volatile`. |
| M2-AC7 | FMAC fallback path (software biquad) verified to drop in if hardware filter is disabled. |

---

## 10. Host unit-test targets (Phase 3a - written before code)

Pure-logic, compiled on host (clang, no hardware):
- `distance->hz` and `tilt->cutoff/vibrato` mapping: ranges, clamps, smoothing step-response.
- CORDIC angle scaling & wavetable correctness vs `sin()` reference (per-entry error bound).
- FMAC q1.15 coefficient generation (RBJ -> normalize -> negate-a -> prescale) vs reference.
- Telemetry serialize + command parse round-trip.

(On-target BIST in section 8 covers the hardware half; full TEST MATRIX is produced in Phase 3.)

---

## 11. Frontend scope (Phase 5 - `src/core/*`, summary; detail in plan)

Vite + React + uPlot + Web Serial, zero backend, localhost. Connect/disconnect (user gesture),
NDJSON parser, **StatusPanel** (per-sensor present/OK/age + i2c_err + audio/servo), charts
(roll/pitch, dist_mm, temp_c, synth_hz, servo_deg), M2 **SynthScope** + cutoff gauge, command box.
Renders Track B showpiece panels only if their telemetry fields appear. Track A does **not** touch
`src/showpiece/*`.

---

## 12. Defaults chosen (override any at review)

1. Distance window **50-500 mm**, **2-octave** exponential map from **A3 = 220 Hz**, closer = lower.
2. Cutoff map **200 Hz-8 kHz** from pitch **-45deg...+45deg**; smoothing taus per section 3.
3. Audio **32 kHz**, 1024-entry q1.15 wavetable, 64-sample double-buffer in SRAM1.
4. FMAC single biquad, prescale /2 / R=1; CMSIS-DSP float biquad as fallback.
5. Telemetry **115200** 8N1 (raise to 921600 later for a smoother scope).

---

## 13. Sources

- **RM0440** (STM32G4 ref manual): section 17 CORDIC, 21 FMAC, 22 DAC, 11 DMA/DMAMUX, 28-29 TIM3/4/6,
  35 I2C.
- **STM32CubeG4** examples: `CORDIC_CosSin`, `CORDIC_Sin_DMA`, `FMAC_IIR_ITToPolling`,
  `DAC_*`, `TIM_InputCapture`, `TIM_PWMOutput`.
- **AN5305** (FMAC DSP), **AN5325** (CORDIC).
- VictorTagayun `NUCLEO-G474RE_RealTime_FIR_IIR_FMAC` (FMAC polling pattern @50 kHz).
- Component datasheets: MPU-6050 PS rev 3.4, MCP9808 DS20005095B, HC-SR04, SG90.
- Web Serial: MDN + developer.chrome.com/docs/capabilities/serial; uPlot `leeoniya/uPlot`.

---

**REVIEW GATE 1 - review/approve this spec before Phase 2 plan.**
