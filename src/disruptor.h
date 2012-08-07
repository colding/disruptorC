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

#ifdef HAVE_CONFIG_H
    #include "ac_config.h"
#endif
#include <inttypes.h>

#ifndef YIELD
    #define VOLATILE volatile
    #define YIELD() {}
#else
    #define VOLATILE
#endif

/*
 * An event processor cursor spot that has this value is not used and
 * thereby vacant.
 */
#define VACANT__ (UINT_FAST64_MAX)

/*
 * Cacheline padded counter.
 */
typedef struct {
        uint_fast64_t count;
        uint8_t padding[(CACHE_LINE_SIZE > sizeof(uint_fast64_t)) ? CACHE_LINE_SIZE - sizeof(uint_fast64_t) : 0];
} count_t __attribute__((aligned(CACHE_LINE_SIZE)));

/*
 * Cacheline padded cursor into ring buffer. Wrapping around forever.
 */
typedef struct {
        uint_fast64_t sequence;
        uint8_t padding[(CACHE_LINE_SIZE > sizeof(uint_fast64_t)) ? CACHE_LINE_SIZE - sizeof(uint_fast64_t) : 0];
} cursor_t __attribute__((aligned(CACHE_LINE_SIZE)));

/*
 * Cacheline padded elements of ring.
 */
#define DEFINE_EVENT_TYPE(content_type__, event_type_name__)                                                            \
    typedef struct {                                                                                                    \
            content_type__ content;                                                                                     \
            uint8_t padding[(CACHE_LINE_SIZE > sizeof(content_type__)) ? CACHE_LINE_SIZE - sizeof(content_type__) : 0]; \
    } event_type_name__ __attribute__((aligned(CACHE_LINE_SIZE)))

/*
 * Event processors may read up to and including max_read_cursor, but
 * no futher.
 *
 * Event publishers may write from (but excluding) max_read_cursor and
 * up to and including max_write_cursor, but no futher.
 *
 * event_count__ MUST be a power of two.
 */
#define DEFINE_RING_BUFFER_TYPE(event_processor_count__, event_count__, event_type_name__, ring_buffer_type_name__) \
    typedef struct {                                                                                                \
            count_t reduced_size;                                                                                   \
            VOLATILE cursor_t max_read_cursor;                                                                      \
            VOLATILE cursor_t write_cursor;                                                                         \
            VOLATILE cursor_t event_processor_cursors[event_processor_count__];                                     \
            event_type_name__ buffer[event_count__];                                                                \
    } ring_buffer_type_name__ __attribute__((aligned(CACHE_LINE_SIZE)))

/*
 * This function must always be invoked on a ring buffer before it is
 * put into use.
 */
#define DEFINE_RING_BUFFER_INIT(event_processor_count__, event_count__, ring_buffer_type_name__) \
static inline void                                                                               \
ring_buffer_init(ring_buffer_type_name__ * const ring_buffer)                                    \
{                                                                                                \
        unsigned int n;                                                                          \
                                                                                                 \
        memset((void*)ring_buffer, 0, sizeof(ring_buffer_type_name__));                          \
        for (n = 0; n < sizeof(ring_buffer->event_processor_cursors)/sizeof(cursor_t); ++n)      \
                ring_buffer->event_processor_cursors[n].sequence = VACANT__;                     \
        __atomic_store_n(&ring_buffer->reduced_size.count, event_count__ - 1, __ATOMIC_SEQ_CST); \
}

/*
 * Using that A % B = A & (B - 1), iff B = 2^n for some n. Therefore,
 * reduced_size is the actual size minus 1.
 */
static inline uint_fast64_t
get_index(const uint_fast64_t reduced_size, const cursor_t * const cursor)
{
        return (cursor->sequence & reduced_size);
}

/*
 * EventProcessors must register before starting to process events.
 *
 * They must furthermore update their spot, as identified by the
 * number returned when registering, in the event_processor_cursors
 * array with the sequence number of the event that they are currently
 * processing.
 */
