/**
 * @file    stm32f4xx_it.h
 * @brief   Interrupt handler prototypes.
 *
 * This header declares the ISR functions so that the startup code (which is
 * typically in assembly and references these symbols) can find them.
 */

#ifndef STM32F4XX_IT_H
#define STM32F4XX_IT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Cortex-M4 Processor Exception Handlers */
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

/* STM32F4xx Peripheral Interrupt Handlers */
void USART1_IRQHandler(void);
void TIM2_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32F4XX_IT_H */
