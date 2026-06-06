/**
 * system_stm32g4xx.c
 * CMSIS SystemInit + SystemClock_Config for STM32G474 @ 170 MHz
 *
 * Clock tree (HSI16 source):
 *   HSI16 (16 MHz) → PLL → SYSCLK 170 MHz
 *   PLLM = 4  → VCO input  = 16/4  = 4 MHz
 *   PLLN = 85 → VCO output = 4×85  = 340 MHz
 *   PLLR = 2  → SYSCLK     = 340/2 = 170 MHz
 *
 * Voltage scaling:
 *   PWR_REGULATOR_VOLTAGE_SCALE1_BOOST (sets PWR_CR5 bit 8 R1MODE)
 *   Required for SYSCLK > 150 MHz on STM32G4.
 *
 * Flash latency:
 *   FLASH_LATENCY_4 (4 wait states) required at 170 MHz / VOS1 boost.
 *
 * AHB/APB prescalers: all ÷1 → all buses at 170 MHz.
 */

#include "stm32g4xx_hal.h"

/* CMSIS required global */
uint32_t SystemCoreClock = 16000000UL; /* updated by SystemCoreClockUpdate() */

/* CMSIS AHB/APB clock table (not used by HAL but required by CMSIS) */
const uint8_t AHBPrescTable[16] = {0U,0U,0U,0U,0U,0U,0U,0U,1U,2U,3U,4U,6U,7U,8U,9U};
const uint8_t APBPrescTable[8]  = {0U,0U,0U,0U,1U,2U,3U,4U};

/**
 * SystemInit — called from startup before main().
 * Resets RCC to a known state; HAL_Init / SystemClock_Config do the rest.
 */
void SystemInit(void)
{
    /* FPU: enable CP10/CP11 full access (Cortex-M4F) */
    SCB->CPACR |= ((3UL << 20U) | (3UL << 22U));

    /* Reset RCC clock configuration to reset state */
    /* Set HSION bit */
    RCC->CR |= RCC_CR_HSION;

    /* Reset CFGR register */
    RCC->CFGR = 0x00000001U;

    /* Reset HSEON, CSSON, PLLON, HSEPRE bits */
    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON | RCC_CR_PLLON | RCC_CR_HSEBYP);

    /* Reset PLLCFGR register */
    RCC->PLLCFGR = 0x00001000U;

    /* Disable all interrupts */
    RCC->CIER = 0x00000000U;

    /* Configure the Vector Table location — default: Flash @ 0x08000000 */
#if defined(VECT_TAB_SRAM)
    SCB->VTOR = SRAM_BASE | VECT_TAB_OFFSET;
#else
    SCB->VTOR = FLASH_BASE | 0x00000000U;
#endif
}

/**
 * SystemClock_Config — call from main() after HAL_Init().
 *
 * Sequence (matches CubeMX NUCLEO-G474RE 170 MHz example):
 *  1. Enable PWR clock, set voltage scale 1 boost.
 *  2. Configure HSI16 + PLL (PLLM=4, PLLN=85, PLLR=2).
 *  3. Set Flash latency to 4 WS before switching SYSCLK.
 *  4. Select PLL as SYSCLK; set AHB/APB prescalers to 1.
 *  5. Update SystemCoreClock.
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef       RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef       RCC_ClkInitStruct = {0};

    /* Step 1: Voltage scaling — must be done before raising frequency */
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    /* Step 2: HSI16 oscillator + PLL configuration */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    /* VCO input  = HSI16 / PLLM = 16 / 4 = 4 MHz  (must be 2.66–16 MHz) */
    RCC_OscInitStruct.PLL.PLLM            = RCC_PLLM_DIV4;
    /* VCO output = 4 × PLLN = 4 × 85 = 340 MHz    (must be 64–344 MHz)  */
    RCC_OscInitStruct.PLL.PLLN            = 85;
    /* PLLP: unused (SAI clock) */
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
    /* PLLQ: unused (USB/FDCAN/QUADSPI) */
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
    /* PLLR: SYSCLK = 340 / 2 = 170 MHz */
    RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /* Step 3 + 4: Bus clocks + Flash latency
     * FLASH_LATENCY_4 = 4 wait states required for 170 MHz @ VOS1 boost.
     * AHB/APB1/APB2 all ÷1 → 170 MHz on every bus.
     */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK
                                     | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1
                                     | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }
}
