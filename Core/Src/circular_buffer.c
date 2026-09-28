/**
 * @file    circular_buffer.c
 * @brief   Lock-free ring buffer implementation for single-producer / single-consumer.
 *
 * DESIGN NOTES
 * ============
 * This is the classic SPSC (Single-Producer, Single-Consumer) ring buffer.
 * Safety guarantees:
 *
 *   - The ISR only ever modifies `head` (via ring_push).
 *   - The main loop only ever modifies `tail` (via ring_pop).
 *   - Both indices are declared `volatile` so the compiler never caches stale
 *     values in registers.
 *   - On Cortex-M4 (ARMv7-M), aligned 16-bit loads and stores are atomic, so
 *     no further synchronisation (critical sections / disable-interrupt) is
 *     required for this pattern.
 *
 * The buffer sacrifices one slot to distinguish full from empty:
 *   empty:  head == tail
 *   full:   (head + 1) & mask == tail
 * This means usable capacity is RING_BUFFER_SIZE − 1 = 255 bytes, which is
 * more than enough for interactive typing at 115200 baud.
 */

#include "circular_buffer.h"

/* -------------------------------------------------------------------------- */

void ring_init(RingBuffer_t *rb)
{
    rb->head = 0U;
    rb->tail = 0U;
    /*
     * We intentionally do NOT zero the data array.  The head/tail pointers
     * fully control which slots are valid; clearing the array would just
     * waste startup time.
     */
}

/* -------------------------------------------------------------------------- */

bool ring_push(RingBuffer_t *rb, uint8_t byte)
{
    uint16_t next_head = (rb->head + 1U) & RING_BUFFER_MASK;

    if (next_head == rb->tail) {
        /*
         * Buffer is full.  We DROP the incoming byte rather than blocking.
         * Blocking inside an ISR is never acceptable — it would stall the
         * entire system.  In practice, if the main loop is running and the
         * user is only typing, 255 bytes of buffer will never fill up.
         */
        return false;
    }

    rb->buffer[rb->head] = byte;
    /*
     * The store to buffer[] MUST be visible before we advance head, otherwise
     * the consumer could read garbage.  On Cortex-M4 with a single-core and
     * no write-buffer reordering for SRAM, a plain store is sufficient.
     * On more exotic platforms you would need a memory barrier here.
     */
    rb->head = next_head;
    return true;
}

/* -------------------------------------------------------------------------- */

bool ring_pop(RingBuffer_t *rb, uint8_t *byte)
{
    if (rb->head == rb->tail) {
        /* Buffer is empty — nothing to read. */
        return false;
    }

    *byte = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1U) & RING_BUFFER_MASK;
    return true;
}

/* -------------------------------------------------------------------------- */

bool ring_is_empty(const RingBuffer_t *rb)
{
    return (rb->head == rb->tail);
}

/* -------------------------------------------------------------------------- */

bool ring_is_full(const RingBuffer_t *rb)
{
    return (((rb->head + 1U) & RING_BUFFER_MASK) == rb->tail);
}
