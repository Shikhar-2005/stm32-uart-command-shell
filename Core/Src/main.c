/**
 * @file    main.c
 * @brief   STM32F429I-DISC1 UART Command Shell — Initialisation and Main Loop.
 *
 * SYSTEM OVERVIEW
 * ===============
 * This is a bare-metal (no RTOS) interactive command shell that runs over
 * USART1 at 115200 baud.  The user connects a USB-to-serial adapter to
 * PA9 (TX) and PA10 (RX), opens a terminal emulator (PuTTY, minicom, etc.),
 * and types commands.
 *
 * Data flow:
 *   Terminal → UART RX → ISR → ring buffer → main loop → parser → action
 *   Action → printf → _write → UART TX → Terminal
 *
 * Hardware used:
 *   - USART1 (PA9/PA10) — command I/O
 *   - GPIOG pin 13      — green LED
 *   - GPIOG pin 14      — red LED (active at startup as a "heartbeat" indicator)
 *   - TIM2              — 1 Hz interrupt for uptime counter
 *   - ADC1 channel 18   — internal temperature sensor
 *
 * Clock configuration:
 *   - HSE = 8 MHz (from ST-LINK's MCO output on the Discovery board)
 *   - PLL → SYSCLK = 180 MHz, AHB = 180 MHz, APB1 = 45 MHz, APB2 = 90 MHz
 *   - This is the maximum frequency for the STM32F429ZIT6.
 */

#include "stm32f4xx_hal.h"
#include "circular_buffer.h"
#include "uart_shell.h"
#include <stdio.h>

/* ========================================================================== */
/*                          Global Objects                                    */
/* ========================================================================== */

/*
 * HAL peripheral handles.
 * These are global because HAL callbacks and ISRs need access to them,
 * and HAL's callback mechanism passes the handle as a parameter.
 */
UART_HandleTypeDef huart1;
TIM_HandleTypeDef  htim2;
ADC_HandleTypeDef  hadc1;

/*
 * Ring buffer for UART RX — shared between the ISR (producer) and
 * the main loop (consumer).  Declared here so both can extern it.
 */
RingBuffer_t ring_buf;

/*
 * Single-byte receive buffer.  HAL_UART_Receive_IT() needs a stable address
 * to store the received byte.  We keep it global so the ISR callback and
 * the re-arm call both reference the same location.
 *
 * WHY not local to the callback?
 * Because HAL_UART_Receive_IT() is asynchronous — it sets up DMA/interrupt
 * state that writes to this address LATER, after the function returns.
 * A local variable would be on the stack and go out of scope.
 */
uint8_t rx_byte;

/*
 * Uptime counter — incremented in TIM2 ISR, read by the "status" command.
 * Declared in stm32f4xx_it.c, externed here for shell access.
 */
extern volatile uint32_t uptime_seconds;

/* ========================================================================== */
/*                     Private Function Prototypes                            */
/* ========================================================================== */

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_ADC1_Init(void);
static void Error_Handler(void);

/* ========================================================================== */
/*                              MAIN                                          */
/* ========================================================================== */

int main(void)
{
    /*
     * HAL_Init() sets up the SysTick timer for 1 ms ticks (used by
     * HAL_Delay, HAL_GetTick, and all HAL timeout mechanisms).
     * It also sets the NVIC priority grouping to 4 bits preemption / 0 sub.
     */
    HAL_Init();

    /*
     * Configure the system clock to 180 MHz from the 8 MHz HSE.
     * This MUST be called before any peripheral init because the peripheral
     * clock dividers depend on the system clock configuration.
     */
    SystemClock_Config();

    /* Initialise all configured peripherals */
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_TIM2_Init();
    MX_ADC1_Init();

    /* Initialise the ring buffer (zero head and tail pointers) */
    ring_init(&ring_buf);

    /* Initialise the command shell (clear line buffer) */
    shell_init();

    /*
     * Start TIM2 with interrupt enabled.
     * From this point, TIM2 fires every 1 second and increments uptime_seconds.
     */
    HAL_TIM_Base_Start_IT(&htim2);

    /*
     * Arm the UART receive interrupt for the first byte.
     * After this call, every received byte will trigger USART1_IRQHandler →
     * HAL_UART_IRQHandler → HAL_UART_RxCpltCallback, which pushes the byte
     * into ring_buf and re-arms for the next byte.
     */
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1U);

    /* Light up the red LED (PG14) as a "system alive" indicator */
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);

    /* Print startup banner */
    printf("\r\n");
    printf("============================================\r\n");
    printf("  STM32F429I-DISC1 UART Command Shell\r\n");
    printf("  USART1 @ 115200 8N1  (PA9=TX, PA10=RX)\r\n");
    printf("  Type 'help' for available commands.\r\n");
    printf("============================================\r\n");
    printf("> ");

    /* ---- Super-loop ---- */
    /*
     * WHY a super-loop instead of an RTOS?
     * For a simple command shell with one input source (UART) and a handful of
     * commands, an RTOS adds unnecessary complexity, RAM usage (for task stacks),
     * and potential for priority-inversion bugs.  A polling loop is perfectly
     * adequate and much easier to reason about.
     */
    while (1) {
        shell_process(&ring_buf);

        /*
         * In a power-sensitive application you would enter WFI (Wait For
         * Interrupt) here to sleep until the next UART byte or timer tick:
         *   __WFI();
         * We omit it for simplicity — the loop just spins.
         */
    }
}

