# Track A — Air-Synth Implementation Plan
**Branch:** `track-a` | **Phase 2 / Gate 2**
**Target:** STM32G474RET6 on NUCLEO-G474RE (MB1367)
**Author:** Seb agent

---

## 1. Module Breakdown

```
src/
├── sensors/
│   ├── mpu6050.c / mpu6050.h       — MPU-6050 I2C driver + accel→roll/pitch
│   ├── mcp9808.c / mcp9808.h       — MCP9808 I2C temperature driver
│   └── hcsr04.c  / hcsr04.h        — HC-SR04 TIM4 input-capture ISR + distance_mm
├── audio_engine/
│   └── audio_engine.c / audio_engine.h  — CORDIC wavetable, TIM6+DMA, FMAC IIR, voice struct
├── mapping/
│   └── mapping.c / mapping.h       — sensor values → audio params (pure math, host-testable)
├── telemetry/
│   └── telemetry.c / telemetry.h   — NDJSON serialiser, LPUART1 DMA ring buffer TX, command parser
├── bist/
│   └── bist.c / bist.h             — selftest routine
└── main.c                          — super-loop: read sensors → mapping → set audio params → telemetry TX
```

---

## 2. Module Specifications

### 2.1 `src/sensors/mpu6050.h` / `mpu6050.c`

**Public API:**
```c
typedef struct {
    float ax_g, ay_g, az_g;   // accelerometer in g (±2 g range)
    float roll_deg, pitch_deg; // computed from accel
    float temp_c;              // MPU die temperature (informational)
    uint8_t ok;                // 1 = healthy, 0 = fault
} MPU6050_Data;

HAL_StatusTypeDef mpu6050_init(I2C_HandleTypeDef *hi2c);
// Verifies WHO_AM_I (0x75) == 0x68, writes PWR_MGMT_1 (0x6B) = 0x00,
// writes ACCEL_CONFIG (0x1C) = 0x00 (±2 g).
// Returns HAL_OK or HAL_ERROR. Sets status flag on error.

HAL_StatusTypeDef mpu6050_read(I2C_HandleTypeDef *hi2c, MPU6050_Data *out);
// Burst-reads 14 bytes from 0x3B (ACCEL_XOUT_H).
// Computes ax/ay/az in g, roll/pitch via atan2f, die temp.
// On I2C error: increments i2c_err, returns HAL_ERROR, out->ok = 0.
```

**Key constants:**
```c
#define MPU6050_ADDR        (0x68 << 1)   // HAL uses 8-bit address
#define MPU6050_REG_WHOAMI  0x75
#define MPU6050_REG_PWR1    0x6B
#define MPU6050_REG_ACCEL   0x1C
#define MPU6050_REG_DATA    0x3B          // ACCEL_XOUT_H, burst 14 bytes
#define MPU6050_WHOAMI_VAL  0x68
#define MPU6050_ACCEL_SENS  16384.0f      // LSB/g at ±2 g
```

**Roll/pitch computation:**
```c
out->ax_g = (int16_t)((buf[0]<<8)|buf[1]) / MPU6050_ACCEL_SENS;
out->ay_g = (int16_t)((buf[2]<<8)|buf[3]) / MPU6050_ACCEL_SENS;
out->az_g = (int16_t)((buf[4]<<8)|buf[5]) / MPU6050_ACCEL_SENS;
out->roll_deg  = atan2f(out->ay_g, out->az_g) * (180.0f / M_PI);
out->pitch_deg = atan2f(-out->ax_g,
                         sqrtf(out->ay_g*out->ay_g + out->az_g*out->az_g))
                 * (180.0f / M_PI);
out->temp_c = (int16_t)((buf[6]<<8)|buf[7]) / 340.0f + 36.53f;
```

**Error handling:** On `HAL_I2C_Master_Transmit` or `HAL_I2C_Master_Receive` failure:
- Retry once (single re-attempt).
- If still failing: set `out->ok = 0`, return `HAL_ERROR`.
- Caller holds last valid `MPU6050_Data`.

---

### 2.2 `src/sensors/mcp9808.h` / `mcp9808.c`

