/*
 *    Copyright (C) 2012-2026, Jules Colding <jcolding@gmail.com>.
 *
 *    All Rights Reserved.
 */

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     (1) Redistributions of source code must retain the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer.
 *
 *     (2) Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     (3) Neither the name of the copyright holder nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef DISRUPTORC_H
#define DISRUPTORC_H

#include "disruptor_types.h"

#include <limits.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_CONFIG_H
    #include "ac_config.h"
#endif

#ifdef LIKELY__
#undef LIKELY__
#endif
#define LIKELY__(expr__) (__builtin_expect(((expr__) ? 1 : 0), 1))

#ifdef UNLIKELY__
#undef UNLIKELY__
#endif
#define UNLIKELY__(expr__) (__builtin_expect(((expr__) ? 1 : 0), 0))

/*
 * Wait strategy: spin DISRUPTOR_PAUSE_SPINS times (rechecking the
 * condition between batches), then yield. Override before including
 * this header. BUILTIN_WAIT_COUNT__ is accepted as an alias for
 * DISRUPTOR_PAUSE_SPINS for source compatibility.
 */
#ifndef DISRUPTOR_PAUSE_SPINS
#ifdef BUILTIN_WAIT_COUNT__
#define DISRUPTOR_PAUSE_SPINS (BUILTIN_WAIT_COUNT__)
#else
#define DISRUPTOR_PAUSE_SPINS (64)
#endif
#endif

#ifndef DISRUPTOR_YIELD_EVERY
#define DISRUPTOR_YIELD_EVERY (32)
#endif

#ifndef BUILTIN_WAIT_COUNT__
#define BUILTIN_WAIT_COUNT__ (DISRUPTOR_PAUSE_SPINS)
#endif

/*
 * An entry processor cursor spot that has this value is not used and
 * is thereby vacant.
 */
#define VACANT__ (UINT_FAST64_MAX)

static inline void
disruptor_cpu_pause(void)
{
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("yield" ::: "memory");
#else
        __asm__ __volatile__("" ::: "memory");
#endif
}

static inline void
disruptor_wait_tick(unsigned * const spins)
{
        unsigned i;

        for (i = 0; i < (unsigned)DISRUPTOR_PAUSE_SPINS; ++i)
                disruptor_cpu_pause();
        if (++*spins >= (unsigned)DISRUPTOR_YIELD_EVERY) {
                *spins = 0;
                sched_yield();
        }
}

static inline __attribute__((unused)) int
disruptor_is_available(const uint_fast64_t * const available,
                       const uint_fast64_t mask,
                       const uint_fast64_t seq)
{
        return __atomic_load_n(&available[seq & mask], __ATOMIC_ACQUIRE) == seq;
}

static inline __attribute__((unused)) void
disruptor_set_available(uint_fast64_t * const available,
                        const uint_fast64_t mask,
                        const uint_fast64_t seq)
{
        __atomic_store_n(&available[seq & mask], seq, __ATOMIC_RELEASE);
}

static __attribute__((unused)) uint_fast64_t
disruptor_highest_published(const uint_fast64_t * const available,
                            const uint_fast64_t mask,
                            const uint_fast64_t lower,
                            const uint_fast64_t upper)
{
        uint_fast64_t seq;

        if (UNLIKELY__(upper < lower))
                return lower - 1;
        for (seq = lower; seq <= upper; ++seq) {
                if (!disruptor_is_available(available, mask, seq))
                        return seq - 1;
        }
        return upper;
}

static __attribute__((unused)) uint_fast64_t
disruptor_scan_gating(const struct cursor_t * const cursors,
                      const unsigned n_cursors,
                      const uint_fast64_t incur,
                      const uint_fast64_t mask)
{
        unsigned n;
        uint_fast64_t seq;
        uint_fast64_t slowest = VACANT__;

        for (n = 0; n < n_cursors; ++n) {
                seq = __atomic_load_n(&cursors[n].sequence, __ATOMIC_ACQUIRE);
                if (seq < slowest)
                        slowest = seq;
        }
        if (UNLIKELY__(VACANT__ == slowest))
                slowest = incur - (mask & incur);
        return slowest;
}