/* ========================================================================== */
/*                      System Clock Configuration                            */
/* ========================================================================== */

/**
 * @brief  Configure the system clock to 180 MHz using PLL with HSE.
 *
 * Clock tree (STM32F429I-DISC1):
 *   HSE (8 MHz from ST-LINK MCO)
 *     → PLL: M=8, N=360, P=2, Q=7
 *     → SYSCLK = 8 * 360 / (8 * 2) = 180 MHz
 *     → AHB prescaler  = /1  → HCLK  = 180 MHz
 *     → APB1 prescaler = /4  → PCLK1 = 45 MHz  (max for APB1)
 *     → APB2 prescaler = /2  → PCLK2 = 90 MHz  (max for APB2)
 *
 * WHY 180 MHz?
 * It's the maximum rated frequency for the STM32F429 and gives best performance.
 * For a simple shell it's overkill, but it matches the CubeMX default and means
 * all HAL timing assumptions (baud rate calculations, etc.) are correct.
 *
 * IMPORTANT: Over-drive mode is required for stable 180 MHz operation.
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Enable Power Control clock and set voltage regulator output scale 1
     * (required for 180 MHz operation) */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* Configure HSE and PLL */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 8U;
    RCC_OscInitStruct.PLL.PLLN       = 360U;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 7U;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /* Enable Over-drive mode for stable 180 MHz operation */
    if (HAL_PWREx_EnableOverDrive() != HAL_OK) {
        Error_Handler();
    }

    /* Configure bus clocks */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK  |
                                       RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1  |
                                       RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    /*
     * Flash latency = 5 wait states for 180 MHz @ 3.3V
     * (see RM0090 Table 10: Number of wait states according to CPU clock)
     */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

/* ========================================================================== */
/*                        GPIO Initialisation                                 */
/* ========================================================================== */

/**
 * @brief  Initialise GPIO pins for the onboard LEDs.
 *
 * STM32F429I-DISC1 LED connections:
 *   PG13 = Green LED (active high)
 *   PG14 = Red LED   (active high)
 *
 * We configure them as push-pull outputs with no pull-up/pull-down.
 * Initial state is OFF (RESET).
 *
 * WHY no external pull resistor?
 * The LEDs on the Discovery board already have series resistors on the PCB.
 */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable GPIOG clock — must be done before accessing any GPIOG register */
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* Start with both LEDs off */
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13 | GPIO_PIN_14, GPIO_PIN_RESET);

    /* Configure PG13 and PG14 as push-pull outputs, low speed, no pull */
    GPIO_InitStruct.Pin   = GPIO_PIN_13 | GPIO_PIN_14;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;  /* LEDs don't need high speed */
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
}

/* ========================================================================== */
/*                       USART1 Initialisation                                */
/* ========================================================================== */

/**
 * @brief  Initialise USART1 at 115200 baud, 8N1.
 *
 * Pin mapping:
 *   PA9  → USART1_TX (Alternate Function AF7)
 *   PA10 → USART1_RX (Alternate Function AF7)
 *
 * WHY USART1?
 * On the STM32F429I-DISC1, the ST-LINK virtual COM port is NOT connected to
 * any USART by default.  We use USART1 on PA9/PA10 which are accessible on
 * the board headers, and connect an external USB-to-serial adapter.
 *
 * WHY 115200 baud?
 * It's the de-facto standard for embedded debug consoles — fast enough to be
 * responsive, slow enough to work reliably over long wires or cheap adapters.
 */
