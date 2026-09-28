# STM32F429I-DISC1 UART Command Shell

A bare-metal interactive command shell for the **STM32F429I Discovery** board, communicating over **USART1** at 115200 baud (8N1).

## Features

| Command    | Description                                    |
|------------|------------------------------------------------|
| `led on`   | Turn the green LED (PG13) ON                   |
| `led off`  | Turn the green LED (PG13) OFF                  |
| `status`   | Show uptime, LED state, and CPU temperature    |
| `help`     | List all available commands                    |

- **Character echo** — typed characters are echoed back to the terminal
- **Backspace support** — handles both `0x08` (BS) and `0x7F` (DEL)
- **Case-insensitive** — `LED ON`, `Led On`, `led on` all work
- **Whitespace-tolerant** — leading/trailing spaces are trimmed

## Hardware

| Function           | Pin(s)   | Notes                              |
|--------------------|----------|------------------------------------|
| USART1 TX          | PA9      | Connect to USB-Serial adapter RX   |
| USART1 RX          | PA10     | Connect to USB-Serial adapter TX   |
| Green LED          | PG13     | Controlled by `led on`/`led off`   |
| Red LED            | PG14     | Lights up at startup (system alive)|
| Internal Temp      | ADC1 CH18| Read on `status` command           |

## Architecture

```
Terminal ──TX──▶ PA10 (RX) ──▶ USART1 ISR ──▶ Ring Buffer ──▶ Main Loop ──▶ Parser ──▶ Command Handler
                                                                                          │
Terminal ◀──RX── PA9  (TX) ◀── printf() ◀── _write() ◀── HAL_UART_Transmit() ◀───────────┘
```

### Key Design Decisions

1. **Lock-free SPSC ring buffer** — ISR pushes bytes, main loop pops. No mutexes needed (single producer, single consumer, volatile indices, Cortex-M4 atomic aligned access).

2. **Minimal ISR** — The UART RX callback does only two things: `ring_push()` and re-arm `HAL_UART_Receive_IT()`. Zero parsing in interrupt context.

3. **Function-pointer dispatch table** — Commands are defined as `{string, handler, help_text}` entries. Adding a command = adding one row. The `help` command auto-generates from this table.

4. **No dynamic allocation** — All buffers are static. `_sbrk` is provided only for newlib's internal printf formatting.

5. **Bare-metal super-loop** — No RTOS. The main loop just calls `shell_process()` which is non-blocking.

## Project Structure

```
Core/
├── Inc/
│   ├── circular_buffer.h      — Ring buffer interface
│   ├── uart_shell.h            — Shell interface + dispatch table types
│   ├── stm32f4xx_hal_conf.h    — HAL module selection
│   └── stm32f4xx_it.h          — ISR prototypes
└── Src/
    ├── main.c                  — Init + main loop
    ├── circular_buffer.c       — Ring buffer implementation
    ├── uart_shell.c            — Command parser + handlers
    ├── stm32f4xx_it.c          — ISR definitions (UART RX, TIM2)
    └── syscalls.c              — printf → UART redirect
```

## Clock Configuration

```
HSE (8 MHz, from ST-LINK MCO)
  └─▶ PLL (M=8, N=360, P=2, Q=7)
       └─▶ SYSCLK = 180 MHz
            ├─▶ AHB  = 180 MHz (÷1)
            ├─▶ APB1 = 45 MHz  (÷4) → TIM2 clock = 90 MHz (auto-doubled)
            └─▶ APB2 = 90 MHz  (÷2) → USART1 clock
```

## Building

### STM32CubeIDE
1. Create a new STM32 project targeting **STM32F429ZITx**
2. Replace the generated `Core/Src/` and `Core/Inc/` files with the ones in this repository
3. Ensure the HAL driver source files are included in the build
4. Build and flash

### Makefile / arm-none-eabi-gcc
You will need:
- `arm-none-eabi-gcc` toolchain
- STM32F4 HAL library source (from STM32CubeF4)
- CMSIS headers
- A linker script for STM32F429ZITx (e.g., `STM32F429ZITx_FLASH.ld`)
- The startup file (`startup_stm32f429xx.s`)

## Terminal Setup

Connect a USB-to-serial adapter to PA9 (TX) and PA10 (RX), then open a terminal:

```bash
# Linux / macOS
minicom -D /dev/ttyUSB0 -b 115200

# Windows (PuTTY)
# Serial, COMx, 115200, 8N1, no flow control
```

**Terminal settings:** Raw mode, no local echo (the firmware echoes characters).

## License

MIT
