/**
 * @file    circular_buffer.h
 * @brief   Lock-free circular (ring) buffer for ISR-to-main data transfer.
 *
 * WHY a ring buffer?
 * ------------------
 * The UART receive interrupt fires for every single byte.  We need to decouple
 * the ISR (which must return as fast as possible) from the main-loop parser
 * (which may take an arbitrary amount of time to process a full command).
 * A circular buffer gives us O(1) push/pop with no dynamic allocation and no
 * need for mutexes — as long as there is exactly ONE producer (ISR) and ONE
 * consumer (main loop), and the head/tail indices are volatile.
 *
 * WHY power-of-2 size?
 * --------------------
 * Using a buffer size that is a power of 2 (256) lets us replace the expensive
 * modulo operator (%) with a fast bitmask (& 0xFF).  This matters inside the
 * ISR where every cycle counts.
 */

#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

/* Buffer size MUST be a power of 2 so we can use bitmask for wrap-around */
#define RING_BUFFER_SIZE  256U
#define RING_BUFFER_MASK  (RING_BUFFER_SIZE - 1U)

/**
 * @brief  Ring buffer structure.
 *
 * head  — index where the NEXT byte will be WRITTEN  (ISR increments this)
 * tail  — index where the NEXT byte will be READ     (main loop increments this)
 *
 * Both are volatile because they are shared between ISR context and thread
 * (main-loop) context.  Without volatile the compiler may cache them in a
 * register and never re-read from memory, causing the main loop to spin
 * forever even though the ISR has pushed new data.
 */
typedef struct {
    uint8_t           buffer[RING_BUFFER_SIZE];
    volatile uint16_t head;   /* write pointer (ISR side)       */
    volatile uint16_t tail;   /* read  pointer (main-loop side) */
} RingBuffer_t;

/**
 * @brief  Initialise the ring buffer (zero head and tail).
 * @param  rb  Pointer to the ring buffer instance.
 */
void ring_init(RingBuffer_t *rb);

/**
 * @brief  Push one byte into the ring buffer (called from ISR).
 * @param  rb    Pointer to the ring buffer instance.
 * @param  byte  The byte to store.
 * @retval true  if the byte was stored successfully.
 * @retval false if the buffer is full (byte is DROPPED — better to lose data
 *               than to block inside an ISR).
 */
bool ring_push(RingBuffer_t *rb, uint8_t byte);

/**
 * @brief  Pop one byte from the ring buffer (called from main loop).
 * @param  rb    Pointer to the ring buffer instance.
 * @param  byte  Output — the byte that was read.
 * @retval true  if a byte was available and has been placed in *byte.
 * @retval false if the buffer is empty.
 */
bool ring_pop(RingBuffer_t *rb, uint8_t *byte);

/**
 * @brief  Check whether the ring buffer is empty.
 * @param  rb  Pointer to the ring buffer instance.
 * @retval true  if empty (head == tail).
 */
bool ring_is_empty(const RingBuffer_t *rb);

/**
 * @brief  Check whether the ring buffer is full.
 * @param  rb  Pointer to the ring buffer instance.
 * @retval true  if full (next head position == tail).
 */
bool ring_is_full(const RingBuffer_t *rb);

#endif /* CIRCULAR_BUFFER_H */