static __attribute__((unused)) void
disruptor_wait_for_capacity(uint_fast64_t * const cached_gating,
                            const struct cursor_t * const cursors,
                            const unsigned n_cursors,
                            const uint_fast64_t last_seq,
                            const uint_fast64_t mask)
{
        uint_fast64_t slowest = __atomic_load_n(cached_gating, __ATOMIC_RELAXED);
        unsigned spins = 0;

        if (LIKELY__((last_seq - slowest) <= mask))
                return;
        do {
                slowest = disruptor_scan_gating(cursors, n_cursors, last_seq, mask);
                __atomic_store_n(cached_gating, slowest, __ATOMIC_RELEASE);
                if (LIKELY__((last_seq - slowest) <= mask))
                        return;
                disruptor_wait_tick(&spins);
        } while (1);
}

static inline __attribute__((unused)) int
disruptor_try_capacity(uint_fast64_t * const cached_gating,
                       const struct cursor_t * const cursors,
                       const unsigned n_cursors,
                       const uint_fast64_t last_seq,
                       const uint_fast64_t mask)
{
        uint_fast64_t slowest = __atomic_load_n(cached_gating, __ATOMIC_RELAXED);

        if (LIKELY__((last_seq - slowest) <= mask))
                return 1;
        slowest = disruptor_scan_gating(cursors, n_cursors, last_seq, mask);
        __atomic_store_n(cached_gating, slowest, __ATOMIC_RELAXED);
        return (last_seq - slowest) <= mask;
}

static __attribute__((unused, noinline)) void
disruptor_help_advance_max_read(uint_fast64_t * const max_read,
                                const uint_fast64_t * const available,
                                const uint_fast64_t mask,
                                const uint_fast64_t claim)
{
        uint_fast64_t cur = __atomic_load_n(max_read, __ATOMIC_RELAXED);

        for (;;) {
                uint_fast64_t next = cur;

                while (next < claim && disruptor_is_available(available, mask, next + 1))
                        ++next;
                if (next == cur)
                        return;
                if (__atomic_compare_exchange_n(max_read, &cur, next, 0,
                                                __ATOMIC_RELEASE, __ATOMIC_RELAXED))
                        return;
        }
}

/*
 * Unpadded entry. Prefer this unless a slot is known to false-share.
 */
#define DEFINE_ENTRY_TYPE(content_type__, entry_type_name__) \
    struct entry_type_name__ {                               \
            content_type__ content;                          \
    }

/*
 * Cache-line padded entry. Use when neighbouring slots are written
 * and read concurrently (typically a tiny ring with a hot producer
 * sitting on the consumer's heels).
 */
#define DEFINE_PADDED_ENTRY_TYPE(content_type__, entry_type_name__)                                                                                \
    struct entry_type_name__ {                                                                                                                     \
            content_type__ content;                                                                                                                \
            uint8_t padding[(CACHE_LINE_SIZE > sizeof(content_type__)) ? (CACHE_LINE_SIZE - sizeof(content_type__)) : 0];                          \
    } __attribute__((aligned(CACHE_LINE_SIZE)))

/*
 * Entry processors may read up to and including the highest contiguous
 * published sequence (max_read_cursor, or the available[] run).
 *
 * Multi-producer publishers claim via write_cursor and publish by
 * storing the sequence into available[seq & mask]. They do not wait
 * for earlier publishers to commit.
 *
 * Single-producer publishers (sp_publisher_*) must be the only
 * claiming thread. They publish with a release store to max_read_cursor.
 *
 * entry_capacity__ MUST be a power of two.
 */
