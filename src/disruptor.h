/*
 *    Copyright (C) 2012 by Jules Colding <jcolding@gmail.com>.
 *
 *    All Rights Reserved.
 */

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1) Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2) Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *
 * 3) The name of the author may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL * THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef DISRUPTORC_H
#define DISRUPTORC_H

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
    #include "ac_config.h"
#endif
#include "disruptor_types.h"

#ifndef YIELD
    #define VOLATILE volatile
    #define YIELD() {}
#else
    #define VOLATILE
#endif

/*
 * An entry processor cursor spot that has this value is not used and
 * thereby vacant.
 */
#define VACANT__ (UINT_FAST64_MAX)

/*
 * Will return k iff k is a power of two, or the next power of two
 * which is greater than k.
 */
static inline size_t
next_power_of_two(size_t k) 
{
        size_t i;

        if (!k)
                return 1;

        --k;
        for (i = 1; i < sizeof(size_t)*CHAR_BIT; i <<= 1)
                k = k | k >> i;

        return ++k;
}

/*
 * Cacheline padded elements of ring.
 */
#define DEFINE_ENTRY_TYPE(content_type__, entry_type_name__)                                                            \
    typedef struct {                                                                                                    \
            content_type__ content;                                                                                     \
            uint8_t padding[(CACHE_LINE_SIZE > sizeof(content_type__)) ? CACHE_LINE_SIZE - sizeof(content_type__) : 0]; \
    } entry_type_name__ __attribute__((aligned(CACHE_LINE_SIZE)))

/*
 * Entry processors may read up to and including max_read_cursor, but
 * no futher.
 *
 * Entry publishers may write from (but excluding) max_read_cursor and
 * up to and including max_write_cursor, but no futher.
 *
 * entry_capacity__ MUST be a power of two.
 */
#define DEFINE_RING_BUFFER_TYPE(entry_processor_capacity__, entry_capacity__, entry_type_name__, ring_buffer_type_name__) \
    typedef struct {                                                                                                      \
            count_t reduced_size;                                                                                         \
            VOLATILE cursor_t max_read_cursor;                                                                            \
            VOLATILE cursor_t write_cursor;                                                                               \
            VOLATILE cursor_t entry_processor_cursors[entry_processor_capacity__];                                        \
            entry_type_name__ buffer[entry_capacity__];                                                                   \
    } ring_buffer_type_name__

/*
 * This function returns a properly aligned ring buffer or NULL.
 */
#define DEFINE_RING_BUFFER_MALLOC(ring_buffer_type_name__)                                               \
static inline ring_buffer_type_name__ *                                                                  \
ring_buffer_malloc(void)                                                                                 \
{                                                                                                        \
        ring_buffer_type_name__ *retv = NULL;                                                            \
        const size_t alignment = next_power_of_two(CACHE_LINE_SIZE);                                     \
                                                                                                         \
        return (posix_memalign((void*)&retv, alignment, sizeof(ring_buffer_type_name__)) ? NULL : retv); \
}

/*
 * This function must always be invoked on a ring buffer before it is
 * put into use.
 */
#define DEFINE_RING_BUFFER_INIT(entry_capacity__, ring_buffer_type_name__)                          \
static inline void                                                                                  \
ring_buffer_init(ring_buffer_type_name__ * const ring_buffer)                                       \
{                                                                                                   \
        unsigned int n;                                                                             \
                                                                                                    \
        memset((void*)ring_buffer, 0, sizeof(ring_buffer_type_name__));                             \
        for (n = 0; n < sizeof(ring_buffer->entry_processor_cursors)/sizeof(cursor_t); ++n)         \
                ring_buffer->entry_processor_cursors[n].sequence = VACANT__;                        \
        __atomic_store_n(&ring_buffer->reduced_size.count, entry_capacity__ - 1, __ATOMIC_SEQ_CST); \
}

/*
 * This function returns a const pointer to an entry in the ring
 * buffer.
 *
 * We are using that A % B = A & (B - 1), iff B = 2^n for some n, to
 * get the index in the ring buffer corresponding to the cursor.
 *
 * This is the reason behind the requirement that the size of the ring
 * buffer MUST be a power of two, and is why the reduced_size is the
 * actual size minus 1.
 */
#define DEFINE_RING_BUFFER_SHOW_ENTRY_FUNCTION(entry_type_name__, ring_buffer_type_name__) \
static inline const entry_type_name__*                                                     \
ring_buffer_showEntry(const ring_buffer_type_name__ * const ring_buffer,                   \
                      const cursor_t * const cursor)                                       \
{                                                                                          \
        return &ring_buffer->buffer[ring_buffer->reduced_size.count & cursor->sequence];   \
}