**Public API:**
```c
typedef struct {
    float temp_c;   // ambient temperature in °C
    uint8_t ok;     // 1 = healthy, 0 = fault
} MCP9808_Data;

HAL_StatusTypeDef mcp9808_init(I2C_HandleTypeDef *hi2c);
// Reads Mfr ID (0x06) → verify 0x0054.
// Reads Device ID (0x07) → verify 0x0400.
// Returns HAL_OK or HAL_ERROR.

HAL_StatusTypeDef mcp9808_read(I2C_HandleTypeDef *hi2c, MCP9808_Data *out);
// Reads Tamb register (0x05), 2 bytes MSB first.
// Parses sign + 12-bit magnitude / 16.0f.
```

**Key constants:**
```c
#define MCP9808_ADDR        (0x18 << 1)
#define MCP9808_REG_TAMB    0x05
#define MCP9808_REG_MFRID   0x06
#define MCP9808_REG_DEVID   0x07
#define MCP9808_MFRID_VAL   0x0054
#define MCP9808_DEVID_VAL   0x0400
```

**Tamb parse:**
```c
uint16_t raw = (buf[0] << 8) | buf[1];
int neg = (raw & 0x1000) ? 1 : 0;
raw &= 0x0FFF;
out->temp_c = neg ? (raw / 16.0f - 256.0f) : (raw / 16.0f);
```

---

### 2.3 `src/sensors/hcsr04.h` / `hcsr04.c`

**Public API:**
```c
void     hcsr04_init(TIM_HandleTypeDef *htim4);
// Configures TIM4_CH1 (PB6) input capture, rising+falling edge toggle.
// Enables TIM4_IRQn at NVIC priority 1.

void     hcsr04_trigger(void);
// Asserts PB5 HIGH for 10 µs, then LOW. Starts capture window.
// Must be called from main loop only (not ISR). Min 60 ms between calls.

uint32_t hcsr04_get_distance_mm(void);
// Returns last filtered distance_mm. Thread-safe (volatile read).

uint8_t  hcsr04_is_valid(void);
// Returns 1 if last measurement was within timeout, 0 if timeout/fault.
```

**Internal state (file-scope, volatile):**
```c
static volatile uint32_t s_t_rise   = 0;
static volatile uint32_t s_echo_us  = 0;
static volatile uint8_t  s_state    = 0;   // 0=idle, 1=waiting_rise, 2=waiting_fall
static volatile uint8_t  s_valid    = 0;
static volatile uint32_t s_dist_mm  = 0;   // filtered, Q16.0
static volatile uint32_t s_timeout_tick = 0;
```

**TIM4 IRQ handler (`TIM4_IRQHandler`):**
```c
void TIM4_IRQHandler(void) {
    if (TIM4->SR & TIM_SR_CC1IF) {
        TIM4->SR &= ~TIM_SR_CC1IF;
        if (s_state == 1) {                    // rising edge
            s_t_rise = TIM4->CCR1;
            // Reconfigure for falling edge
            TIM4->CCER &= ~TIM_CCER_CC1P;     // clear polarity → falling
            TIM4->CCER |=  TIM_CCER_CC1NP;
            s_state = 2;
        } else if (s_state == 2) {             // falling edge
            uint32_t t_fall = TIM4->CCR1;
            uint32_t echo_us = (t_fall >= s_t_rise)
                               ? (t_fall - s_t_rise)
                               : (0xFFFF - s_t_rise + t_fall + 1);
            uint32_t raw_mm = (echo_us * 10) / 58;
            raw_mm = (raw_mm < 20) ? 20 : (raw_mm > 4000) ? 4000 : raw_mm;
            // 1st-order IIR: α=0.1 (integer approx: new = (old*9 + raw) / 10)
            s_dist_mm = (s_dist_mm * 9 + raw_mm) / 10;
            s_valid = 1;
            s_state = 0;
        }
    }
    // Overflow flag: timeout detection handled in main loop
}
```

**Timeout detection (main loop, called after trigger):**
```c
if (s_state != 0 && (HAL_GetTick() - s_timeout_tick) > 38) {
    s_state = 0;
    s_valid = 0;   // status.sr04 = 1 set by caller
}
```

**TIM4 hardware configuration:**
- Prescaler: 169 (→ 1 MHz, 1 µs/tick)
- ARR: 0xFFFF
- CC1: input capture on TI1, initially rising edge
- No DMA (IRQ only)

---

### 2.4 `src/audio_engine/audio_engine.h` / `audio_engine.c`