#define DEFINE_RING_BUFFER_TYPE(entry_processor_capacity__, entry_capacity__, entry_type_name__, ring_buffer_type_name__) \
    struct ring_buffer_type_name__ {                                                                                      \
            struct count_t reduced_size;                                                                                  \
            struct cursor_t slowest_entry_processor;                                                                      \
            struct cursor_t max_read_cursor;                                                                              \
            struct cursor_t write_cursor;                                                                                 \
            struct cursor_t entry_processor_cursors[entry_processor_capacity__];                                          \
            uint_fast64_t available[entry_capacity__] __attribute__((aligned(CACHE_LINE_SIZE)));                          \
            struct entry_type_name__ buffer[entry_capacity__] __attribute__((aligned(CACHE_LINE_SIZE)));                  \
    } __attribute__((aligned(PAGE_SIZE)))

#define DEFINE_RING_BUFFER_MALLOC(ring_buffer_type_name__, ring_buffer_prefix__...)                              \
static __attribute__((unused)) struct ring_buffer_type_name__ *                                                  \
ring_buffer_prefix__ ## ring_buffer_malloc(void)                                                                 \
{                                                                                                                \
        struct ring_buffer_type_name__ *retv = NULL;                                                             \
                                                                                                                 \
        return (posix_memalign((void**)&retv, PAGE_SIZE, sizeof(struct ring_buffer_type_name__)) ? NULL : retv); \
}

#define DEFINE_RING_BUFFER_INIT(entry_capacity__, ring_buffer_type_name__, ring_buffer_prefix__...) \
static __attribute__((unused)) void                                                                 \
ring_buffer_prefix__ ## ring_buffer_init(struct ring_buffer_type_name__ * const ring_buffer)        \
{                                                                                                   \
        unsigned int n;                                                                             \
                                                                                                    \
        memset((void*)ring_buffer, 0, sizeof(struct ring_buffer_type_name__));                      \
        for (n = 0; n < sizeof(ring_buffer->entry_processor_cursors)/sizeof(struct cursor_t); ++n)  \
                ring_buffer->entry_processor_cursors[n].sequence = VACANT__;                        \
        memset(ring_buffer->available, 0xFF, sizeof(ring_buffer->available));                       \
        __atomic_store_n(&ring_buffer->reduced_size.count, entry_capacity__ - 1, __ATOMIC_SEQ_CST); \
}

#define DEFINE_RING_BUFFER_SHOW_ENTRY_FUNCTION(entry_type_name__, ring_buffer_type_name__, ring_buffer_prefix__...) \
static inline __attribute__((unused)) const struct entry_type_name__*                                               \
ring_buffer_prefix__ ## ring_buffer_show_entry(const struct ring_buffer_type_name__ * const ring_buffer,            \
                                               const struct cursor_t * const cursor)                                \
{                                                                                                                   \
        return &ring_buffer->buffer[ring_buffer->reduced_size.count & cursor->sequence];                            \
}                                                                                                                   \
                                                                                                                    \
static inline __attribute__((unused)) const struct entry_type_name__*                                               \
ring_buffer_prefix__ ## ring_buffer_show_entry_at(const struct ring_buffer_type_name__ * const ring_buffer,         \
                                                  const uint_fast64_t sequence)                                     \
{                                                                                                                   \
        return &ring_buffer->buffer[ring_buffer->reduced_size.count & sequence];                                    \
}

#define DEFINE_RING_BUFFER_ACQUIRE_ENTRY_FUNCTION(entry_type_name__, ring_buffer_type_name__, ring_buffer_prefix__...) \
static inline __attribute__((unused)) struct entry_type_name__*                                                        \
ring_buffer_prefix__ ## ring_buffer_acquire_entry(struct ring_buffer_type_name__ * const ring_buffer,                  \
                                                  const struct cursor_t * const cursor)                                \
{                                                                                                                      \
        return &ring_buffer->buffer[ring_buffer->reduced_size.count & cursor->sequence];                               \
}                                                                                                                      \
                                                                                                                       \
static inline __attribute__((unused)) struct entry_type_name__*                                                        \
ring_buffer_prefix__ ## ring_buffer_acquire_entry_at(struct ring_buffer_type_name__ * const ring_buffer,               \
                                                     const uint_fast64_t sequence)                                     \
{                                                                                                                      \
        return &ring_buffer->buffer[ring_buffer->reduced_size.count & sequence];                                       \
}

