/**
 * main.c — Air-Synth Phase 0 / M1 / M2 firmware
 * Target: STM32G474RE @ 170 MHz (NUCLEO-G474RE)
 *
 * Phase 0 (this file):
 *   ✓ 170 MHz clock via HSI16 PLL
 *   ✓ LPUART1 @ 115200 baud (PA2 TX / PA3 RX, AF12)
 *   ✓ LD2 heartbeat blink (PA5, 500 ms)
 *   ✓ 10 Hz rich JSON telemetry frame over LPUART1
 *   ✓ HC-SR04 distance measurement (TIM4 input-capture, PB6)
 *   ✓ DAC1 CH1 (PA4) + TIM6 IRQ audio engine
 *   ✓ I2C1 (PB8/PB9) initialised for IMU (M2)
 *   ✓ TIM3 CH1 (PB4) 50 Hz PWM for servo (M2)
 *
 * NVIC priority scheme (lower number = higher priority):
 *   0  — TIM6_DAC      (audio sample clock)
 *   2  — TIM4          (HC-SR04 echo capture)
 *  15  — SysTick / LPUART1 (telemetry TX)
 */

#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "audio_engine.h"
#include "mapping.h"
#include "mcp9808.h"
#include "mpu6050.h"
#include "telemetry.h"

/* ======================================================================== */
/* Peripheral handles                                                        */
/* ======================================================================== */
UART_HandleTypeDef  hlpuart1;
TIM_HandleTypeDef   htim3;
TIM_HandleTypeDef   htim4;
TIM_HandleTypeDef   htim6;
I2C_HandleTypeDef   hi2c1;
DAC_HandleTypeDef   hdac1;
DMA_HandleTypeDef   hdma_dac1_ch1;

/* ======================================================================== */
/* HC-SR04 distance measurement state                                       */
/* All fields written from TIM4 ISR → volatile                              */
/* ======================================================================== */
#define HCSR04_TIMEOUT_US   25000U  /* 25 ms → ~4 m max range */
#define HCSR04_TRIG_US      10U     /* 10 µs trigger pulse */
#define SOUND_CM_PER_US_X2  58U     /* round-trip: distance_cm = tof_us / 58 */

typedef struct {
    volatile uint32_t  capture_start;   /* TIM4 count at rising edge  */
    volatile uint32_t  capture_end;     /* TIM4 count at falling edge */
    volatile bool      got_rising;
    volatile bool      measurement_ready;
    volatile uint32_t  distance_cm;     /* last valid measurement     */
} Hcsr04State;

static Hcsr04State g_sonar = {0};

/* ======================================================================== */
/* Live sensor/audio telemetry state                                         */
/* ======================================================================== */
#define FALLBACK_TEMP_C  25.0f
#define AUDIO_SAMPLE_RATE_HZ  44100U

static uint8_t     g_mpu_addr = MPU6050_ADDR_DEFAULT;
static bool        g_mpu_present = false;
static bool        g_mcp_present = false;
static StatusFlags g_status = {0};

/* ======================================================================== */
/* Telemetry ring buffer (ISR-safe, single-producer single-consumer)        */
/* ======================================================================== */
static uint8_t  g_telem_buf[TELEM_BUF_SIZE];
static volatile uint32_t g_telem_head = 0;  /* written by main */
static volatile uint32_t g_telem_tail = 0;  /* written by LPUART TX ISR */

/* ======================================================================== */
/* Forward declarations                                                      */
/* ======================================================================== */
void SystemClock_Config(void);   /* defined in system_stm32g4xx.c */
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM6_Init(void);
static void MX_I2C1_Init(void);
static void MX_DAC1_Init(void);

static void     Sonar_Trigger(void);
static uint32_t Sonar_GetDistance_cm(void);
static void     Sensors_Init(void);
static void     Build_Telemetry_Frame(uint32_t tick_ms, TelemetryFrame_t *frame);
static void     Telem_Send(const char *json, uint16_t len);
static void     Telem_Flush(void);

