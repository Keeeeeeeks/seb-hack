/**
 * stm32g4xx_it.h — ISR prototypes
 */
#ifndef STM32G4XX_IT_H
#define STM32G4XX_IT_H

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

void TIM4_IRQHandler(void);
void TIM6_DAC_IRQHandler(void);
void DMA1_Channel1_IRQHandler(void);
void LPUART1_IRQHandler(void);

#endif /* STM32G4XX_IT_H */
