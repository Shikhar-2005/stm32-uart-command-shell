/**
 * @file    uart_shell.h
 * @brief   UART command-line shell — parser, dispatcher, and command table.
 *
 * WHY a separate shell module?
 * ----------------------------
 * Separating the shell logic from main.c keeps each file focused on a single
 * responsibility:
 *   - main.c  → hardware initialisation and the super-loop
 *   - uart_shell → command accumulation, parsing, and dispatch
 *
 * This also makes the shell independently testable on a host PC (you can feed
 * it bytes from stdin instead of a real UART).
 *
 * WHY a function-pointer dispatch table?
 * --------------------------------------
 * A table of {string, handler} pairs is the classic embedded-C pattern for
 * command dispatching.  Compared to a long if/else-if chain it is:
 *   1. Easier to extend (just add a row to the table).
 *   2. Data-driven — the "help" command can iterate the same table to
 *      auto-generate its output, so help text never goes out of sync.
 *   3. Constant-time lookup could be added later with a hash, but for a
 *      handful of commands a linear scan is perfectly fine.
 */

#ifndef UART_SHELL_H
#define UART_SHELL_H

#include <stdint.h>
#include "circular_buffer.h"

/* Maximum length of a single command line (including null terminator).
 * 128 bytes is generous for our simple commands and avoids wasting RAM. */
#define SHELL_LINE_MAX  128U

/**
 * @brief  Command type enumeration.
 *
 * WHY an enum if we use a dispatch table?
 * We keep the enum for clarity in logging/debugging and so that future code
 * (e.g. a state machine) can switch on it if needed.
 */
typedef enum {
    CMD_LED_ON,
    CMD_LED_OFF,
    CMD_STATUS,
    CMD_HELP,
    CMD_UNKNOWN
} CommandType_t;

/**
 * @brief  Function pointer type for command handlers.
 *
 * Every command handler has the same signature: takes no arguments, returns
 * nothing.  Any data they need (UART handle, GPIO port) is accessed through
 * module-level externs — this keeps the dispatch table simple.
 */
typedef void (*CmdHandler_t)(void);

/**
 * @brief  One entry in the command dispatch table.
 */
typedef struct {
    const char   *cmd_string;   /* e.g. "led on"  */
    CmdHandler_t  handler;      /* function to call */
    const char   *help_text;    /* short description for the help command */
} ShellCommand_t;

/**
 * @brief  Initialise the shell (clear the line buffer, print banner).
 */
void shell_init(void);

/**
 * @brief  Process all available bytes from the ring buffer.
 *
 * Call this in the main super-loop.  It drains the ring buffer one byte at a
 * time, echoes each character, handles backspace, and dispatches a command
 * when a newline is received.
 *
 * @param  rb  Pointer to the ring buffer fed by the UART ISR.
 */
void shell_process(RingBuffer_t *rb);

#endif /* UART_SHELL_H */
