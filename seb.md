# Project Context — AIR-SYNTH (NY Tech Week "Hardware Hack")

Theme: **"The World Is Your Controller."** A gesture/distance-controlled musical instrument:
hand height (HC-SR04) + IMU tilt (MPU-6050) drive a real-time audio synth rendered on the
STM32G4's DSP silicon (CORDIC oscillator + FMAC filter + DAC/DMA), with servo + LED expression
and a Web Serial browser dashboard.

ALL firmware is developed via the Seb agent (hackathon rule). Use `/plan` first; stand up a
build+flash+serial test bench early; **build & verify on hardware after every change**.

## Target Hardware
- **Primary MCU/Processor**: STM32G474RET6 — Arm Cortex-M4F @ 170 MHz, FPU + DSP instructions, CORDIC + FMAC math accelerators
- **Secondary Processors**: On-board ST-LINK-V3E (debug + Virtual COM Port)
- **Dev Board / Custom PCB**: NUCLEO-G474RE (ST MB1367)
- **Board Revision**: MB1367

## Clock Configuration
- **HSE/HSI**: HSI16 (16 MHz internal RC). Optional: 8 MHz HSE from ST-LINK MCO.
- **PLL Config**: PLL → 170 MHz (set Flash latency + boost mode per CubeMX for 170 MHz)
- **System Clock Frequency**: 170 MHz SYSCLK; timers on 170 MHz buses

## Pin Mapping / GPIO Assignments  (authoritative — do not reassign without updating this block)
- Telemetry UART: **LPUART1 — PA2 (TX) / PA3 (RX)** @115200 8N1 → ST-LINK VCP (default route)
- Audio DAC out: **DAC1_OUT1 = PA4** → STEMMA speaker amp IN  (⚠ NEVER PA5 — that is LD2 LED / SPI1_SCK)
- I2C1: **SCL = PB8, SDA = PB9** → MPU-6050 (0x68) + MCP9808 (0x18), shared bus, 3V3
- Servo PWM: **TIM3_CH1 = PB4** (Arduino D5), 50 Hz, 1.0–2.0 ms pulse
- HC-SR04: **TRIG = PB5 (D4)** GPIO out; **ECHO = PB6 (D10) = TIM4_CH1** input capture (5V→3V3 divider on ECHO)
- Status LED: **PA5** (on-board LD2). Optional extra LEDs: PC7 (D9), PB3 (D3)
- User button: PC13 (optional)

## Memory Layout
- **Flash Size**: 512 KB @ 0x08000000
- **RAM Size**: 128 KB = 96 KB SRAM1/2 (@0x20000000) + 32 KB CCM SRAM
- **Linker Script Regions**: audio DMA wavetable + FMAC/CORDIC buffers in SRAM1; hot DSP state may use CCM

## Peripherals & Interfaces
- DAC1 (CH1/PA4) + DMA (circular) triggered by **TIM6 TRGO @ 32 kHz** → audio output
- **CORDIC**: SINE → oscillator wavetable; PHASE (atan2) → tilt roll/pitch from accel
- **FMAC**: IIR Direct Form 1 (q15) tone filter; cutoff swept by tilt
- TIM6: audio sample clock; TIM3_CH1: servo PWM; TIM4_CH1: HC-SR04 echo input capture (IRQ)
- I2C1 (Fast Mode 400 kHz): IMU + temp
- LPUART1: telemetry — **non-blocking TX (DMA/IRQ ring buffer), must NEVER stall audio**
- DMA: DAC (audio) [+ optional CORDIC/FMAC, LPUART TX]

## External Devices & Sensors
- MPU-6050 6-axis IMU @ I2C 0x68 — WHO_AM_I(0x75)=0x68; PWR_MGMT_1(0x6B) clear sleep; ACCEL_XOUT_H=0x3B
- MCP9808 temp @ I2C 0x18 — Tamb reg 0x05; Mfr ID(0x06)=0x0054; Device ID(0x07)=0x0400
- HC-SR04 ultrasonic — 10 µs trigger, echo width 58 µs/cm, 2–400 cm, 5V (echo divided to 3V3)
- SG90 micro servo — 50 Hz PWM, ~180°, 5V power, common ground, 470 µF + 100 nF decoupling
- Adafruit STEMMA speaker (class-D amp) — analog/DAC audio in

## Communication Protocols
- **Protocol**: I2C1 (sensors), LPUART1 (host telemetry + commands)
- **Baud Rate / Mode**: I2C 400 kHz; LPUART 115200 8N1 (raise to 921600 for a smoother scope)

## Software Stack
- **Build System**: CMake + Ninja (or Make)
- **Compile Command**: `cmake -B build -G Ninja && cmake --build build`
- **HAL / Framework**: STM32CubeG4 HAL (+ LL for CORDIC/FMAC where clearer)
- **RTOS**: none — bare-metal super-loop + interrupts/DMA (call out if an RTOS is introduced)
- **Toolchain**: arm-none-eabi-gcc (GNU Arm Embedded)
- **Compiler Flags**: `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -ffunction-sections -fdata-sections`
- **External Libraries**: CMSIS, STM32G4 HAL/LL; CMSIS-DSP optional (prefer CORDIC/FMAC hardware)