/* ======================================================================== */
/* main                                                                      */
/* ======================================================================== */
int main(void)
{
    /* HAL init — configures SysTick @ 1 ms, priority 15 */
    HAL_Init();

    /* 170 MHz PLL from HSI16 */
    SystemClock_Config();

    /* Peripheral init */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_LPUART1_UART_Init();
    MX_TIM3_Init();
    MX_TIM4_Init();
    MX_TIM6_Init();
    MX_I2C1_Init();
    MX_DAC1_Init();

    mapping_init();
    Sensors_Init();
    audio_init(AUDIO_SAMPLE_RATE_HZ);
    audio_set_master_gain(0.5f);
    voice_set_gain(0, 0.6f);

    /* Start TIM3 CH1 PWM (servo, 50 Hz, 1.5 ms neutral) */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);

    /* Start TIM4 input-capture (HC-SR04 echo) */
    HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_1);

    /* ------------------------------------------------------------------ */
    /* Main loop — 10 Hz telemetry + 100 ms sonar poll                    */
    /* ------------------------------------------------------------------ */
    uint32_t last_telem_ms = 0;
    uint32_t last_sonar_ms = 0;
    uint32_t tick_ms;
    char     json_buf[256];

    while (1) {
        tick_ms = HAL_GetTick();

        /* Sonar: trigger every 100 ms */
        if ((tick_ms - last_sonar_ms) >= 100U) {
            last_sonar_ms = tick_ms;
            Sonar_Trigger();
        }

        /* Telemetry: emit rich sensor/audio JSON every 100 ms */
        if ((tick_ms - last_telem_ms) >= 100U) {
            last_telem_ms = tick_ms;

            TelemetryFrame_t frame;
            Build_Telemetry_Frame(tick_ms, &frame);

            uint32_t servo_ccr = 1000U + (uint32_t)((frame.audio.servo_deg / 180.0f) * 1000.0f);
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, servo_ccr);
            voice_set_freq(0, frame.audio.filt_hz);
            filter_set_cutoff(frame.audio.cutoff_hz);

            int n = telemetry_serialize(json_buf, sizeof(json_buf), &frame);

            if (n > 0 && n < (int)sizeof(json_buf)) {
                Telem_Send(json_buf, (uint16_t)n);
            }

            /* Heartbeat LED toggle: proves firmware main loop is alive. */
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        }

        /* Drain telemetry ring buffer via LPUART1 (polling fallback) */
        Telem_Flush();
    }
}

/* SystemClock_Config is defined in system_stm32g4xx.c (non-static).
 * The forward declaration at the top of this file resolves to it. */

/* ======================================================================== */
/* GPIO init                                                                 */
/* ======================================================================== */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable all GPIO clocks we use */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* ---- PA5: LD2 LED output ------------------------------------------ */
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin   = LED_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);

    /* ---- PA2/PA3: LPUART1 TX/RX (AF12) -------------------------------- */
    GPIO_InitStruct.Pin       = LPUART_TX_PIN | LPUART_RX_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = LPUART_AF;
    HAL_GPIO_Init(LPUART_PORT, &GPIO_InitStruct);

    /* ---- PA4: DAC1_OUT1 (analog) --------------------------------------- */
    GPIO_InitStruct.Pin  = DAC_OUT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DAC_OUT_PORT, &GPIO_InitStruct);

    /* ---- PB8/PB9: I2C1 SCL/SDA (AF4, open-drain) --------------------- */
    GPIO_InitStruct.Pin       = I2C_SCL_PIN | I2C_SDA_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;   /* external 4.7 kΩ pull-ups */
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = I2C_AF;
    HAL_GPIO_Init(I2C_PORT, &GPIO_InitStruct);

    /* ---- PB4: TIM3_CH1 PWM (servo, AF2) ------------------------------- */
    GPIO_InitStruct.Pin       = SERVO_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = SERVO_AF;
    HAL_GPIO_Init(SERVO_PORT, &GPIO_InitStruct);

    /* ---- PB5: HC-SR04 TRIG (GPIO output) ------------------------------ */
    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin   = TRIG_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(TRIG_PORT, &GPIO_InitStruct);

    /* ---- PB6: TIM4_CH1 input-capture (HC-SR04 ECHO, AF2) ------------- */
    GPIO_InitStruct.Pin       = ECHO_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = ECHO_AF;
    HAL_GPIO_Init(ECHO_PORT, &GPIO_InitStruct);
}

