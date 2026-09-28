/**
 * @file    uart_shell.c
 * @brief   Command-line shell: line accumulation, parsing, and dispatch.
 *
 * ARCHITECTURE
 * ============
 * The shell is intentionally stateless between commands — it holds only a line
 * buffer that accumulates characters until a newline arrives.  This makes the
 * code simple, predictable, and easy to test.
 *
 * The dispatch table is a const array of {string, handler, help_text} structs.
 * When a complete line is received we do a linear search through the table.
 * For 4 commands this is negligible; even for 50+ commands it would still be
 * sub-microsecond on a 180 MHz Cortex-M4.
 */

#include "uart_shell.h"
#include "stm32f4xx_hal.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ---- External handles (defined in main.c, needed by command handlers) ---- */
extern UART_HandleTypeDef huart1;
extern ADC_HandleTypeDef  hadc1;
extern volatile uint32_t  uptime_seconds;

/* ---- Module-internal state ----------------------------------------------- */

/*
 * Line buffer — accumulates characters until '\r' or '\n'.
 * Not volatile because it is only ever touched from the main loop (shell_process).
 */
static char     line_buf[SHELL_LINE_MAX];
static uint16_t line_pos = 0U;

/*
 * LED state tracking — we keep a software shadow so "status" can report the
 * current state without reading the GPIO ODR register (which is valid but
 * less portable).
 */
static uint8_t led_green_state = 0U;

/* ---- Forward declarations of command handlers ---------------------------- */
static void cmd_led_on(void);
static void cmd_led_off(void);
static void cmd_status(void);
static void cmd_help(void);

/* ---- Dispatch table ------------------------------------------------------ */

/*
 * WHY a dispatch table instead of if/else?
 *   1. Adding a new command = adding one row.  No control-flow changes.
 *   2. The "help" handler iterates this same table, so help text is always
 *      in sync with the actual command set.
 *   3. It reads like documentation.
 */
static const ShellCommand_t cmd_table[] = {
    { "led on",   cmd_led_on,  "Turn the green LED (PG13) ON"               },
    { "led off",  cmd_led_off, "Turn the green LED (PG13) OFF"              },
    { "status",   cmd_status,  "Show uptime, LED state, and CPU temperature" },
    { "help",     cmd_help,    "Show this help message"                      },
};

/* Number of entries — computed at compile time so we never forget to update it */
#define CMD_TABLE_SIZE  (sizeof(cmd_table) / sizeof(cmd_table[0]))

/* ---- Helper: trim leading/trailing whitespace in-place ------------------- */

/**
 * @brief  Strip leading and trailing spaces/tabs from a string in-place.
 *
 * WHY trim?  The user might accidentally type "  led on  " with extra spaces.
 * Without trimming, the strcmp would fail and report "Unknown command", which
 * is a terrible user experience.
 */
static void str_trim(char *s)
{
    /* Advance past leading whitespace */
    char *start = s;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    /* If the entire string was whitespace, make it empty */
    if (*start == '\0') {
        s[0] = '\0';
        return;
    }

    /* Find the end and walk backwards over trailing whitespace */
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';

    /* Shift trimmed content to the beginning of the buffer if needed */
    if (start != s) {
        memmove(s, start, (size_t)(end - start + 2));  /* +2 for char + null */
    }
}

/* ---- Helper: convert string to lowercase in-place ----------------------- */

/**
 * @brief  Convert a string to lowercase in-place.
 *
 * WHY?  So that "LED ON", "Led On", and "led on" all match.  Case-insensitive
 * matching is standard UX for command shells.
 */
static void str_to_lower(char *s)
{
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

/* ---- Command dispatch ---------------------------------------------------- */

/**
 * @brief  Look up and execute a command from the dispatch table.
 * @param  line  The trimmed, lowercase command string.
 */
static void dispatch_command(const char *line)
{
    /* Skip empty lines (user just pressed Enter) */
    if (line[0] == '\0') {
        printf("> ");
        return;
    }

    /* Linear search through the command table */
    for (uint32_t i = 0U; i < CMD_TABLE_SIZE; i++) {
        if (strcmp(line, cmd_table[i].cmd_string) == 0) {
            cmd_table[i].handler();
            printf("> ");
            return;
        }
    }

    /* No match found */
    printf("Unknown command: '%s'. Type 'help'\r\n", line);
    printf("> ");
}

/* ---- Command handlers ---------------------------------------------------- */

/**
 * @brief  Turn the green LED ON.
 *
 * PG13 is active-high on the STM32F429I-DISC1 board: writing SET turns it on.
 */
static void cmd_led_on(void)
{
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_SET);
    led_green_state = 1U;
    printf("LED ON\r\n");
}

/**
 * @brief  Turn the green LED OFF.
 */
static void cmd_led_off(void)
{
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_RESET);
    led_green_state = 0U;
    printf("LED OFF\r\n");
}

/**
 * @brief  Print system status: uptime, LED state, and internal temperature.
 *
 * WHY read temperature here instead of continuously?
 * Polling the ADC only on demand saves power and avoids unnecessary bus traffic.
 * A single conversion takes ~15 µs, which is imperceptible to the user.
 */