/*
 * This function returns a non-const pointer to an entry in the ring
 * buffer.
 *
 * See above for the implementation details.
 */
#define DEFINE_RING_BUFFER_ACQUIRE_ENTRY_FUNCTION(entry_type_name__, ring_buffer_type_name__) \
static inline entry_type_name__*                                                              \
ring_buffer_acquireEntry(ring_buffer_type_name__ * const ring_buffer,                         \
                         const cursor_t * const cursor)                                       \
{                                                                                             \
        return &ring_buffer->buffer[ring_buffer->reduced_size.count & cursor->sequence];      \
}

/*
 * Entry Processors must register before starting to process entries.
 *
 * They must furthermore update their spot, as identified by the
 * number returned when registering, in the entry_processor_cursors
 * array with the sequence number of the entry that they are currently
 * processing.
 */
#define DEFINE_ENTRY_PROCESSOR_BARRIER_REGISTER_FUNCTION(ring_buffer_type_name__)                                                  \
static inline uint_fast64_t                                                                                                        \
entry_processor_barrier_register(ring_buffer_type_name__ * const ring_buffer,                                                      \
                                 count_t * const entry_processor_number)                                                           \
{                                                                                                                                  \
        unsigned int n;                                                                                                            \
        uint_fast64_t vacant = VACANT__;                                                                                           \
                                                                                                                                   \
        do {                                                                                                                       \
                for (n = 0; n < sizeof(ring_buffer->entry_processor_cursors)/sizeof(cursor_t); ++n) {                              \
                        if (__atomic_compare_exchange_n(&ring_buffer->entry_processor_cursors[n].sequence,                         \
                                                        &vacant,                                                                   \
                                                        __atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE), \
                                                        1,                                                                         \
                                                        __ATOMIC_RELEASE,                                                          \
                                                        __ATOMIC_RELAXED)) {                                                       \
                                entry_processor_number->count = n;                                                                 \
                                goto out;                                                                                          \
                        }                                                                                                          \
                        vacant = VACANT__;                                                                                         \
                }                                                                                                                  \
        } while (1);                                                                                                               \
out:                                                                                                                               \
        vacant = 0;                                                                                                                \
        __atomic_compare_exchange_n(&ring_buffer->entry_processor_cursors[entry_processor_number->count].sequence,                 \
                                    &vacant,                                                                                       \
                                    1,                                                                                             \
                                    1,                                                                                             \
                                    __ATOMIC_RELAXED,                                                                              \
                                    __ATOMIC_RELAXED);                                                                             \
        return ring_buffer->entry_processor_cursors[entry_processor_number->count].sequence;                                       \
}

/*
 * Entry Processors must unregister to free up their spot in the entry
 * processor array in the ring buffer, so that other processors can
 * hook on.
 */
#define DEFINE_ENTRY_PROCESSOR_BARRIER_UNREGISTER_FUNCTION(ring_buffer_type_name__)                                                  \
static inline void                                                                                                                   \
entry_processor_barrier_unregister(ring_buffer_type_name__ * const ring_buffer,                                                      \
                                   const count_t * const entry_processor_number)                                                     \
{                                                                                                                                    \
        __atomic_store_n(&ring_buffer->entry_processor_cursors[entry_processor_number->count].sequence, VACANT__, __ATOMIC_RELAXED); \
}


/*
 * Entry Processors must read their spot in the
 * entry_processor_cursors array, by way of the register function, to
 * know with which sequence number to begin.
 */
#define DEFINE_ENTRY_PROCESSOR_BARRIER_WAITFOR_BLOCKING_FUNCTION(ring_buffer_type_name__)                    \
static inline void                                                                                           \
entry_processor_barrier_waitFor_blocking(const ring_buffer_type_name__ * const ring_buffer,                  \
                                         cursor_t * const cursor)                                            \
{                                                                                                            \
        while (cursor->sequence > __atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE)) \
                YIELD();                                                                                     \
                                                                                                             \
        cursor->sequence = __atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE);        \
}

/*
 * Entry Processors must read their spot in the
 * entry_processor_cursors array, by way of the register function, to
 * know with which sequence number to begin.
 */
#define DEFINE_ENTRY_PROCESSOR_BARRIER_WAITFOR_NONBLOCKING_FUNCTION(ring_buffer_type_name__)              \
static inline int                                                                                         \
entry_processor_barrier_waitFor_nonblocking(const ring_buffer_type_name__ * const ring_buffer,            \
                                            cursor_t * const cursor)                                      \
{                                                                                                         \
        if (cursor->sequence > __atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE)) \
                return 0;                                                                                 \
                                                                                                          \
        cursor->sequence = __atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE);     \
        return 1;                                                                                         \
}