/* ======================================================================== */
/* DMA init — reserved for M2 DAC DMA path                                  */
/* ======================================================================== */
static void MX_DMA_Init(void)
{
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* DMA1 Channel 1 → DAC1 CH1 (DMAMUX request 6), inactive in M1. */
    hdma_dac1_ch1.Instance                 = DMA1_Channel1;
    hdma_dac1_ch1.Init.Request             = DMA_REQUEST_DAC1_CHANNEL1;
    hdma_dac1_ch1.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_dac1_ch1.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_dac1_ch1.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_dac1_ch1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_dac1_ch1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_dac1_ch1.Init.Mode                = DMA_CIRCULAR;
    hdma_dac1_ch1.Init.Priority            = DMA_PRIORITY_HIGH;

    if (HAL_DMA_Init(&hdma_dac1_ch1) != HAL_OK) {
        Error_Handler();
    }

    /* Link DMA handle to DAC handle */
    __HAL_LINKDMA(&hdac1, DMA_Handle1, hdma_dac1_ch1);

    /* NVIC: DMA1 Channel 1 — reserved at high priority for M2 audio DMA. */
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

/* ======================================================================== */
/* LPUART1 @ 115200 baud, 8N1                                               */
/* PA2 TX / PA3 RX — AF12 (verified: STM32G474 DS Table 13)                */
/* LPUART clock source: PCLK1 = 170 MHz                                     */
/* BRR = 256 × PCLK1 / baud = 256 × 170000000 / 115200 ≈ 377778           */
/* ======================================================================== */
static void MX_LPUART1_UART_Init(void)
{
    __HAL_RCC_LPUART1_CLK_ENABLE();

    hlpuart1.Instance                    = LPUART1;
    hlpuart1.Init.BaudRate               = 115200;
    hlpuart1.Init.WordLength             = UART_WORDLENGTH_8B;
    hlpuart1.Init.StopBits               = UART_STOPBITS_1;
    hlpuart1.Init.Parity                 = UART_PARITY_NONE;
    hlpuart1.Init.Mode                   = UART_MODE_TX_RX;
    hlpuart1.Init.HwFlowCtl             = UART_HWCONTROL_NONE;
    hlpuart1.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    hlpuart1.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&hlpuart1) != HAL_OK) {
        Error_Handler();
    }

    /* FIFO mode: TX threshold = 1/8 full (reduces ISR rate) */
    if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_UARTEx_EnableFifoMode(&hlpuart1) != HAL_OK) {
        Error_Handler();
    }

    /* NVIC: LPUART1 — lowest priority (telemetry must not block audio) */
    HAL_NVIC_SetPriority(LPUART1_IRQn, 15, 0);
    HAL_NVIC_EnableIRQ(LPUART1_IRQn);
}

/* ======================================================================== */
/* TIM3 — 50 Hz PWM for servo (PB4, TIM3_CH1, AF2)                        */
/* ARR = (170 MHz / (PSC+1) / 50) - 1                                      */
/* PSC = 169 → TIM3 clock = 1 MHz → ARR = 19999 → period = 20 ms          */
/* CCR1 = 1500 → pulse = 1.5 ms (neutral)                                  */
/* ======================================================================== */
static void MX_TIM3_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 169;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 19999;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) {
        Error_Handler();
    }

    sConfigOC.OCMode       = TIM_OCMODE_PWM1;
    sConfigOC.Pulse        = 1500;   /* 1.5 ms neutral */
    sConfigOC.OCPolarity   = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode   = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) {
        Error_Handler();
    }
}