## Development Environment
- **Host OS**: Darwin (macOS)
- **IDE**: Seb CLI (agent-driven)
- **Debug Interface (SWD/JTAG)**: SWD via on-board ST-LINK-V3E
- **Debug Probe (ST-Link/J-Link/OpenOCD)**: ST-LINK-V3E (STM32_Programmer_CLI / OpenOCD)

## Debug Configuration
<!-- Auto-learned during debug sessions. Edit directly to change debug settings. -->
- **GDB Server Command**: `openocd -f interface/stlink.cfg -f target/stm32g4x.cfg` (or ST-LINK GDB server)
- **GDB Executable**: arm-none-eabi-gdb
- **Firmware ELF Path**: build/air-synth.elf
- **GDB Server Port**: 3333

## Power Management
- **Battery / Power Source**: USB via ST-LINK (5V). If **LD4 (overcurrent >500 mA)** lights → use external 5V for servo + speaker.
- Servo + speaker on 5V rail, common ground, bulk + bypass caps.

## Boot Configuration
- **Bootloader**: built-in system bootloader (unused); app at 0x08000000
- **OTA Mechanism**: none

## Interrupt Configuration
- **NVIC Priorities** (lower number = higher): DAC/audio DMA = highest; TIM4 echo capture = high; I2C = mid; LPUART TX = lowest
- **Key ISRs**: DMA1 (DAC), TIM4_CC (echo), optional MPU INT (motion), LPUART TX DMA
- Rules: no HAL_Delay / heavy work in ISRs; shared state `volatile`

## Compliance & Standards
- Hackathon demo (no formal standard). Bonus points: file any tool bugs found.

## SDK / Library Paths
<!-- Reference examples to feed Seb as context -->
- STM32CubeG4 package (HAL + examples). Consult:
  - `NUCLEO-G474RE/Examples_LL/CORDIC/CORDIC_CosSin` and `.../CORDIC_Sin_DMA`
  - `VictorTagayun/NUCLEO-G474RE_RealTime_FIR_IIR_FMAC` (FMAC FIR/IIR + DAC)
  - `STM32G474E-EVAL/Demonstrations/Modules/math/app_math.c` (CORDIC + FMAC)

## Supporting Devices
- Breadboard, jumpers, resistors (incl. 1k/2k for HC-SR04 echo divider), LEDs.

---

# SHARED CONTRACT (binding for BOTH tracks — track-a.md and track-b.md)

## Audio-Engine API (frozen seam — Track A implements on-target; Track B builds on top + host sim)
```c
// audio_engine.h
#define MAX_VOICES 4
void  audio_init(uint32_t sample_rate_hz);
void  voice_set_freq(int v, float hz);   // A drives v=0; B uses 0..MAX_VOICES-1
void  voice_set_gain(int v, float g);    // 0..1
void  filter_set_cutoff(float hz);       // FMAC
void  filter_set_q(float q);
void  audio_set_master_gain(float g);
// rendering is timer/DMA driven internally (CORDIC sine -> wavetable -> DAC DMA); callers only set params
```

## Telemetry (newline-delimited JSON, ~30 Hz, non-blocking TX)
Base (Track A) + extensions (Track B, ignored by older FE):
```json
{"t":ms,"roll":f,"pitch":f,"ax":f,"ay":f,"az":f,"dist_mm":i,"temp_c":f,
 "synth_hz":f,"filt_hz":f,"servo_deg":f,
 "status":{"mpu":0,"mcp":0,"sr04":0,"audio":0,"servo":0,"i2c_err":0},
 "voices":[{"hz":0,"g":0}], "seq":{"bpm":0,"step":0,"len":0,"on":false}, "detune_c":0}
```
Commands (FE→board, line JSON): `{"cmd":"selftest"}` | `{"cmd":"set","filt_hz":1200}` | `{"cmd":"seq","bpm":120,"on":true}`

## Frontend (ONE app, zero backend)
Vite + React + uPlot, Web Serial API (Chrome/Edge), served on localhost.
- Track A owns `src/core/*`: Connect, NDJSON parser, StatusPanel, charts, SynthScope, command box.
- Track B owns `src/showpiece/*`: SequencerGrid, VoicesPanel, TempDetune, optional Three.js pose cube.
- App renders showpiece panels only when their telemetry fields appear.
- NOTE: only ONE process can own the VCP — FE owns it at demo time; Seb's serial during dev.

## Git / Integration
- `main` = bring-up baseline. `track-a` = trunk (integration target). `track-b` = showpiece (host-sim + FE panels).
- Integration at **T-45 min**: merge `track-b` → `track-a` ONLY if `track-a` is green. `track-a` stays always-demoable.

## Rubric hooks
- Log every Seb prompt + ticket (`docs/*-agentlog.md`). Prefer interrupts + DMA + CORDIC + FMAC.
- Be ready to explain agent decisions, architecture, and the "why" (judges reward this heavily).

## Project Notes
- Build order is layered & always-demoable: blink → telemetry → sensors → tone → CORDIC/DAC synth → FMAC filter → garnish.
- Graceful degradation: any missing sensor → status flag red, audio holds safe; headline-kit-only still demos (IMU → servo + LED).
