/**
 * stm32g4xx_hal_conf.h
 * HAL module enable/disable for Air-Synth Phase 0 + M1/M2.
 * Only enable what we actually use to keep code size down.
 */
#ifndef STM32G4XX_HAL_CONF_H
#define STM32G4XX_HAL_CONF_H

/* ---- Module selection -------------------------------------------------- */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_DAC_MODULE_ENABLED
#define HAL_CORDIC_MODULE_ENABLED
#define HAL_FMAC_MODULE_ENABLED

/* ---- HSE / HSI / LSE / LSI oscillator values --------------------------- */
/* NUCLEO-G474RE has 24 MHz HSE (X3) but we use HSI16 for simplicity       */
#if !defined(HSE_VALUE)
  #define HSE_VALUE    24000000UL   /* Hz — on-board crystal */
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT  100U /* ms */
#endif
#if !defined(HSI_VALUE)
  #define HSI_VALUE    16000000UL   /* Hz — internal RC */
#endif
#if !defined(HSI48_VALUE)
  #define HSI48_VALUE  48000000UL
#endif
#if !defined(LSI_VALUE)
  #define LSI_VALUE    32000UL
#endif
#if !defined(LSE_VALUE)
  #define LSE_VALUE    32768UL
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT  5000U /* ms */
#endif
#if !defined(EXTERNAL_CLOCK_VALUE)
  #define EXTERNAL_CLOCK_VALUE  12288000UL
#endif

/* ---- SysTick / tick frequency ------------------------------------------ */
#define TICK_INT_PRIORITY  15U   /* lowest priority — audio DMA is higher */

/* ---- Ethernet (unused) -------------------------------------------------- */
#define ETH_TX_DESC_CNT  4U
#define ETH_RX_DESC_CNT  4U

/* ---- USB (unused) ------------------------------------------------------- */
#define USE_USB_FS

/* ---- Misc HAL options --------------------------------------------------- */
#define USE_RTOS                     0U
#define PREFETCH_ENABLE              1U
#define INSTRUCTION_CACHE_ENABLE     1U
#define DATA_CACHE_ENABLE            1U

/* ---- Include HAL driver headers ---------------------------------------- */
#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32g4xx_hal_rcc.h"
  #include "stm32g4xx_hal_rcc_ex.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32g4xx_hal_gpio.h"
  #include "stm32g4xx_hal_gpio_ex.h"
#endif
#ifdef HAL_EXTI_MODULE_ENABLED
  #include "stm32g4xx_hal_exti.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32g4xx_hal_dma.h"
  #include "stm32g4xx_hal_dma_ex.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32g4xx_hal_cortex.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32g4xx_hal_flash.h"
  #include "stm32g4xx_hal_flash_ex.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32g4xx_hal_pwr.h"
  #include "stm32g4xx_hal_pwr_ex.h"
#endif
#ifdef HAL_I2C_MODULE_ENABLED
  #include "stm32g4xx_hal_i2c.h"
  #include "stm32g4xx_hal_i2c_ex.h"
#endif
#ifdef HAL_TIM_MODULE_ENABLED
  #include "stm32g4xx_hal_tim.h"
  #include "stm32g4xx_hal_tim_ex.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32g4xx_hal_uart.h"
  #include "stm32g4xx_hal_uart_ex.h"
#endif
#ifdef HAL_DAC_MODULE_ENABLED
  #include "stm32g4xx_hal_dac.h"
  #include "stm32g4xx_hal_dac_ex.h"
#endif
#ifdef HAL_CORDIC_MODULE_ENABLED
  #include "stm32g4xx_hal_cordic.h"
#endif
#ifdef HAL_FMAC_MODULE_ENABLED
  #include "stm32g4xx_hal_fmac.h"
#endif

/* ---- Assertion ---------------------------------------------------------- */
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line);
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0U)
#endif

#endif /* STM32G4XX_HAL_CONF_H */