/* ======================================================================== */
/* TIM4 — input-capture for HC-SR04 echo (PB6, TIM4_CH1, AF2)             */
/* PSC = 169 → TIM4 clock = 1 MHz → 1 count = 1 µs                        */
/* ARR = 0xFFFF (65535 µs max capture window)                               */
/* Both edges captured via polarity toggle in ISR callback                  */
/* ======================================================================== */
static void MX_TIM4_Init(void)
{
    TIM_IC_InitTypeDef sConfigIC = {0};

    __HAL_RCC_TIM4_CLK_ENABLE();

    htim4.Instance               = TIM4;
    htim4.Init.Prescaler         = 169;
    htim4.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim4.Init.Period            = 0xFFFF;
    htim4.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_IC_Init(&htim4) != HAL_OK) {
        Error_Handler();
    }

    /* Capture on rising edge first; ISR toggles to falling */
    sConfigIC.ICPolarity  = TIM_INPUTCHANNELPOLARITY_RISING;
    sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
    sConfigIC.ICFilter    = 0x0F;   /* 8-sample digital filter */

    if (HAL_TIM_IC_ConfigChannel(&htim4, &sConfigIC, TIM_CHANNEL_1) != HAL_OK) {
        Error_Handler();
    }

    /* NVIC: TIM4 — high priority (time-critical echo capture) */
    HAL_NVIC_SetPriority(TIM4_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
}

/* ======================================================================== */
/* TIM6 — audio sample clock                                                */
/* audio_engine sets PSC/ARR dynamically so PA4 follows mapped filt_hz.     */
/* ======================================================================== */
static void MX_TIM6_Init(void)
{
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    __HAL_RCC_TIM6_CLK_ENABLE();

    htim6.Instance               = TIM6;
    htim6.Init.Prescaler         = 169;
    htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim6.Init.Period            = (1000000U / (440U * AUDIO_WAVETABLE_N)) - 1U;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(&htim6) != HAL_OK) {
        Error_Handler();
    }

    /* TIM6 interrupt drives DAC writes directly in audio_engine. */
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;

    if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK) {
        Error_Handler();
    }

    /* NVIC: TIM6 — highest priority (audio) */
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

/* ======================================================================== */
/* I2C1 — 400 kHz Fast Mode (PB8 SCL / PB9 SDA)                           */
/* TIMINGR computed for PCLK1 = 170 MHz, 400 kHz FM:                       */
/*   CubeMX value: 0x00F02B86                                               */
/* ======================================================================== */
static void MX_I2C1_Init(void)
{
    __HAL_RCC_I2C1_CLK_ENABLE();

    hi2c1.Instance              = I2C1;
    hi2c1.Init.Timing           = 0x00F02B86U;  /* 400 kHz @ 170 MHz PCLK1 */
    hi2c1.Init.OwnAddress1      = 0;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }

    /* Enable analog + digital noise filters */
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK) {
        Error_Handler();
    }
}

/* ======================================================================== */
/* DAC1 CH1 — 12-bit right-aligned, PA4 speaker amp input                  */
/* ======================================================================== */
static void MX_DAC1_Init(void)
{
    DAC_ChannelConfTypeDef sConfig = {0};

    __HAL_RCC_DAC1_CLK_ENABLE();

    hdac1.Instance = DAC1;

    if (HAL_DAC_Init(&hdac1) != HAL_OK) {
        Error_Handler();
    }

    sConfig.DAC_HighFrequency         = DAC_HIGH_FREQUENCY_INTERFACE_MODE_ABOVE_80MHZ;
    sConfig.DAC_DMADoubleDataMode     = DISABLE;
    sConfig.DAC_SignedFormat          = DISABLE;
    sConfig.DAC_SampleAndHold         = DAC_SAMPLEANDHOLD_DISABLE;
    sConfig.DAC_Trigger               = DAC_TRIGGER_NONE;
    sConfig.DAC_Trigger2              = DAC_TRIGGER_NONE;
    sConfig.DAC_OutputBuffer          = DAC_OUTPUTBUFFER_ENABLE;
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
    sConfig.DAC_UserTrimming          = DAC_TRIMMING_FACTORY;

    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK) {
        Error_Handler();
    }
}

