/**
 * stm32g4xx_it.c — Interrupt Service Routines
 *
 * Rules (from SEB.MD):
 *  - No HAL_Delay / heavy work in ISRs
 *  - Shared state must be volatile
 *  - Audio sample ISR = highest priority; LPUART TX = lowest
 */

#include "main.h"
#include "stm32g4xx_it.h"
#include "audio_engine.h"

/* ---- External handles (defined in main.c) ------------------------------ */
extern UART_HandleTypeDef  hlpuart1;
extern TIM_HandleTypeDef   htim4;
extern DMA_HandleTypeDef   hdma_dac1_ch1;
extern TIM_HandleTypeDef   htim6;

/* ======================================================================== */
/* Cortex-M4 core exception handlers                                        */
/* ======================================================================== */

void NMI_Handler(void)
{
    HAL_RCC_NMI_IRQHandler();
}

void HardFault_Handler(void)
{
    /* Spin — attach debugger to inspect stack frame */
    while (1) {}
}

void MemManage_Handler(void)
{
    while (1) {}
}

void BusFault_Handler(void)
{
    while (1) {}
}

void UsageFault_Handler(void)
{
    while (1) {}
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/**
 * SysTick_Handler — 1 ms tick for HAL timebase.
 * Priority: TICK_INT_PRIORITY (15, lowest) so audio is never blocked.
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* ======================================================================== */
/* Peripheral ISRs                                                           */
/* ======================================================================== */

/**
 * TIM4_IRQHandler — HC-SR04 echo input-capture on TIM4_CH1 (PB6).
 * Priority: high (see NVIC config in main.c).
 * The HAL callback (HAL_TIM_IC_CaptureCallback) is in main.c.
 */
void TIM4_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim4);
}

/**
 * TIM6_DAC_IRQHandler — audio sample clock.
 * Priority: highest; writes the next PA4 DAC sample directly.
 */
void TIM6_DAC_IRQHandler(void)
{
    (void)htim6;
    audio_tim6_irq();
}

/**
 * DMA1_Channel1_IRQHandler — reserved for M2 DAC1 CH1 DMA audio.
 * Priority: highest when DMA audio is enabled.
 */
void DMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_dac1_ch1);
}

/**
 * LPUART1_IRQHandler — non-blocking TX (IRQ-driven ring buffer).
 * Priority: lowest (must never stall audio).
 */
void LPUART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&hlpuart1);
}
