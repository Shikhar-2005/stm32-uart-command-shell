/**
 * @file    syscalls.c
 * @brief   Minimal newlib system call stubs for bare-metal STM32.
 *
 * WHY THIS FILE?
 * ==============
 * The C standard library functions printf(), puts(), etc. ultimately call
 * _write() to output characters.  In a hosted environment (Linux, Windows)
 * _write() maps to a kernel system call.  On bare-metal there is no kernel,
 * so we must provide our own implementation that sends bytes somewhere useful
 * — in our case, out the UART.
 *
 * This file also stubs out other system calls (_read, _close, _lseek, etc.)
 * that newlib may reference.  Without these stubs the linker would complain
 * about undefined symbols.
 */

#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* UART handle used for printf output — defined in main.c */
extern UART_HandleTypeDef huart1;

/* ---- _write: redirect stdout/stderr to UART ----------------------------- */

/**
 * @brief  Write bytes to a file descriptor.
 *
 * @param  file  File descriptor (1 = stdout, 2 = stderr).
 * @param  ptr   Pointer to the data buffer.
 * @param  len   Number of bytes to write.
 * @return Number of bytes written, or -1 on error.
 *
 * WHY HAL_MAX_DELAY?
 * We use a blocking transmit with infinite timeout because printf is called
 * from the main loop (never from an ISR).  If the UART TX is busy, we wait.
 * This is acceptable for a debug/command shell.  In a production system you
 * might use DMA-based transmit with a software FIFO.
 */
int _write(int file, char *ptr, int len)
{
    (void)file;  /* We send everything to UART regardless of fd */

    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, HAL_MAX_DELAY);
    return len;
}

/* ---- Stubs for other system calls (required by newlib) ------------------- */

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;  /* No stdin support */
}

int _close(int file)
{
    (void)file;
    return -1;
}

int _lseek(int file, int offset, int whence)
{
    (void)file;
    (void)offset;
    (void)whence;
    return 0;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;  /* Character device */
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;  /* Yes, it's a terminal */
}

/**
 * @brief  Increase program data space (heap).
 *
 * We provide a minimal _sbrk so that newlib's internal allocator works
 * (printf uses it internally for formatting buffers).  This does NOT mean
 * we use malloc in our application code.
 */
extern uint32_t _end;   /* Defined by the linker script — end of BSS */
extern uint32_t _estack; /* Defined by the linker script — top of stack */

void *_sbrk(int incr)
{
    static uint8_t *heap_end = NULL;
    uint8_t *prev_heap_end;

    if (heap_end == NULL) {
        heap_end = (uint8_t *)&_end;
    }

    prev_heap_end = heap_end;

    /*
     * Check that we don't collide with the stack.
     * In a real system you might want a larger guard margin.
     */
    if ((heap_end + incr) > (uint8_t *)&_estack) {
        errno = ENOMEM;
        return (void *)-1;
    }

    heap_end += incr;
    return (void *)prev_heap_end;
}