**Public API (frozen — shared contract):**
```c
#define MAX_VOICES 4

void  audio_init(uint32_t sample_rate_hz);
void  voice_set_freq(int v, float hz);
void  voice_set_gain(int v, float g);
void  filter_set_cutoff(float hz);
void  filter_set_q(float q);
void  audio_set_master_gain(float g);
```

**Internal structures:**
```c
typedef struct {
    volatile uint32_t phase_accum;   // Q16.16 fixed-point
    volatile uint32_t phase_step;    // Q16.16; freq_hz * 256 / Fs * 65536
    volatile uint32_t gain_q15;      // gain * 32767, updated atomically
    volatile uint8_t  active;
} Voice;

static Voice voices[MAX_VOICES];
static volatile float master_gain = 1.0f;
static volatile float filter_q    = 0.707f;
```

**Wavetable and DMA buffers (SRAM1, `.dma_buf` section):**
```c
__attribute__((section(".dma_buf"), aligned(4)))
static uint16_t wavetable[256];

__attribute__((section(".dma_buf"), aligned(4)))
static uint16_t dac_buf[128];   // ping-pong: [0..63] = ping, [64..127] = pong

__attribute__((section(".dma_buf"), aligned(4)))
static int16_t fmac_in[64];

__attribute__((section(".dma_buf"), aligned(4)))
static int16_t fmac_out[64];

__attribute__((section(".dma_buf"), aligned(4)))
static int16_t fmac_coeffs[5];  // b0, b1, b2, -a1, -a2 in q15
```

**`audio_init` sequence:**
1. Generate CORDIC sine wavetable (256 samples, 12-bit DAC range).
2. Configure TIM6: PSC=0, ARR=5311, TRGO=Update.
3. Configure DAC1_OUT1 (PA4): trigger=TIM6, DMA circular, half-word.
4. Start DMA: `HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t*)dac_buf, 128, DAC_ALIGN_12B_R)`.
5. Configure FMAC: IIR DF1, load default coefficients (fc=1000 Hz, Q=0.707).
6. Enable DMA half-complete and complete callbacks.

**DMA callback (fills inactive half of `dac_buf`):**
```c
void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef *hdac) {
    audio_fill_buf(dac_buf, 0, 64);       // fill ping half
}
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac) {
    audio_fill_buf(dac_buf, 64, 64);      // fill pong half
}

static void audio_fill_buf(uint16_t *buf, uint32_t offset, uint32_t len) {
    for (uint32_t s = 0; s < len; s++) {
        int32_t mix = 0;
        for (int v = 0; v < MAX_VOICES; v++) {
            if (!voices[v].active) continue;
            uint8_t idx = (uint8_t)(voices[v].phase_accum >> 16);
            int32_t sample = (int32_t)wavetable[idx] - 2048;
            mix += (sample * (int32_t)voices[v].gain_q15) >> 15;
            voices[v].phase_accum += voices[v].phase_step;
        }
        mix = (int32_t)(mix * master_gain);
        mix = (mix < -2048) ? -2048 : (mix > 2047) ? 2047 : mix;
        fmac_in[s] = (int16_t)mix;
    }
    // Run FMAC (or bypass if not ready)
    if (fmac_ready) {
        fmac_process(fmac_in, fmac_out, len);
        for (uint32_t s = 0; s < len; s++)
            buf[offset + s] = (uint16_t)(fmac_out[s] + 2048);
    } else {
        for (uint32_t s = 0; s < len; s++)
            buf[offset + s] = (uint16_t)(fmac_in[s] + 2048);
    }
}
```

**`voice_set_freq` (thread-safe):**
```c
void voice_set_freq(int v, float hz) {
    if (v < 0 || v >= MAX_VOICES) return;
    hz = (hz < 20.0f) ? 20.0f : (hz > 16000.0f) ? 16000.0f : hz;
    uint32_t step = (uint32_t)((hz / 32000.0f) * 256.0f * 65536.0f);
    voices[v].phase_step = step;   // 32-bit write is atomic on Cortex-M4
    voices[v].active = 1;
}
```

**`filter_set_cutoff` (main loop only — not ISR safe):**
Recomputes biquad coefficients using bilinear transform (see spec §2.4), writes to FMAC.

---

### 2.5 `src/mapping/mapping.h` / `mapping.c`

**Purpose:** Pure math, no HAL dependencies. Independently compilable on host for unit tests.

