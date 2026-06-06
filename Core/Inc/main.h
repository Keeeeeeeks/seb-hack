/**
 * main.h — Air-Synth Phase 0 / M1 / M2 top-level header
 *
 * Pin map (authoritative — matches SEB.MD):
 *   LPUART1 TX = PA2  AF12
 *   LPUART1 RX = PA3  AF12
 *   DAC1_OUT1  = PA4  (analog, no AF)
 *   LD2 LED    = PA5  GPIO out
 *   I2C1 SCL   = PB8  AF4
 *   I2C1 SDA   = PB9  AF4
 *   TIM3_CH1   = PB4  AF2  (servo 50 Hz)
 *   HC-SR04 TRIG = PB5 GPIO out
 *   HC-SR04 ECHO = PB6 TIM4_CH1 AF2 input-capture
 */
#ifndef MAIN_H
#define MAIN_H

#include "stm32g4xx_hal.h"

/* ---- Peripheral handles (defined in main.c, extern here) --------------- */
extern UART_HandleTypeDef  hlpuart1;
extern TIM_HandleTypeDef   htim3;
extern TIM_HandleTypeDef   htim4;
extern I2C_HandleTypeDef   hi2c1;
extern DAC_HandleTypeDef   hdac1;
extern DMA_HandleTypeDef   hdma_dac1_ch1;
extern TIM_HandleTypeDef   htim6;

/* ---- Pin / port aliases ------------------------------------------------ */
#define LED_PIN         GPIO_PIN_5
#define LED_PORT        GPIOA

#define LPUART_TX_PIN   GPIO_PIN_2
#define LPUART_RX_PIN   GPIO_PIN_3
#define LPUART_PORT     GPIOA
#define LPUART_AF       GPIO_AF12_LPUART1   /* PA2/PA3 → AF12 on STM32G4 */

#define DAC_OUT_PIN     GPIO_PIN_4
#define DAC_OUT_PORT    GPIOA

#define I2C_SCL_PIN     GPIO_PIN_8
#define I2C_SDA_PIN     GPIO_PIN_9
#define I2C_PORT        GPIOB
#define I2C_AF          GPIO_AF4_I2C1

#define SERVO_PIN       GPIO_PIN_4
#define SERVO_PORT      GPIOB
#define SERVO_AF        GPIO_AF2_TIM3

#define TRIG_PIN        GPIO_PIN_5
#define TRIG_PORT       GPIOB

#define ECHO_PIN        GPIO_PIN_6
#define ECHO_PORT       GPIOB
#define ECHO_AF         GPIO_AF2_TIM4

/* ---- Telemetry ring buffer size (power of 2) --------------------------- */
#define TELEM_BUF_SIZE  512U

/* ---- Error handler ----------------------------------------------------- */
void Error_Handler(void);

#endif /* MAIN_H */