/* ======================================================================== */
/* Sensor sampling + telemetry assembly                                      */
/* ======================================================================== */
static void Sensors_Init(void)
{
    if (mpu6050_init(&hi2c1, MPU6050_ADDR_DEFAULT) == MPU6050_OK) {
        g_mpu_addr = MPU6050_ADDR_DEFAULT;
        g_mpu_present = true;
        g_status.mpu = 0;
    } else if (mpu6050_init(&hi2c1, MPU6050_ADDR_ALT) == MPU6050_OK) {
        g_mpu_addr = MPU6050_ADDR_ALT;
        g_mpu_present = true;
        g_status.mpu = 0;
    } else {
        g_mpu_present = false;
        g_status.mpu = 1;
    }

    g_mcp_present = (mcp9808_init(&hi2c1, MCP9808_ADDR_DEFAULT) == MCP9808_OK);
    g_status.mcp = g_mcp_present ? 0 : 1;
    g_status.sr04 = 0;
    g_status.audio = 0;
    g_status.servo = 0;
    g_status.i2c_err = 0;
}

static void Build_Telemetry_Frame(uint32_t tick_ms, TelemetryFrame_t *frame)
{
    Mpu6050Data imu = {0};
    float temp_c = FALLBACK_TEMP_C;

    if (g_mpu_present && mpu6050_read(&hi2c1, g_mpu_addr, &imu) == MPU6050_OK) {
        g_status.mpu = 0;
    } else {
        g_status.mpu = 1;
        g_status.i2c_err++;
    }

    if (g_mcp_present && mcp9808_read_temp(&hi2c1, MCP9808_ADDR_DEFAULT, &temp_c) == MCP9808_OK) {
        g_status.mcp = 0;
    } else {
        g_status.mcp = 1;
        temp_c = FALLBACK_TEMP_C;
        g_status.i2c_err++;
    }

    uint32_t dist_cm = Sonar_GetDistance_cm();
    float dist_mm = (float)(dist_cm * 10U);
    if (dist_mm <= 0.0f) {
        dist_mm = MAP_DIST_MAX_MM;
        g_status.sr04 = 1;
    } else {
        g_status.sr04 = 0;
    }

    SensorState sensor = {
        .dist_filt_mm = dist_mm,
        .roll_deg = imu.roll_deg,
        .pitch_deg = imu.pitch_deg,
        .temp_c = temp_c,
    };

    AudioParams audio = {0};
    mapping_update(&sensor, &audio);

    frame->sensor.t_ms = tick_ms;
    frame->sensor.roll_deg = imu.roll_deg;
    frame->sensor.pitch_deg = imu.pitch_deg;
    frame->sensor.ax_g = imu.ax_g;
    frame->sensor.ay_g = imu.ay_g;
    frame->sensor.az_g = imu.az_g;
    frame->sensor.dist_filt_mm = dist_mm;
    frame->sensor.temp_c = temp_c;
    frame->audio = audio;
    frame->status = g_status;
}

/* ======================================================================== */
/* HC-SR04 driver                                                            */
/* ======================================================================== */

/**
 * Sonar_Trigger — send 10 µs TRIG pulse.
 * Called from main loop every 100 ms (safe inter-trigger interval).
 * Uses DWT cycle counter for µs delay (no HAL_Delay to avoid SysTick jitter).
 */