static void cmd_status(void)
{
    /* ---- Uptime ---- */
    uint32_t secs = uptime_seconds;  /* snapshot volatile once */
    uint32_t hrs  = secs / 3600U;
    uint32_t mins = (secs % 3600U) / 60U;
    uint32_t s    = secs % 60U;
    printf("Uptime : %02lu:%02lu:%02lu (%lu s)\r\n", hrs, mins, s, secs);

    /* ---- LED state ---- */
    printf("LED PG13: %s\r\n", led_green_state ? "ON" : "OFF");

    /* ---- Internal temperature sensor ---- */
    /*
     * The STM32F429 internal temperature sensor is connected to ADC1 channel 18.
     * Conversion formula (from the datasheet, RM0090 §13.10):
     *   V_sense = raw * V_ref / 4096
     *   Temp(°C) = ((V_sense - V_25) / Avg_Slope) + 25
     * Where V_25 = 0.76 V and Avg_Slope = 2.5 mV/°C.
     */
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 100U) == HAL_OK) {
        uint32_t raw = HAL_ADC_GetValue(&hadc1);
        float voltage  = (float)raw * 3.3f / 4096.0f;
        float temp_c   = ((voltage - 0.76f) / 0.0025f) + 25.0f;
        printf("CPU Temp: %.1f C (raw ADC: %lu)\r\n", temp_c, raw);
    } else {
        printf("CPU Temp: ADC read failed\r\n");
    }
    HAL_ADC_Stop(&hadc1);
}

/**
 * @brief  Print all available commands by iterating the dispatch table.
 *
 * WHY iterate the table?  Because it guarantees the help output is always in
 * sync with the actual commands.  If someone adds a new command to the table,
 * it automatically appears in help.  No manual editing of a separate help
 * string is needed.
 */
static void cmd_help(void)
{
    printf("\r\n--- Available Commands ---\r\n");
    for (uint32_t i = 0U; i < CMD_TABLE_SIZE; i++) {
        printf("  %-10s : %s\r\n", cmd_table[i].cmd_string, cmd_table[i].help_text);
    }
    printf("--------------------------\r\n");
}

/* ---- Public API ---------------------------------------------------------- */

void shell_init(void)
{
    line_pos = 0U;
    memset(line_buf, 0, sizeof(line_buf));
}

/**
 * @brief  Drain the ring buffer and process characters one at a time.
 *
 * This function is called from the main super-loop.  It is non-blocking: if
 * the ring buffer is empty it returns immediately so the main loop can do
 * other work (e.g. low-power sleep in a more advanced design).
 *
 * Character handling:
 *   - Printable ASCII → echo back to terminal, append to line buffer.
 *   - Backspace (0x08 or 0x7F) → remove last char, send destructive backspace
 *     sequence "\b \b" (move cursor back, overwrite with space, move back again).
 *   - '\r' or '\n' → null-terminate line, dispatch command, reset buffer.
 *   - Everything else → ignored.
 */
void shell_process(RingBuffer_t *rb)
{
    uint8_t byte;

    while (ring_pop(rb, &byte)) {
        if (byte == '\r' || byte == '\n') {
            /*
             * End of line — time to parse.
             * Echo a newline so the terminal moves to the next line.
             */
            printf("\r\n");

            line_buf[line_pos] = '\0';

            /* Trim whitespace and normalise case before dispatch */
            str_trim(line_buf);
            str_to_lower(line_buf);

            dispatch_command(line_buf);

            /* Reset for the next command */
            line_pos = 0U;
            memset(line_buf, 0, sizeof(line_buf));

        } else if (byte == 0x08U || byte == 0x7FU) {
            /*
             * Backspace handling.
             * 0x08 = BS (some terminals), 0x7F = DEL (most modern terminals).
             *
             * We send the three-character sequence "\b \b":
             *   \b  → move cursor left one position
             *   ' ' → overwrite the character with a space
             *   \b  → move cursor left again
             * This gives the visual effect of deleting the character.
             */
            if (line_pos > 0U) {
                line_pos--;
                line_buf[line_pos] = '\0';
                /* Destructive backspace: erase the character on the terminal */
                printf("\b \b");
            }

        } else if (byte >= 0x20U && byte <= 0x7EU) {
            /*
             * Printable ASCII character.
             * Guard against buffer overflow — leave room for the null terminator.
             */
            if (line_pos < (SHELL_LINE_MAX - 1U)) {
                line_buf[line_pos] = (char)byte;
                line_pos++;

                /*
                 * Echo the character back to the terminal.
                 * WHY echo? Because we are in "raw" mode — the terminal does not
                 * automatically display what the user types.  Without echo, the
                 * user would type blind.
                 */
                char echo = (char)byte;
                HAL_UART_Transmit(&huart1, (uint8_t *)&echo, 1U, HAL_MAX_DELAY);
            }
        }
        /* else: ignore non-printable, non-backspace, non-newline bytes */
    }
}