**Public API:**
```c
typedef struct {
    float dist_filt_mm;   // smoothed distance
    float roll_deg;
    float pitch_deg;
    float temp_c;
} SensorState;

typedef struct {
    float synth_hz;       // pre-smoothing pitch
    float filt_hz;        // post-smoothing pitch (drives DAC)
    float cutoff_hz;      // FMAC cutoff
    float servo_deg;      // servo angle
    float volume;         // 0..1
    float vibrato_depth_cents;
} AudioParams;

void mapping_init(void);
void mapping_update(const SensorState *s, AudioParams *out);
// Applies all mapping math: distance→pitch, octave gate, IIR smoothing,
// tilt→cutoff, tilt→vibrato, distance→volume, distance→servo.
```

**All mapping constants defined as macros:**
```c
#define MAP_DIST_MIN_MM     20.0f
#define MAP_DIST_MAX_MM     4000.0f
#define MAP_FREQ_MIN_HZ     110.0f
#define MAP_FREQ_MAX_HZ     880.0f
#define MAP_OCTAVE_ROLL_DEG 30.0f
#define MAP_IIR_ALPHA       0.1f
#define MAP_CUTOFF_MIN_HZ   400.0f
#define MAP_CUTOFF_MAX_HZ   4000.0f
#define MAP_PITCH_MIN_DEG   (-45.0f)
#define MAP_PITCH_MAX_DEG   45.0f
#define MAP_VIBRATO_MAX_CENTS 10.0f
#define MAP_VIBRATO_ROLL_MAX  45.0f
```

**No global state except IIR filter state (static inside `mapping_update`).**

---

### 2.6 `src/telemetry/telemetry.h` / `telemetry.c`

**Public API:**
```c
void telemetry_init(UART_HandleTypeDef *huart);
// Initialises LPUART1 DMA TX, 256-byte ring buffer.

void telemetry_send(const SensorState *s, const AudioParams *a,
                    const StatusFlags *f);
// Serialises to NDJSON, enqueues in ring buffer. Non-blocking.
// Drops frame silently if ring buffer full.

void telemetry_poll(void);
// Called from main loop. Kicks DMA TX if idle and data available.

void telemetry_rx_irq(void);
// Called from LPUART1 IRQ. Accumulates RX bytes into line buffer.
// On '\n': parses command, dispatches callback.

typedef void (*cmd_callback_t)(const char *cmd_json);
void telemetry_register_cmd_cb(cmd_callback_t cb);
```

**Ring buffer (static, 256 bytes):**
```c
static uint8_t tx_ring[256];
static volatile uint16_t tx_head = 0;
static volatile uint16_t tx_tail = 0;
// head == tail → empty; (head+1)%256 == tail → full
```

**TX flow:**
1. `telemetry_send` → `snprintf` into 128-byte stack buffer → copy to ring.
2. `telemetry_poll` → if DMA idle and ring non-empty → start DMA TX for contiguous segment.
3. DMA TC callback → advance `tx_tail`, call `telemetry_poll` again.

**JSON serialiser (no heap, no `malloc`):**
```c
static int build_json(char *buf, size_t sz,
                      const SensorState *s, const AudioParams *a,
                      const StatusFlags *f) {
    return snprintf(buf, sz,
        "{\"t\":%lu,\"roll\":%.2f,\"pitch\":%.2f,"
        "\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
        "\"dist_mm\":%lu,\"temp_c\":%.2f,"
        "\"synth_hz\":%.1f,\"filt_hz\":%.1f,\"servo_deg\":%.2f,"
        "\"status\":{\"mpu\":%d,\"mcp\":%d,\"sr04\":%d,"
        "\"audio\":%d,\"servo\":%d,\"i2c_err\":%d}}\n",
        s->t_ms, s->roll_deg, s->pitch_deg,
        s->ax_g, s->ay_g, s->az_g,
        (uint32_t)s->dist_filt_mm, s->temp_c,
        a->synth_hz, a->filt_hz, a->servo_deg,
        f->mpu, f->mcp, f->sr04, f->audio, f->servo, f->i2c_err);
}
```

**Command parser:**
```c
// Minimal JSON key scan — no heap, no cJSON
// Looks for "cmd" key, dispatches:
//   "selftest" → bist_run()
//   "set" + "filt_hz" → filter_set_cutoff(val)
//   "seq" → ignored (Track B extension)
```