static void MX_USART1_UART_Init(void)
{
    /* Enable peripheral clocks */
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Configure PA9 (TX) and PA10 (RX) as alternate function */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin       = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;      /* Pull-up on RX prevents floating */
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Configure USART1 peripheral */
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200U;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }

    /* Enable USART1 interrupt in NVIC */
    HAL_NVIC_SetPriority(USART1_IRQn, 1U, 0U);  /* High priority for RX */
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/* ========================================================================== */
/*                        TIM2 Initialisation                                 */
/* ========================================================================== */

/**
 * @brief  Initialise TIM2 as a basic 1 Hz timer for uptime counting.
 *
 * TIM2 is on APB1 (45 MHz bus clock).  However, because the APB1 prescaler
 * is /4 (not /1), the timer input clock is automatically doubled by the RCC:
 *   TIM2 input clock = PCLK1 * 2 = 45 * 2 = 90 MHz
 *
 * To get a 1 Hz update event:
 *   Prescaler = 9000 - 1  → 90 MHz / 9000 = 10 kHz timer tick
 *   Period    = 10000 - 1 → 10 kHz / 10000 = 1 Hz
 *
 * WHY TIM2?
 * TIM2 is a 32-bit general-purpose timer, which means its period register is
 * 32 bits wide.  This gives us maximum flexibility.  We don't need a 32-bit
 * counter here, but TIM2 is otherwise unused so we might as well use it.
 */
static void MX_TIM2_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 9000U - 1U;   /* 90 MHz / 9000 = 10 kHz */
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 10000U - 1U;   /* 10 kHz / 10000 = 1 Hz  */
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) {
        Error_Handler();
    }

    /* Enable TIM2 interrupt in NVIC */
    HAL_NVIC_SetPriority(TIM2_IRQn, 2U, 0U);  /* Lower priority than UART */
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}

/* ========================================================================== */
/*                        ADC1 Initialisation                                 */
/* ========================================================================== */

/**
 * @brief  Initialise ADC1 for reading the internal temperature sensor.
 *
 * The internal temperature sensor is connected to ADC1 channel 18 on
 * STM32F42x/F43x devices (it's channel 16 on F40x/F41x — a common gotcha).
 *
 * Configuration:
 *   - 12-bit resolution (4096 levels)
 *   - Single conversion mode (we trigger manually in the "status" command)
 *   - Sample time = 480 cycles (the temperature sensor needs at least
 *     10 µs sampling time per the datasheet; at 90 MHz ADC clock / 8,
 *     480 cycles = 480 / (90/8) = 42.7 µs — well above minimum)
 *
 * WHY not continuous mode?
 * We only read temperature when the user types "status".  Continuous mode
 * would waste power and bus bandwidth for no benefit.
 */
static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    __HAL_RCC_ADC1_CLK_ENABLE();

    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV8;  /* 90/8 = 11.25 MHz */
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;  /* Single shot */
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1U;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        Error_Handler();
    }

    /* Configure channel 18 = internal temperature sensor */
    sConfig.Channel      = ADC_CHANNEL_18;        /* Temp sensor on F429 */
    sConfig.Rank         = 1U;
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;  /* Long sample for accuracy */

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        Error_Handler();
    }

    /*
     * Enable the internal temperature sensor and VREFINT.
     * The TSVREFE bit in ADC_CCR must be set before reading the temp sensor.
     */
    ADC->CCR |= ADC_CCR_TSVREFE;
}

/* ========================================================================== */
/*                          Error Handler                                     */
/* ========================================================================== */

/**
 * @brief  Error handler — called when a HAL function returns an error.
 *
 * We turn on the red LED and loop forever.  In a debugger you can inspect the
 * call stack to find out which init function failed and why.
 *
 * In a production system you would log the error to flash and reset.
 */
static void Error_Handler(void)
{
    /* Turn on the red LED (PG14) to signal an error */
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = GPIO_PIN_14;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG, &gpio);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);

    __disable_irq();
    while (1) {
        /* Trap — attach a debugger */
    }
}