#define DEFINE_ENTRY_PROCESSOR_BARRIER_REGISTER_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)                                 \
static inline __attribute__((unused)) uint_fast64_t                                                                                        \
ring_buffer_prefix__ ## entry_processor_barrier_register(struct ring_buffer_type_name__ * const ring_buffer,                               \
                                                         struct count_t * const entry_processor_number)                                    \
{                                                                                                                                          \
        unsigned int n;                                                                                                                    \
        uint_fast64_t vacant = VACANT__;                                                                                                   \
                                                                                                                                           \
        do {                                                                                                                               \
                for (n = 0; n < sizeof(ring_buffer->entry_processor_cursors)/sizeof(struct cursor_t); ++n) {                               \
                        if (__atomic_compare_exchange_n(&ring_buffer->entry_processor_cursors[n].sequence,                                 \
                                                        &vacant,                                                                           \
                                                        __atomic_load_n(&ring_buffer->slowest_entry_processor.sequence, __ATOMIC_ACQUIRE), \
                                                        1,                                                                                 \
                                                        __ATOMIC_RELEASE,                                                                  \
                                                        __ATOMIC_RELAXED)) {                                                               \
                                entry_processor_number->count = n;                                                                         \
                                goto out;                                                                                                  \
                        }                                                                                                                  \
                        vacant = VACANT__;                                                                                                 \
                }                                                                                                                          \
        } while (1);                                                                                                                       \
out:                                                                                                                                       \
        if (!ring_buffer->entry_processor_cursors[entry_processor_number->count].sequence) {                                               \
                __atomic_store_n(&ring_buffer->entry_processor_cursors[entry_processor_number->count].sequence, 1, __ATOMIC_RELEASE);      \
                return 1;                                                                                                                  \
        }                                                                                                                                  \
        return ring_buffer->entry_processor_cursors[entry_processor_number->count].sequence;                                               \
}

#define DEFINE_ENTRY_PROCESSOR_BARRIER_UNREGISTER_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)                         \
static inline __attribute__((unused)) void                                                                                           \
ring_buffer_prefix__ ## entry_processor_barrier_unregister(struct ring_buffer_type_name__ * const ring_buffer,                       \
                                                           const struct count_t * const entry_processor_number)                      \
{                                                                                                                                    \
        __atomic_store_n(&ring_buffer->entry_processor_cursors[entry_processor_number->count].sequence, VACANT__, __ATOMIC_RELEASE); \
}

#define DEFINE_ENTRY_PROCESSOR_BARRIER_WAITFOR_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)          \
static inline __attribute__((unused)) void                                                                                  \
ring_buffer_prefix__ ## entry_processor_barrier_wait_for_blocking(const struct ring_buffer_type_name__ * const ring_buffer, \
                                                                  struct cursor_t * __restrict__ const cursor)              \
{                                                                                                                           \
        const uint_fast64_t needed = cursor->sequence;                                                                      \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                                         \
        uint_fast64_t published;                                                                                            \
        uint_fast64_t claim;                                                                                                \
        uint_fast64_t ext;                                                                                                  \
        unsigned spins = 0;                                                                                                 \
                                                                                                                            \
        published = __atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE);                              \
        while (published < needed                                                                                           \
               && !disruptor_is_available(ring_buffer->available, mask, needed)) {                                          \
                disruptor_wait_tick(&spins);                                                                                \
                published = __atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE);                      \
        }                                                                                                                   \
        if (published < needed)                                                                                             \
                published = needed;                                                                                         \
        claim = __atomic_load_n(&ring_buffer->write_cursor.sequence, __ATOMIC_RELAXED);                                     \
        if (claim > published) {                                                                                            \
                ext = disruptor_highest_published(ring_buffer->available, mask, published + 1, claim);                      \
                if (ext > published)                                                                                        \
                        published = ext;                                                                                    \
        }                                                                                                                   \
        ext = __atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE);                                    \
        if (ext > published)                                                                                                \
                published = ext;                                                                                            \
        cursor->sequence = published;                                                                                       \
}