---

### 2.7 `src/bist/bist.h` / `bist.c`

**Public API:**
```c
void bist_run(void);
// Runs full selftest sequence, emits NDJSON result lines via telemetry.
// Blocks for ~1 s (servo sweep + DAC burst). Called from main loop only.
```

**Sequence (see spec §1.7 for output format):**
1. I2C scan: probe 0x68, 0x18.
2. MPU WHO_AM_I read.
3. MCP9808 Mfr ID read.
4. HC-SR04 single ping (38 ms timeout).
5. DAC 440 Hz burst, 200 ms.
6. Servo sweep: 0° → 90° → 180° → 0° (HAL_Delay between steps — BIST only, not audio path).
7. LED blink × 3.
8. Summary line.

---

### 2.8 `main.c` — Super-Loop

```c
int main(void) {
    HAL_Init();
    SystemClock_Config();       // 170 MHz PLL from HSI16
    MX_GPIO_Init();             // PA4, PA5, PB4, PB5, PB6, PB8, PB9
    MX_I2C1_Init();             // 400 kHz
    MX_TIM3_Init();             // servo 50 Hz
    MX_TIM4_Init();             // echo capture 1 MHz
    MX_TIM6_Init();             // audio 32 kHz TRGO
    MX_DAC1_Init();             // PA4
    MX_DMA_Init();
    MX_LPUART1_UART_Init();     // 115200 8N1

    mpu6050_init(&hi2c1);
    mcp9808_init(&hi2c1);
    hcsr04_init(&htim4);
    audio_init(32000);
    telemetry_init(&hlpuart1);
    mapping_init();

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);   // servo

    uint32_t last_sensor_tick = 0;
    uint32_t last_telem_tick  = 0;
    uint32_t last_ping_tick   = 0;

    for (;;) {
        uint32_t now = HAL_GetTick();

        // HC-SR04 ping every 60 ms
        if (now - last_ping_tick >= 60) {
            hcsr04_trigger();
            last_ping_tick = now;
        }

        // Sensor read + mapping every 10 ms
        if (now - last_sensor_tick >= 10) {
            last_sensor_tick = now;

            MPU6050_Data mpu = {0};
            mpu6050_read(&hi2c1, &mpu);

            MCP9808_Data mcp = {0};
            mcp9808_read(&hi2c1, &mcp);

            SensorState ss = {
                .dist_filt_mm = hcsr04_get_distance_mm(),
                .roll_deg     = mpu.roll_deg,
                .pitch_deg    = mpu.pitch_deg,
                .temp_c       = mcp.temp_c,
            };

            AudioParams ap = {0};
            mapping_update(&ss, &ap);

            voice_set_freq(0, ap.filt_hz);
            voice_set_gain(0, ap.volume);
            filter_set_cutoff(ap.cutoff_hz);

            // Servo
            uint32_t ccr = 1000 + (uint32_t)(ap.servo_deg * (1000.0f / 180.0f));
            TIM3->CCR1 = (ccr < 1000) ? 1000 : (ccr > 2000) ? 2000 : ccr;
        }

        // Telemetry ~30 Hz
        if (now - last_telem_tick >= 33) {
            last_telem_tick = now;
            // build StatusFlags from module state
            telemetry_send(&ss, &ap, &flags);
        }

        telemetry_poll();   // drain TX ring buffer
    }
}
```

---

## 3. Interrupt & DMA Plan

### 3.1 NVIC Priority Table

| Priority | IRQ | Peripheral | Rationale |
|----------|-----|------------|-----------|
| **0** (highest) | DMA1_Channelx_IRQ | DAC1 audio DMA | Audio must never glitch |
| **1** | TIM4_IRQ | HC-SR04 echo capture | Timing-critical: 1 µs resolution |
| **2** | I2C1_EV_IRQ / I2C1_ER_IRQ | I2C1 | Sensor reads; can tolerate brief delay |
| **3** (lowest) | LPUART1_IRQ / DMA TX | LPUART1 telemetry | Non-critical; must not block audio |

> STM32G4 uses 4-bit priority (0–15). Use `HAL_NVIC_SetPriority(IRQn, prio, 0)`.

### 3.2 ISR Rules

