/**
 * @file    stm32f4xx_it.c
 * @brief   Interrupt service routines for STM32F429.
 *
 * DESIGN PHILOSOPHY
 * =================
 * ISRs must be SHORT.  Every microsecond spent in an ISR is a microsecond that
 * other interrupts are delayed (or cannot fire at all if they are the same or
 * lower priority).  Therefore:
 *
 *   - The UART RX ISR only does TWO things: push the byte into the ring buffer,
 *     and re-arm the receive interrupt.  ZERO parsing, ZERO printf, ZERO
 *     string operations.
 *
 *   - The TIM2 ISR only increments a counter.
 *
 * All heavy lifting is deferred to the main loop.
 */

#include "stm32f4xx_hal.h"
#include "circular_buffer.h"

/* ---- External objects (defined in main.c) -------------------------------- */
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef  htim2;
extern RingBuffer_t       ring_buf;
extern uint8_t            rx_byte;

/* ---- Uptime counter (shared with uart_shell.c via extern) ---------------- */

/*
 * volatile because it is written in the TIM2 ISR and read in the main loop.
 * uint32_t gives us ~136 years of uptime before rollover, which is plenty.
 */
volatile uint32_t uptime_seconds = 0U;

/* ========================================================================== */
/*                    Cortex-M4 Processor Exception Handlers                  */
/* ========================================================================== */

/**
 * @brief  Non-Maskable Interrupt — triggered by hardware errors, watchdog, etc.
 *         In a production system you might log the fault and reset.
 */
void NMI_Handler(void)
{
    while (1) {
        /* Trap — should never happen in normal operation */
    }
}

/**
 * @brief  Hard Fault — the catch-all for unhandled faults (bus error, usage
 *         fault, etc.).  We loop forever so a debugger can inspect the state.
 */
void HardFault_Handler(void)
{
    while (1) {
        /* Trap — attach a debugger to inspect the call stack */
    }
}

/**
 * @brief  Memory Management Fault (MPU violation).
 */
void MemManage_Handler(void)
{
    while (1) { }
}

/**
 * @brief  Bus Fault — invalid memory access on the bus.
 */
void BusFault_Handler(void)
{
    while (1) { }
}

/**
 * @brief  Usage Fault — undefined instruction, unaligned access, etc.
 */
void UsageFault_Handler(void)
{
    while (1) { }
}

/**
 * @brief  Supervisor Call — used by RTOSes.  We don't use an RTOS here.
 */
void SVC_Handler(void)
{
}

/**
 * @brief  Debug Monitor — used by debug tools.
 */
void DebugMon_Handler(void)
{
}

/**
 * @brief  PendSV — used by RTOSes for context switching.  Unused in bare-metal.
 */
void PendSV_Handler(void)
{
}

/**
 * @brief  SysTick — HAL needs this to maintain its 1 ms timebase (HAL_Delay,
 *         HAL_GetTick, timeout handling in HAL_UART_Transmit, etc.).
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* ========================================================================== */
/*                    STM32F4xx Peripheral Interrupt Handlers                  */
/* ========================================================================== */

/**
 * @brief  USART1 global interrupt handler.
 *
 * We call HAL_UART_IRQHandler() which handles the low-level register dance
 * (clears flags, reads the data register, etc.) and then calls our callback
 * HAL_UART_RxCpltCallback() when a byte has been received.
 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

/**
 * @brief  TIM2 global interrupt handler.
 *
 * HAL_TIM_IRQHandler() clears the update interrupt flag and calls
 * HAL_TIM_PeriodElapsedCallback() which we override below.
 */
void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2);
}

/* ========================================================================== */
/*                         HAL Callbacks                                      */
/* ========================================================================== */

/**
 * @brief  UART receive-complete callback (called from HAL_UART_IRQHandler).
 *
 * This fires every time ONE byte has been received (because we armed with
 * HAL_UART_Receive_IT(&huart1, &rx_byte, 1)).
 *
 * We do exactly TWO things:
 *   1. Push the received byte into the ring buffer.
 *   2. Re-arm the interrupt for the next byte.
 *
 * WHY re-arm here?
 * HAL_UART_Receive_IT() is a one-shot: after receiving the requested number
 * of bytes (1 in our case), the UART RX interrupt is disabled.  If we don't
 * re-arm, we will never receive another byte.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        ring_push(&ring_buf, rx_byte);
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1U);
    }
}

/**
 * @brief  Timer period elapsed callback — fires once per second from TIM2.
 *
 * We only increment the uptime counter.  That's it.  If you need
 * sub-second resolution, read SysTick or use a higher-frequency timer.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        uptime_seconds++;
    }
}