#define DEFINE_ENTRY_PROCESSOR_BARRIER_WAITFOR_NONBLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)          \
static inline __attribute__((unused)) int                                                                                      \
ring_buffer_prefix__ ## entry_processor_barrier_wait_for_nonblocking(const struct ring_buffer_type_name__ * const ring_buffer, \
                                                                     struct cursor_t * __restrict__ const cursor)              \
{                                                                                                                              \
        const uint_fast64_t needed = cursor->sequence;                                                                         \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                                            \
        uint_fast64_t published;                                                                                               \
        uint_fast64_t claim;                                                                                                   \
        uint_fast64_t ext;                                                                                                     \
                                                                                                                               \
        published = __atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE);                                 \
        if (published < needed                                                                                                 \
            && !disruptor_is_available(ring_buffer->available, mask, needed))                                                  \
                return 0;                                                                                                      \
        if (published < needed)                                                                                                \
                published = needed;                                                                                            \
        claim = __atomic_load_n(&ring_buffer->write_cursor.sequence, __ATOMIC_RELAXED);                                        \
        if (claim > published) {                                                                                               \
                ext = disruptor_highest_published(ring_buffer->available, mask, published + 1, claim);                         \
                if (ext > published)                                                                                           \
                        published = ext;                                                                                       \
        }                                                                                                                      \
        ext = __atomic_load_n(&ring_buffer->max_read_cursor.sequence, __ATOMIC_ACQUIRE);                                       \
        if (ext > published)                                                                                                   \
                published = ext;                                                                                               \
        cursor->sequence = published;                                                                                          \
        return 1;                                                                                                              \
}

#define DEFINE_ENTRY_PROCESSOR_BARRIER_RELEASEENTRY_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)                               \
static inline __attribute__((unused)) void                                                                                                   \
ring_buffer_prefix__ ## entry_processor_barrier_release_entry(struct ring_buffer_type_name__ * const ring_buffer,                            \
                                                              const struct count_t * __restrict__ const entry_processor_number,              \
                                                              const struct cursor_t * __restrict__ const cursor)                             \
{                                                                                                                                            \
        __atomic_store_n(&ring_buffer->entry_processor_cursors[entry_processor_number->count].sequence, cursor->sequence, __ATOMIC_RELEASE); \
}

/*
 * Multi-producer claim. write_cursor is advanced with a relaxed
 * fetch_add; consumers are only rescanned when the cached gating
 * sequence says the claim would wrap the ring.
 */
#define DEFINE_ENTRY_PUBLISHER_NEXTENTRY_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)              \
static inline __attribute__((always_inline, unused)) void                                                                 \
ring_buffer_prefix__ ## publisher_next_entry_blocking(struct ring_buffer_type_name__ * const ring_buffer,                 \
                                                      struct cursor_t * __restrict__ const cursor)                        \
{                                                                                                                         \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                                       \
        const uint_fast64_t incur =                                                                                       \
                1 + __atomic_fetch_add(&ring_buffer->write_cursor.sequence, 1, __ATOMIC_RELAXED);                         \
                                                                                                                          \
        cursor->sequence = incur;                                                                                         \
        disruptor_wait_for_capacity(&ring_buffer->slowest_entry_processor.sequence,                                       \
                                    ring_buffer->entry_processor_cursors,                                                 \
                                    (unsigned)(sizeof(ring_buffer->entry_processor_cursors)/sizeof(struct cursor_t)),     \
                                    incur, mask);                                                                         \
}

#define DEFINE_ENTRY_PUBLISHER_NEXTENTRIES_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)            \
static inline __attribute__((unused)) void                                                                                \
ring_buffer_prefix__ ## publisher_next_entries_blocking(struct ring_buffer_type_name__ * const ring_buffer,               \
                                                        struct cursor_t * __restrict__ const cursor,                      \
                                                        const uint_fast64_t n)                                            \
{                                                                                                                         \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                                       \
        const uint_fast64_t last =                                                                                        \
                n + __atomic_fetch_add(&ring_buffer->write_cursor.sequence, n, __ATOMIC_RELAXED);                         \
                                                                                                                          \
        cursor->sequence = last;                                                                                          \
        disruptor_wait_for_capacity(&ring_buffer->slowest_entry_processor.sequence,                                       \
                                    ring_buffer->entry_processor_cursors,                                                 \
                                    (unsigned)(sizeof(ring_buffer->entry_processor_cursors)/sizeof(struct cursor_t)),     \
                                    last, mask);                                                                          \
}