static void Sonar_Trigger(void)
{
    g_sonar.got_rising         = false;
    g_sonar.measurement_ready  = false;

    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_SET);

    /* 10 µs delay via DWT — DWT must be enabled (done in HAL_Init) */
    uint32_t start = DWT->CYCCNT;
    uint32_t delay_cycles = (SystemCoreClock / 1000000U) * HCSR04_TRIG_US;
    while ((DWT->CYCCNT - start) < delay_cycles) {}

    HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);
}

/**
 * Sonar_GetDistance_cm — return last valid measurement (thread-safe read).
 */
static uint32_t Sonar_GetDistance_cm(void)
{
    return g_sonar.distance_cm;
}

/**
 * HAL_TIM_IC_CaptureCallback — called from TIM4_IRQHandler.
 * Implements two-edge capture: rising → record start, falling → compute ToF.
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM4) return;

    if (!g_sonar.got_rising) {
        /* Rising edge: record start, reconfigure for falling */
        g_sonar.capture_start = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        g_sonar.got_rising    = true;

        __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1,
                                      TIM_INPUTCHANNELPOLARITY_FALLING);
    } else {
        /* Falling edge: compute time-of-flight */
        g_sonar.capture_end = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

        uint32_t tof_us;
        if (g_sonar.capture_end >= g_sonar.capture_start) {
            tof_us = g_sonar.capture_end - g_sonar.capture_start;
        } else {
            /* Timer wrapped (period = 65535 µs) */
            tof_us = (0xFFFFU - g_sonar.capture_start) + g_sonar.capture_end + 1U;
        }

        if (tof_us < HCSR04_TIMEOUT_US) {
            g_sonar.distance_cm = tof_us / SOUND_CM_PER_US_X2;
        }
        g_sonar.measurement_ready = true;
        g_sonar.got_rising        = false;

        /* Restore rising-edge polarity for next trigger */
        __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1,
                                      TIM_INPUTCHANNELPOLARITY_RISING);
    }
}

/* ======================================================================== */
/* Telemetry ring buffer helpers                                             */
/* ======================================================================== */

/**
 * Telem_Send — copy JSON string into ring buffer (called from main context).
 * Non-blocking: drops bytes if buffer is full (telemetry is best-effort).
 */
static void Telem_Send(const char *json, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uint32_t next_head = (g_telem_head + 1U) & (TELEM_BUF_SIZE - 1U);
        if (next_head == g_telem_tail) {
            break;  /* buffer full — drop remainder */
        }
        g_telem_buf[g_telem_head] = (uint8_t)json[i];
        g_telem_head = next_head;
    }
}

/**
 * Telem_Flush — drain ring buffer to LPUART1 (polling, called from main loop).
 * Sends up to 64 bytes per call to avoid monopolising the loop.
 */
static void Telem_Flush(void)
{
    uint8_t  chunk[64];
    uint16_t count = 0;

    while (g_telem_tail != g_telem_head && count < sizeof(chunk)) {
        chunk[count++] = g_telem_buf[g_telem_tail];
        g_telem_tail   = (g_telem_tail + 1U) & (TELEM_BUF_SIZE - 1U);
    }

    if (count > 0) {
        /* Blocking transmit with 100 ms timeout — acceptable at 115200 baud */
        HAL_UART_Transmit(&hlpuart1, chunk, count, 100U);
    }
}

/* ======================================================================== */
/* Error handler                                                             */
/* ======================================================================== */
void Error_Handler(void)
{
    __disable_irq();
    /* Rapid blink LD2 to signal fault */
    while (1) {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        /* Spin-delay ~100 ms at 170 MHz (rough) */
        for (volatile uint32_t i = 0; i < 1700000U; i++) {}
    }
}

/* ======================================================================== */
/* HAL MSP callbacks (called by HAL_xxx_Init)                               */
/* ======================================================================== */

/**
 * HAL_MspInit — called by HAL_Init().
 * Enable DWT cycle counter for µs-accurate TRIG pulse.
 */
void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    /* Enable DWT cycle counter (used by Sonar_Trigger) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT       = 0;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
}
