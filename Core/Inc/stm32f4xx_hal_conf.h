/**
 * @file    stm32f4xx_hal_conf.h
 * @brief   HAL configuration file for STM32F429I-DISC1 project.
 *
 * This file selects which HAL modules are compiled.  Disabling unused modules
 * reduces compile time and binary size.  We enable only what we actually use:
 *   - GPIO, UART, TIM, ADC, DMA, PWR, RCC, CORTEX, FLASH
 */

#ifndef STM32F4XX_HAL_CONF_H
#define STM32F4XX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================= Module Selection =============================== */

#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
/* #define HAL_CAN_MODULE_ENABLED */
/* #define HAL_CRC_MODULE_ENABLED */
/* #define HAL_DAC_MODULE_ENABLED */
#define HAL_DMA_MODULE_ENABLED
/* #define HAL_ETH_MODULE_ENABLED */
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
/* #define HAL_I2C_MODULE_ENABLED */
/* #define HAL_I2S_MODULE_ENABLED */
/* #define HAL_IWDG_MODULE_ENABLED */
/* #define HAL_LTDC_MODULE_ENABLED */
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
/* #define HAL_RNG_MODULE_ENABLED */
/* #define HAL_RTC_MODULE_ENABLED */
/* #define HAL_SAI_MODULE_ENABLED */
/* #define HAL_SD_MODULE_ENABLED */
/* #define HAL_SPI_MODULE_ENABLED */
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
/* #define HAL_USART_MODULE_ENABLED */
/* #define HAL_WWDG_MODULE_ENABLED */
#define HAL_CORTEX_MODULE_ENABLED

/* ========================= Oscillator Values ============================== */

/**
 * HSE = 8 MHz — provided by the ST-LINK's MCO output on the Discovery board.
 * This is NOT an external crystal; it's a clock signal from the ST-LINK MCU.
 */
#if !defined(HSE_VALUE)
  #define HSE_VALUE    ((uint32_t)8000000U)
#endif

#if !defined(HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT  ((uint32_t)100U)   /* ms */
#endif

/**
 * HSI = 16 MHz — internal RC oscillator (factory trimmed ±1%).
 * Used as a fallback if HSE fails.
 */
#if !defined(HSI_VALUE)
  #define HSI_VALUE    ((uint32_t)16000000U)
#endif

/**
 * LSI = ~32 kHz — internal low-speed RC (for IWDG and RTC if no LSE).
 */
#if !defined(LSI_VALUE)
  #define LSI_VALUE    ((uint32_t)32000U)
#endif

/**
 * LSE = 32.768 kHz — external low-speed crystal (for RTC).
 * Present on the Discovery board but unused in this project.
 */
#if !defined(LSE_VALUE)
  #define LSE_VALUE    ((uint32_t)32768U)
#endif

#if !defined(LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT  ((uint32_t)5000U)   /* ms */
#endif

/**
 * External I2S clock source (not used).
 */
#if !defined(EXTERNAL_CLOCK_VALUE)
  #define EXTERNAL_CLOCK_VALUE  ((uint32_t)12288000U)
#endif

/* ========================= System Configuration =========================== */

/**
 * VDD supply voltage in mV.  Used by the Flash latency calculation.
 * The Discovery board runs at 3.3V (= 3300 mV).
 */
#define VDD_VALUE                    ((uint32_t)3300U)

/**
 * SysTick and interrupt priorities.
 * Tick interrupt priority = 0 (highest) to ensure HAL_Delay works reliably.
 */
#define TICK_INT_PRIORITY            ((uint32_t)0U)

#define USE_RTOS                     0U
#define PREFETCH_ENABLE              1U
#define INSTRUCTION_CACHE_ENABLE     1U
#define DATA_CACHE_ENABLE            1U

/* ========================= Assert Configuration =========================== */

/*
 * Uncomment the line below to enable HAL assert checks (useful for debugging).
 * In production, leave it commented out to avoid the overhead.
 */
/* #define USE_FULL_ASSERT    1U */

/* ========================= Include HAL Module Headers ===================== */

#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32f4xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32f4xx_hal_gpio.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32f4xx_hal_dma.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32f4xx_hal_cortex.h"
#endif

#ifdef HAL_ADC_MODULE_ENABLED
  #include "stm32f4xx_hal_adc.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32f4xx_hal_flash.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32f4xx_hal_pwr.h"
#endif

#ifdef HAL_TIM_MODULE_ENABLED
  #include "stm32f4xx_hal_tim.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32f4xx_hal_uart.h"
#endif

/* ========================= Assert Macro =================================== */

#ifdef USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t *file, uint32_t line);
#else
  #define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32F4XX_HAL_CONF_H */