#define DEFINE_ENTRY_PUBLISHER_NEXTENTRY_NONBLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)           \
static inline __attribute__((unused)) int                                                                                 \
ring_buffer_prefix__ ## publisher_next_entry_nonblocking(struct ring_buffer_type_name__ * const ring_buffer,              \
                                                         struct cursor_t * __restrict__ const cursor)                     \
{                                                                                                                         \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                                       \
        uint_fast64_t current = __atomic_load_n(&ring_buffer->write_cursor.sequence, __ATOMIC_RELAXED);                   \
        const uint_fast64_t incur = current + 1;                                                                          \
                                                                                                                          \
        if (!disruptor_try_capacity(&ring_buffer->slowest_entry_processor.sequence,                                       \
                                    ring_buffer->entry_processor_cursors,                                                 \
                                    (unsigned)(sizeof(ring_buffer->entry_processor_cursors)/sizeof(struct cursor_t)),     \
                                    incur, mask))                                                                         \
                return 0;                                                                                                 \
        if (__atomic_compare_exchange_n(&ring_buffer->write_cursor.sequence, &current, incur,                             \
                                        0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {                                         \
                cursor->sequence = incur;                                                                                 \
                return 1;                                                                                                 \
        }                                                                                                                 \
        return 0;                                                                                                         \
}

#define DEFINE_ENTRY_PUBLISHER_NEXTENTRIES_NONBLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)         \
static inline __attribute__((unused)) int                                                                                 \
ring_buffer_prefix__ ## publisher_next_entries_nonblocking(struct ring_buffer_type_name__ * const ring_buffer,            \
                                                           struct cursor_t * __restrict__ const cursor,                   \
                                                           const uint_fast64_t n)                                         \
{                                                                                                                         \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                                       \
        uint_fast64_t current = __atomic_load_n(&ring_buffer->write_cursor.sequence, __ATOMIC_RELAXED);                   \
        const uint_fast64_t last = current + n;                                                                           \
                                                                                                                          \
        if (!disruptor_try_capacity(&ring_buffer->slowest_entry_processor.sequence,                                       \
                                    ring_buffer->entry_processor_cursors,                                                 \
                                    (unsigned)(sizeof(ring_buffer->entry_processor_cursors)/sizeof(struct cursor_t)),     \
                                    last, mask))                                                                          \
                return 0;                                                                                                 \
        if (__atomic_compare_exchange_n(&ring_buffer->write_cursor.sequence, &current, last,                               \
                                        0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {                                         \
                cursor->sequence = last;                                                                                  \
                return 1;                                                                                                 \
        }                                                                                                                 \
        return 0;                                                                                                         \
}

/*
 * Multi-producer publish. Marks the sequence available and tries to
 * advance max_read_cursor over the contiguous published run. Does not
 * wait for earlier publishers — they publish independently.
 */
#define DEFINE_ENTRY_PUBLISHER_COMMITENTRY_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...) \
static inline __attribute__((always_inline, unused)) void                                                      \
ring_buffer_prefix__ ## publisher_commit_entry_blocking(struct ring_buffer_type_name__ * const ring_buffer,    \
                                                        const struct cursor_t * __restrict__ const cursor)     \
{                                                                                                              \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                            \
        const uint_fast64_t seq = cursor->sequence;                                                            \
        uint_fast64_t expected = seq - 1;                                                                      \
                                                                                                               \
        disruptor_set_available(ring_buffer->available, mask, seq);                                            \
        if (__atomic_compare_exchange_n(&ring_buffer->max_read_cursor.sequence, &expected, seq,                \
                                        0, __ATOMIC_RELEASE, __ATOMIC_RELAXED))                                \
                return;                                                                                        \
        disruptor_help_advance_max_read(&ring_buffer->max_read_cursor.sequence,                                \
                                        ring_buffer->available,                                                \
                                        mask,                                                                  \
                                        __atomic_load_n(&ring_buffer->write_cursor.sequence, __ATOMIC_RELAXED)); \
}

#define DEFINE_ENTRY_PUBLISHER_COMMITENTRIES_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)        \
static inline __attribute__((unused)) void                                                                     \
ring_buffer_prefix__ ## publisher_commit_entries(struct ring_buffer_type_name__ * const ring_buffer,           \
                                                 const struct cursor_t * __restrict__ const cursor,            \
                                                 const uint_fast64_t n)                                        \
{                                                                                                              \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                            \
        const uint_fast64_t last = cursor->sequence;                                                           \
        const uint_fast64_t first = last - n + 1;                                                              \
        uint_fast64_t seq;                                                                                     \
        uint_fast64_t expected = first - 1;                                                                    \
                                                                                                               \
        for (seq = first; seq <= last; ++seq)                                                                  \
                disruptor_set_available(ring_buffer->available, mask, seq);                                    \
        if (__atomic_compare_exchange_n(&ring_buffer->max_read_cursor.sequence, &expected, last,               \
                                        0, __ATOMIC_RELEASE, __ATOMIC_RELAXED))                                \
                return;                                                                                        \
        disruptor_help_advance_max_read(&ring_buffer->max_read_cursor.sequence,                                \
                                        ring_buffer->available,                                                \
                                        mask,                                                                  \
                                        __atomic_load_n(&ring_buffer->write_cursor.sequence, __ATOMIC_RELAXED)); \
}

#define DEFINE_ENTRY_PUBLISHER_COMMITENTRY_NONBLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...) \
static inline __attribute__((unused)) int                                                                         \
ring_buffer_prefix__ ## publisher_commit_entry_nonblocking(struct ring_buffer_type_name__ * const ring_buffer,    \
                                                           const struct cursor_t * __restrict__ const cursor)     \
{                                                                                                                 \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                               \
        const uint_fast64_t seq = cursor->sequence;                                                               \
        uint_fast64_t expected = seq - 1;                                                                         \
                                                                                                                  \
        disruptor_set_available(ring_buffer->available, mask, seq);                                               \
        if (!__atomic_compare_exchange_n(&ring_buffer->max_read_cursor.sequence, &expected, seq,                  \
                                         0, __ATOMIC_RELEASE, __ATOMIC_RELAXED))                                  \
                disruptor_help_advance_max_read(&ring_buffer->max_read_cursor.sequence,                           \
                                                ring_buffer->available,                                           \
                                                mask,                                                             \
                                                __atomic_load_n(&ring_buffer->write_cursor.sequence,              \
                                                                __ATOMIC_RELAXED));                               \
        return 1;                                                                                                 \
}