#define DEFINE_EVENT_PROCESSOR_BARRIER_REGISTER_FUNCTION(ring_buffer_type_name__)                                                                                                                    \
static inline void                                                                                                                                                                                   \
event_processor_barrier_register(ring_buffer_type_name__ * const ring_buffer,                                                                                                                        \
                                 count_t * const event_processor_number)                                                                                                                             \
{                                                                                                                                                                                                    \
        unsigned int n;                                                                                                                                                                              \
        uint_fast64_t vacant = VACANT__;                                                                                                                                                             \
                                                                                                                                                                                                     \
        do {                                                                                                                                                                                         \
                for (n = 0; n < sizeof(ring_buffer->event_processor_cursors)/sizeof(cursor_t); ++n) {                                                                                                \
                        if (__atomic_compare_exchange_n(&ring_buffer->event_processor_cursors[n].sequence, &vacant, ring_buffer->max_read_cursor.sequence, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) { \
                                event_processor_number->count = n;                                                                                                                                   \
                                return;                                                                                                                                                              \
                        }                                                                                                                                                                            \
                        vacant = VACANT__;                                                                                                                                                           \
                }                                                                                                                                                                                    \
        } while (1);                                                                                                                                                                                 \
}

/*
 * EventProcessors must unregister to free up their spot in the event
 * processor array in the ring buffer, so that other processors can
 * hook on.
 */
#define DEFINE_EVENT_PROCESSOR_BARRIER_UNREGISTER_FUNCTION(ring_buffer_type_name__)                                                  \
static inline void                                                                                                                   \
event_processor_barrier_unregister(ring_buffer_type_name__ * const ring_buffer,                                                      \
                                   count_t * const event_processor_number)                                                           \
{                                                                                                                                    \
        __atomic_store_n(&ring_buffer->event_processor_cursors[event_processor_number->count].sequence, VACANT__, __ATOMIC_RELEASE); \
}


/*
 * EventProcessors must read their spot in the event_processor_cursors
 * array to know with which sequence number to begin. An exception is
 * if the sequence number is 0, then it must skip 0 and waitFor 1.
 */
#define DEFINE_EVENT_PROCESSOR_BARRIER_WAITFOR_BLOCKING_FUNCTION(ring_buffer_type_name__)   \
static inline void                                                                          \
event_processor_barrier_waitFor_blocking(const ring_buffer_type_name__ * const ring_buffer, \
                                         cursor_t * const cursor)                           \
{                                                                                           \
        while (cursor->sequence > ring_buffer->max_read_cursor.sequence)                    \
                YIELD();                                                                    \
                                                                                            \
        cursor->sequence = ring_buffer->max_read_cursor.sequence;                           \
}

/*
 * EventProcessors must read their spot in the event_processor_cursors
 * array to know with which sequence number to begin. An exception is
 * if the sequence number is 0, then it must skip 0 and waitFor 1.
 */
#define DEFINE_EVENT_PROCESSOR_BARRIER_WAITFOR_NONBLOCKING_FUNCTION(ring_buffer_type_name__)   \
static inline int                                                                              \
event_processor_barrier_waitFor_nonblocking(const ring_buffer_type_name__ * const ring_buffer, \
                                            cursor_t * const cursor)                           \
{                                                                                              \
        if (cursor->sequence > ring_buffer->max_read_cursor.sequence)                          \
                return 0;                                                                      \
                                                                                               \
        cursor->sequence = ring_buffer->max_read_cursor.sequence;                              \
                                                                                               \
        return 1;                                                                              \
}

/*
 * This function returns a pointer to an event in the ring buffer.
 */
#define DEFINE_EVENT_PROCESSOR_BARRIER_GETENTRY_FUNCTION(event_type_name__, ring_buffer_type_name__) \
static inline const event_type_name__*                                                               \
event_processor_barrier_getEntry(const ring_buffer_type_name__ * const ring_buffer,                  \
                                 const cursor_t * const cursor)                                      \
{                                                                                                    \
        return &ring_buffer->buffer[get_index(ring_buffer->reduced_size.count, cursor)];             \
}

/*
 * EventProcessors must tell the ring buffer how far they are done
 * reading the events.
 */
#define DEFINE_EVENT_PROCESSOR_BARRIER_RELEASEENTRY_FUNCTION(ring_buffer_type_name__)                                                        \
static inline void                                                                                                                           \
event_processor_barrier_releaseEntry(ring_buffer_type_name__ * const ring_buffer,                                                            \
                                     const count_t * const event_processor_number,                                                           \
                                     const cursor_t * const cursor)                                                                          \
{                                                                                                                                            \
        __atomic_store_n(&ring_buffer->event_processor_cursors[event_processor_number->count].sequence, cursor->sequence, __ATOMIC_RELEASE); \
}

/*
 * Publishers must call this function to get an event to write into.
 */
#define DEFINE_EVENT_PUBLISHERPORT_NEXTENTRY_BLOCKING_FUNCTION(ring_buffer_type_name__)                             \
static inline void                                                                                                  \
publisher_port_nextEntry_blocking(ring_buffer_type_name__ * const ring_buffer,                                      \
                                  cursor_t * const  cursor)                                                         \
{                                                                                                                   \
        unsigned int n;                                                                                             \
        uint_fast64_t seq;                                                                                          \
        cursor_t minimum_reader;                                                                                    \
                                                                                                                    \
        cursor->sequence = __atomic_add_fetch(&ring_buffer->write_cursor.sequence, 1, __ATOMIC_RELEASE);            \
        do {                                                                                                        \
                minimum_reader.sequence = UINT_FAST64_MAX;                                                          \
                for (n = 0; n < sizeof(ring_buffer->event_processor_cursors)/sizeof(cursor_t); ++n) {               \
                        seq = __atomic_load_n(&ring_buffer->event_processor_cursors[n].sequence, __ATOMIC_ACQUIRE); \
                        if (seq < minimum_reader.sequence)                                                          \
                                minimum_reader.sequence = seq;                                                      \
                }                                                                                                   \
                if (((cursor->sequence - minimum_reader.sequence) <= ring_buffer->reduced_size.count)               \
                    || (UINT_FAST64_MAX == minimum_reader.sequence))                                                \
                        return;                                                                                     \
                YIELD();                                                                                            \
        } while (1);                                                                                                \
}

/*
 * Publishers must call this function to get an event to write into.
 */
#define DEFINE_EVENT_PUBLISHERPORT_NEXTENTRY_NONBLOCKING_FUNCTION(ring_buffer_type_name__)                    \
static inline int                                                                                             \
publisher_port_nextEntry_nonblocking(ring_buffer_type_name__ * const ring_buffer,                             \
                                     cursor_t * const  cursor)                                                \
{                                                                                                             \
        unsigned int n;                                                                                       \
        uint_fast64_t seq;                                                                                    \
        cursor_t minimum_reader;                                                                              \
                                                                                                              \
        cursor->sequence = __atomic_add_fetch(&ring_buffer->write_cursor.sequence, 1, __ATOMIC_RELEASE);      \
        minimum_reader.sequence = UINT_FAST64_MAX;                                                            \
        for (n = 0; n < sizeof(ring_buffer->event_processor_cursors)/sizeof(cursor_t); ++n) {                 \
                seq = __atomic_load_n(&ring_buffer->event_processor_cursors[n].sequence, __ATOMIC_ACQUIRE);   \
                if (seq < minimum_reader.sequence)                                                            \
                        minimum_reader.sequence = seq;                                                        \
        }                                                                                                     \
        if (((cursor->sequence - minimum_reader.sequence) <= ring_buffer->reduced_size.count)                 \
            || (UINT_FAST64_MAX == minimum_reader.sequence))                                                  \
                return 1;                                                                                     \
        return 0;                                                                                             \
}

/*
 * Publishers must call this function to commit the event to the event
 * processors. Blocks until the event has been committed.
 */
#define DEFINE_EVENT_PUBLISHERPORT_COMMITENTRY_BLOCKING_FUNCTION(ring_buffer_type_name__)                           \
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
 * Publishers must call this function to commit the event to the event
 * processors. Returns 1 (one) if the event has been commited, 0
 * (zero) otherwise.
 */
#define DEFINE_EVENT_PUBLISHERPORT_COMMITENTRY_NONBLOCKING_FUNCTION(ring_buffer_type_name__)                     \
static inline int                                                                                                \
publisher_port_commitEntry_nonblocking(ring_buffer_type_name__ * const ring_buffer,                              \
                                       const cursor_t * const cursor)                                            \
{                                                                                                                \
        const uint_fast64_t required_read_sequence = cursor->sequence - 1;                                       \
                                                                                                                 \
        if (__atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE) != required_read_sequence) \
                return 0;                                                                                        \
        __atomic_fetch_add(&ring_buffer->max_read_cursor.sequence, 1, __ATOMIC_RELEASE);                         \
        return 1;                                                                                                \
}

#endif //  DISRUPTORC_H