- **No HAL_Delay** in any ISR.
- **No I2C calls** in audio DMA callbacks.
- **No `printf`/`snprintf`** in ISRs (use flag + main-loop dispatch).
- All shared state between ISR and main loop: declared `volatile`.
- 32-bit aligned reads/writes are atomic on Cortex-M4 (no critical section needed for single `uint32_t`).
- For multi-word state (e.g. `MPU6050_Data`): use a double-buffer or copy under `__disable_irq()`.

### 3.3 DMA Channel Allocation (STM32G474)

| DMA | Channel | Request | Peripheral | Direction |
|-----|---------|---------|------------|-----------|
| DMA1 | Ch1 | DAC1_CH1 | DAC1_OUT1 | Mem→Periph, circular |
| DMA1 | Ch2 | LPUART1_TX | LPUART1 | Mem→Periph, normal |
| DMA1 | Ch3 | LPUART1_RX | LPUART1 | Periph→Mem, circular (optional) |

> Verify DMA request mapping against STM32G474 reference manual Table 80 (DMA1 request map).
> DMAMUX1 is used on G4 — set `DMAMUX1_Channelx->CCR` request field accordingly.

---

## 4. Timer Allocation

| Timer | Function | Clock | PSC | ARR | Rate | Notes |
|-------|----------|-------|-----|-----|------|-------|
| TIM6 | Audio sample clock | APB1 = 170 MHz | 0 | 5311 | 32 kHz | TRGO → DAC1 trigger |
| TIM3 | Servo PWM | APB1 = 170 MHz | 169 | 19999 | 50 Hz | CH1 = PB4, AF2 |
| TIM4 | HC-SR04 echo capture | APB1 = 170 MHz | 169 | 0xFFFF | 1 MHz tick | CH1 = PB6, AF2 |

**TIM6 ARR calculation:**
```
ARR = (SYSCLK / (PSC+1) / Fs) - 1
    = (170,000,000 / 1 / 32,000) - 1
    = 5312 - 1 = 5311
```

**TIM3 ARR calculation:**
```
ARR = (170,000,000 / 170 / 50) - 1
    = (1,000,000 / 50) - 1
    = 20,000 - 1 = 19,999
```

---

## 5. Buffer Sizes & Memory Placement

### 5.1 SRAM Layout

| Buffer | Type | Size | Section | Alignment | Notes |
|--------|------|------|---------|-----------|-------|
| `wavetable[256]` | `uint16_t` | 512 B | `.dma_buf` (SRAM1) | 4-byte | CORDIC sine, 12-bit |
| `dac_buf[128]` | `uint16_t` | 256 B | `.dma_buf` (SRAM1) | 4-byte | Ping-pong DAC output |
| `fmac_in[64]` | `int16_t` | 128 B | `.dma_buf` (SRAM1) | 4-byte | FMAC input, q15 |
| `fmac_out[64]` | `int16_t` | 128 B | `.dma_buf` (SRAM1) | 4-byte | FMAC output, q15 |
| `fmac_coeffs[5]` | `int16_t` | 10 B | `.dma_buf` (SRAM1) | 4-byte | Biquad b0,b1,b2,-a1,-a2 |
| `tx_ring[256]` | `uint8_t` | 256 B | `.bss` (SRAM1) | 1-byte | LPUART TX ring |
| `rx_line[128]` | `uint8_t` | 128 B | `.bss` (SRAM1) | 1-byte | LPUART RX line buf |

**Total DMA-accessible buffers:** ~1.2 KB in SRAM1.
**Total SRAM budget:** 128 KB available; well within limits.

### 5.2 Linker Script Addition

Add to `STM32G474RETx_FLASH.ld`:
```ld
/* DMA-accessible buffers in SRAM1 */
.dma_buf (NOLOAD) :
{
    . = ALIGN(4);
    *(.dma_buf)
    . = ALIGN(4);
} >RAM
```

> `RAM` region in the default CubeMX linker script maps to SRAM1 starting at 0x20000000.
> DMA1 on STM32G474 can access all SRAM regions. CCM SRAM (0x10000000) is NOT DMA-accessible.

### 5.3 Stack & Heap

- Main stack: 2 KB (default CubeMX, sufficient for bare-metal super-loop).
- Heap: 0 (no dynamic allocation — all buffers are static).
- ISR stack: shared with main stack on Cortex-M4 (MSP).

---

## 6. Build Order

Each step is independently flashable and demoable. Commit after each green step.