/*
 * Single-producer claim / publish. The calling thread must be the
 * only publisher. write_cursor is a plain increment; commit is a
 * release store of max_read_cursor (publishing the whole claimed
 * range up to cursor->sequence).
 */
#define DEFINE_SP_PUBLISHER_NEXTENTRY_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)             \
static inline __attribute__((always_inline, unused)) void                                                             \
ring_buffer_prefix__ ## sp_publisher_next_entry_blocking(struct ring_buffer_type_name__ * const ring_buffer,          \
                                                         struct cursor_t * __restrict__ const cursor)                 \
{                                                                                                                     \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                                   \
        const uint_fast64_t incur = ring_buffer->write_cursor.sequence + 1;                                           \
                                                                                                                      \
        disruptor_wait_for_capacity(&ring_buffer->slowest_entry_processor.sequence,                                   \
                                    ring_buffer->entry_processor_cursors,                                             \
                                    (unsigned)(sizeof(ring_buffer->entry_processor_cursors)/sizeof(struct cursor_t)), \
                                    incur, mask);                                                                     \
        __atomic_store_n(&ring_buffer->write_cursor.sequence, incur, __ATOMIC_RELAXED);                               \
        cursor->sequence = incur;                                                                                     \
}

#define DEFINE_SP_PUBLISHER_NEXTENTRIES_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)           \
static inline __attribute__((always_inline, unused)) void                                                             \
ring_buffer_prefix__ ## sp_publisher_next_entries_blocking(struct ring_buffer_type_name__ * const ring_buffer,        \
                                                           struct cursor_t * __restrict__ const cursor,               \
                                                           const uint_fast64_t n)                                     \
{                                                                                                                     \
        const uint_fast64_t mask = ring_buffer->reduced_size.count;                                                   \
        const uint_fast64_t last = ring_buffer->write_cursor.sequence + n;                                            \
                                                                                                                      \
        disruptor_wait_for_capacity(&ring_buffer->slowest_entry_processor.sequence,                                   \
                                    ring_buffer->entry_processor_cursors,                                             \
                                    (unsigned)(sizeof(ring_buffer->entry_processor_cursors)/sizeof(struct cursor_t)), \
                                    last, mask);                                                                      \
        __atomic_store_n(&ring_buffer->write_cursor.sequence, last, __ATOMIC_RELAXED);                                \
        cursor->sequence = last;                                                                                      \
}