/*
 * Entry Processors must tell the ring buffer how far they are done
 * reading the entries.
 */
#define DEFINE_ENTRY_PROCESSOR_BARRIER_RELEASEENTRY_FUNCTION(ring_buffer_type_name__)                                                        \
static inline void                                                                                                                           \
entry_processor_barrier_releaseEntry(ring_buffer_type_name__ * const ring_buffer,                                                            \
                                     const count_t * const entry_processor_number,                                                           \
                                     const cursor_t * const cursor)                                                                          \
{                                                                                                                                            \
        __atomic_store_n(&ring_buffer->entry_processor_cursors[entry_processor_number->count].sequence, cursor->sequence, __ATOMIC_RELAXED); \
}

/*
 * Entry Publishers must call this function to get an entry to write
 * into.  I have found that __ATOMIC_ACQUIRE (in the __atomic_load_n)
 * is actually faster than __ATOMIC_RELAXED contrary to what I would
 * expect. Mayby other entry types will show otherwise.
 * 
 * It is actually faster (at least on my machione) to do "x = 1 +
 * fetch_add(, 1)" instead of "x = add_fetch(, 1)".
 */
#define DEFINE_ENTRY_PUBLISHERPORT_NEXTENTRY_BLOCKING_FUNCTION(ring_buffer_type_name__)                             \
static inline void                                                                                                  \
publisher_port_nextEntry_blocking(ring_buffer_type_name__ * const ring_buffer,                                      \
                                  cursor_t * const  cursor)                                                         \
{                                                                                                                   \
        unsigned int n;                                                                                             \
        uint_fast64_t seq;                                                                                          \
        uint_fast64_t slowest_reader;                                                                               \
                                                                                                                    \
        cursor->sequence = 1 + __atomic_fetch_add(&ring_buffer->write_cursor.sequence, 1, __ATOMIC_RELEASE);        \
        do {                                                                                                        \
                slowest_reader = VACANT__;                                                                          \
                for (n = 0; n < sizeof(ring_buffer->entry_processor_cursors)/sizeof(cursor_t); ++n) {               \
                        seq = __atomic_load_n(&ring_buffer->entry_processor_cursors[n].sequence, __ATOMIC_ACQUIRE); \
                        if (seq < slowest_reader)                                                                   \
                                slowest_reader = seq;                                                               \
                }                                                                                                   \
                if (((cursor->sequence - slowest_reader) <= ring_buffer->reduced_size.count)                        \
                    || (VACANT__ == slowest_reader))                                                                \
                        return;                                                                                     \
                YIELD();                                                                                            \
        } while (1);                                                                                                \
}

/*
 * Entry Publishers must call this function to commit the entry to the
 * entry processors. Blocks until the entry has been committed.
 */
#define DEFINE_ENTRY_PUBLISHERPORT_COMMITENTRY_BLOCKING_FUNCTION(ring_buffer_type_name__)                           \
static inline void                                                                                                  \
publisher_port_commitEntry_blocking(ring_buffer_type_name__ * const ring_buffer,                                    \
                                    const cursor_t * const cursor)                                                  \
{                                                                                                                   \
        const uint_fast64_t required_read_sequence = cursor->sequence - 1;                                          \
                                                                                                                    \
        while (__atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE) != required_read_sequence) \
                YIELD();                                                                                            \
        __atomic_fetch_add(&ring_buffer->max_read_cursor.sequence, 1, __ATOMIC_RELEASE);                            \
}

/*
 * Entry Publishers must call this function to commit the entry to the
 * entry processors. Returns 1 (one) if the entry has been commited, 0
 * (zero) otherwise.
 */
#define DEFINE_ENTRY_PUBLISHERPORT_COMMITENTRY_NONBLOCKING_FUNCTION(ring_buffer_type_name__)                     \
static inline int                                                                                                \
publisher_port_commitEntry_nonblocking(ring_buffer_type_name__ * const ring_buffer,                              \
                                       const cursor_t * const cursor)                                            \
{                                                                                                                \
        const uint_fast64_t required_read_sequence = cursor->sequence - 1;                                       \
                                                                                                                 \
        if (__atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE) != required_read_sequence) \
                return 0;                                                                                        \
                                                                                                                 \
        __atomic_fetch_add(&ring_buffer->max_read_cursor.sequence, 1, __ATOMIC_RELEASE);                         \
        return 1;                                                                                                \
}

#endif //  DISRUPTORC_H