| Step | Description | Demoable Output | Key Files |
|------|-------------|-----------------|-----------|
| 1 | **Blink** | LD2 (PA5) blinks 1 Hz | `main.c`, GPIO init |
| 2 | **Heartbeat** | LPUART1 emits `{"t":ms,"alive":1}` @ 1 Hz | `main.c`, LPUART init |
| 3 | **I2C scan** | Prints found devices at 0x18, 0x68 | `main.c` + I2C init |
| 4 | **MPU read** | Telemetry includes `roll`, `pitch`, `ax/ay/az` | `mpu6050.c` |
| 5 | **MCP read** | Telemetry includes `temp_c` | `mcp9808.c` |
| 6 | **HC-SR04** | Telemetry includes `dist_mm` | `hcsr04.c`, TIM4 |
| 7 | **Servo** | Servo sweeps with distance | `main.c`, TIM3 |
| 8 | **Basic tone** | Audible pitch changes with distance | `main.c`, TIM6 IRQ, DAC |
| 9 | **CORDIC wavetable** | Sine wavetable generated, verified in debugger | `audio_engine.c` |
| 10 | **DAC DMA** | Clean 440 Hz sine from STEMMA speaker | `audio_engine.c`, DMA |
| 11 | **FMAC filter** | Tone brightness sweeps with tilt | `audio_engine.c`, FMAC |
| 12 | **Full telemetry** | All fields present, ~30 Hz, non-blocking | `telemetry.c` |
| 13 | **BIST** | `selftest` command → PASS/FAIL per subsystem | `bist.c` |

---

## 7. Risks & Fallbacks

| Risk | Likelihood | Impact | Fallback |
|------|-----------|--------|---------|
| FMAC coefficient instability (overflow, NaN) | Medium | Audio glitch | Bypass FMAC; ship CORDIC sine unfiltered. Set `status.audio=1`. |
| DMA conflict (DAC + LPUART share DMA1) | Low | Audio dropout | Move LPUART TX to IRQ-driven (no DMA). Audio DMA stays. |
| TIM4 input capture polarity toggle race | Low | Wrong distance | Use two CC channels (CC1 rising, CC2 falling on same TI1) instead of polarity toggle. |
| I2C bus lockup (SDA stuck low) | Low | Sensor loss | HAL_I2C_DeInit + re-init sequence; hold last values. |
| HC-SR04 echo divider missing | High (wiring) | PB6 overvoltage | Check before power-on. 1kΩ + 2kΩ divider mandatory. |
| PA4/PA5 confusion (DAC vs LED) | Medium | No audio / LED damage | PA4 = DAC only. PA5 = LED only. Never swap. Verified in GPIO init. |
| CORDIC wavetable in CCM SRAM | Medium | DMA fault (bus error) | Ensure `.dma_buf` maps to SRAM1 (0x20000000), not CCM (0x10000000). |
| 170 MHz PLL not configured | High | Wrong timer rates | Verify `SystemClock_Config()` sets SYSCLK=170 MHz before any timer init. |

---

## 8. File-Level Dependency Graph

```
main.c
├── sensors/mpu6050.h      (I2C1, FPU atan2f)
├── sensors/mcp9808.h      (I2C1)
├── sensors/hcsr04.h       (TIM4, GPIO PB5/PB6)
├── mapping/mapping.h      (pure math — no HAL)
├── audio_engine/audio_engine.h  (TIM6, DAC1, DMA1, CORDIC, FMAC)
├── telemetry/telemetry.h  (LPUART1, DMA1)
└── bist/bist.h            (calls all of the above)

mapping/mapping.c          — no HAL includes (host-testable)
bist/bist.c                — calls mpu6050, mcp9808, hcsr04, audio_engine, telemetry
```

---

## 9. Coding Conventions

- All ISR-shared variables: `volatile`.
- No dynamic allocation (`malloc`, `new`): all buffers static.
- No `HAL_Delay` outside BIST.
- HAL return codes checked; errors logged to `status` flags.
- `CLAMP(x, lo, hi)` macro used throughout (defined in `mapping.h`).
- Fixed-point: Q16.16 for phase accumulator; q15 for FMAC coefficients.
- Float used freely (FPU enabled via `-mfloat-abi=hard -mfpu=fpv4-sp-d16`).
- Compiler flags: `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -ffunction-sections -fdata-sections`.