#define DEFINE_SP_PUBLISHER_COMMITENTRY_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__...)  \
static inline __attribute__((always_inline, unused)) void                                           \
ring_buffer_prefix__ ## sp_publisher_commit_entry(struct ring_buffer_type_name__ * const ring_buffer, \
                                                  const struct cursor_t * __restrict__ const cursor)  \
{                                                                                                   \
        __atomic_store_n(&ring_buffer->max_read_cursor.sequence, cursor->sequence, __ATOMIC_RELEASE); \
}

#define DEFINE_ALL_RING_BUFFER_FUNCTIONS(entry_capacity__, entry_type_name__, ring_buffer_type_name__, ring_buffer_prefix__...) \
        DEFINE_RING_BUFFER_MALLOC(ring_buffer_type_name__, ring_buffer_prefix__)                                                \
        DEFINE_RING_BUFFER_INIT(entry_capacity__, ring_buffer_type_name__, ring_buffer_prefix__)                                \
        DEFINE_RING_BUFFER_SHOW_ENTRY_FUNCTION(entry_type_name__, ring_buffer_type_name__, ring_buffer_prefix__)                \
        DEFINE_RING_BUFFER_ACQUIRE_ENTRY_FUNCTION(entry_type_name__, ring_buffer_type_name__, ring_buffer_prefix__)             \
        DEFINE_ENTRY_PROCESSOR_BARRIER_REGISTER_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                         \
        DEFINE_ENTRY_PROCESSOR_BARRIER_UNREGISTER_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                       \
        DEFINE_ENTRY_PROCESSOR_BARRIER_WAITFOR_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                 \
        DEFINE_ENTRY_PROCESSOR_BARRIER_WAITFOR_NONBLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)              \
        DEFINE_ENTRY_PROCESSOR_BARRIER_RELEASEENTRY_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                     \
        DEFINE_ENTRY_PUBLISHER_NEXTENTRY_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                       \
        DEFINE_ENTRY_PUBLISHER_NEXTENTRIES_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                     \
        DEFINE_ENTRY_PUBLISHER_NEXTENTRY_NONBLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                    \
        DEFINE_ENTRY_PUBLISHER_NEXTENTRIES_NONBLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                  \
        DEFINE_ENTRY_PUBLISHER_COMMITENTRY_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                     \
        DEFINE_ENTRY_PUBLISHER_COMMITENTRIES_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                            \
        DEFINE_ENTRY_PUBLISHER_COMMITENTRY_NONBLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                  \
        DEFINE_SP_PUBLISHER_NEXTENTRY_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                          \
        DEFINE_SP_PUBLISHER_NEXTENTRIES_BLOCKING_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)                        \
        DEFINE_SP_PUBLISHER_COMMITENTRY_FUNCTION(ring_buffer_type_name__, ring_buffer_prefix__)

#endif /* DISRUPTORC_H */
