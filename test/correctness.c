/*
 *  Copyright (C) 2012-2026 Jules Colding <jcolding@gmail.com>
 *
 *  All Rights Reserved.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 *  You can use, modify and redistribute it in any way you want.
 */

#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <pthread.h>

#ifdef HAVE_CONFIG_H
    #include "ac_config.h"
#endif
#include "src/disruptor.h"

#define STOP UINT64_MAX
#define ENTRIES_TO_GENERATE (400)
#define ENTRY_BUFFER_SIZE (16)
#define MAX_ENTRY_PROCESSORS (2)
#define BATCH_N (4)

DEFINE_ENTRY_TYPE(uint_fast64_t, entry_t);
DEFINE_RING_BUFFER_TYPE(MAX_ENTRY_PROCESSORS, ENTRY_BUFFER_SIZE, entry_t, ring_buffer_t);
DEFINE_ALL_RING_BUFFER_FUNCTIONS(ENTRY_BUFFER_SIZE, entry_t, ring_buffer_t);

struct ring_buffer_t ring_buffer;
static volatile int g_failed;

static int
create_thread(pthread_t * const thread_id,
              void *thread_arg,
              void *(*thread_func)(void *))
{
        int retv = 0;
        pthread_attr_t thread_attr;

        if (pthread_attr_init(&thread_attr))
                return 0;

        if (pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE))
                goto err;

        if (pthread_create(thread_id, &thread_attr, thread_func, thread_arg))
                goto err;

        retv = 1;
err:
        pthread_attr_destroy(&thread_attr);

        return retv;
}

static void
fail(const char *msg)
{
        printf("%s\n", msg);
        g_failed = 1;
}

static void*
entry_publisher_nonblocking_thread(void *arg)
{
        struct ring_buffer_t *buffer = (struct ring_buffer_t*)arg;
        struct cursor_t cursor;
        struct entry_t *entry;
        uint64_t reps = ENTRIES_TO_GENERATE;

        do {
        again:
                if (!publisher_next_entry_nonblocking(buffer, &cursor))
                        goto again;
                entry = ring_buffer_acquire_entry(buffer, &cursor);
                entry->content = cursor.sequence;
                publisher_commit_entry_blocking(buffer, &cursor);
        } while (--reps);

        publisher_next_entry_blocking(buffer, &cursor);
        entry = ring_buffer_acquire_entry(buffer, &cursor);
        entry->content = STOP;
        publisher_commit_entry_blocking(buffer, &cursor);
        printf("Publisher done\n");

        return NULL;
}

static void*
entry_publisher_blocking_thread(void *arg)
{
        struct ring_buffer_t *buffer = (struct ring_buffer_t*)arg;
        struct cursor_t cursor;
        struct entry_t *entry;
        uint64_t reps = ENTRIES_TO_GENERATE;

        do {
                publisher_next_entry_blocking(buffer, &cursor);
                entry = ring_buffer_acquire_entry(buffer, &cursor);
                entry->content = cursor.sequence;
                publisher_commit_entry_blocking(buffer, &cursor);
        } while (--reps);

        publisher_next_entry_blocking(buffer, &cursor);
        entry = ring_buffer_acquire_entry(buffer, &cursor);
        entry->content = STOP;
        publisher_commit_entry_blocking(buffer, &cursor);
        printf("Publisher done\n");

        return NULL;
}

static void*
entry_publisher_batch_thread(void *arg)
{
        struct ring_buffer_t *buffer = (struct ring_buffer_t*)arg;
        struct cursor_t cursor;
        uint64_t reps = ENTRIES_TO_GENERATE;

        while (reps) {
                const uint_fast64_t n = (reps >= BATCH_N) ? BATCH_N : reps;
                uint_fast64_t seq;
                uint_fast64_t first;

                publisher_next_entries_blocking(buffer, &cursor, n);
                first = cursor.sequence - n + 1;
                for (seq = first; seq <= cursor.sequence; ++seq)
                        ring_buffer_acquire_entry_at(buffer, seq)->content = seq;
                publisher_commit_entries(buffer, &cursor, n);
                reps -= n;
        }

        publisher_next_entry_blocking(buffer, &cursor);
        ring_buffer_acquire_entry(buffer, &cursor)->content = STOP;
        publisher_commit_entry_blocking(buffer, &cursor);
        printf("Batch publisher done\n");

        return NULL;
}

static void*
entry_publisher_sp_thread(void *arg)
{
        struct ring_buffer_t *buffer = (struct ring_buffer_t*)arg;
        struct cursor_t cursor;
        uint64_t reps = ENTRIES_TO_GENERATE;

        while (reps) {
                const uint_fast64_t n = (reps >= BATCH_N) ? BATCH_N : reps;
                uint_fast64_t seq;
                uint_fast64_t first;

                sp_publisher_next_entries_blocking(buffer, &cursor, n);
                first = cursor.sequence - n + 1;
                for (seq = first; seq <= cursor.sequence; ++seq)
                        ring_buffer_acquire_entry_at(buffer, seq)->content = seq;
                sp_publisher_commit_entry(buffer, &cursor);
                reps -= n;
        }

        sp_publisher_next_entry_blocking(buffer, &cursor);
        ring_buffer_acquire_entry(buffer, &cursor)->content = STOP;
        sp_publisher_commit_entry(buffer, &cursor);
        printf("SP publisher done\n");

        return NULL;
}

static void*
entry_processor_thread(void *arg)
{
        struct cursor_t n;
        struct ring_buffer_t *buffer = (struct ring_buffer_t*)arg;
        struct cursor_t cursor;
        struct cursor_t cursor_upper_limit;
        struct count_t reg_number;
        const struct entry_t *entry;

        cursor.sequence = entry_processor_barrier_register(buffer, &reg_number);
        cursor_upper_limit.sequence = cursor.sequence;

        do {
                entry_processor_barrier_wait_for_blocking(buffer, &cursor_upper_limit);
                for (n.sequence = cursor.sequence; n.sequence <= cursor_upper_limit.sequence; ++n.sequence) {
                        entry = ring_buffer_show_entry(buffer, &n);
                        if (STOP == entry->content) {
                                printf("Entry processor exiting normally\n");
                                goto out;
                        }

                        if (entry->content != n.sequence) {
                                fail("Entry processor - ERROR");
                                goto out;
                        }
                }
                entry_processor_barrier_release_entry(buffer, &reg_number, &cursor_upper_limit);

                ++cursor_upper_limit.sequence;
                cursor.sequence = cursor_upper_limit.sequence;
        } while (1);
out:
        entry_processor_barrier_unregister(buffer, &reg_number);
        printf("Entry processor done\n");

        return NULL;
}

struct counting_args {
        struct ring_buffer_t *buffer;
        uint_fast64_t seen;
};

static void*
entry_processor_counting_thread(void *arg)
{
        struct counting_args *args = arg;
        struct ring_buffer_t *buffer = args->buffer;
        struct cursor_t n;
        struct cursor_t cursor;
        struct cursor_t cursor_upper_limit;
        struct count_t reg_number;
        const struct entry_t *entry;

        args->seen = 0;
        cursor.sequence = entry_processor_barrier_register(buffer, &reg_number);
        cursor_upper_limit.sequence = cursor.sequence;

        do {
                entry_processor_barrier_wait_for_blocking(buffer, &cursor_upper_limit);
                for (n.sequence = cursor.sequence; n.sequence <= cursor_upper_limit.sequence; ++n.sequence) {
                        entry = ring_buffer_show_entry_at(buffer, n.sequence);
                        if (STOP == entry->content)
                                goto out;
                        if (entry->content != n.sequence) {
                                fail("Counting processor - ERROR");
                                goto out;
                        }
                        ++args->seen;
                }
                entry_processor_barrier_release_entry(buffer, &reg_number, &cursor_upper_limit);
                ++cursor_upper_limit.sequence;
                cursor.sequence = cursor_upper_limit.sequence;
        } while (1);
out:
        entry_processor_barrier_unregister(buffer, &reg_number);
        return NULL;
}

static int
run_mp_pair(struct ring_buffer_t *buffer,
            void *(*publisher)(void *),
            const char *label)
{
        pthread_t p_1, p_2, p_3;
        pthread_t c_1, c_2;

        g_failed = 0;
        ring_buffer_init(buffer);

        if (!create_thread(&c_1, buffer, entry_processor_thread)
            || !create_thread(&c_2, buffer, entry_processor_thread)) {
                fail("could not create entry processor thread");
                return 0;
        }
        sleep(1);
        create_thread(&p_1, buffer, publisher);
        create_thread(&p_2, buffer, publisher);
        create_thread(&p_3, buffer, publisher);

        pthread_join(p_1, NULL);
        pthread_join(p_2, NULL);
        pthread_join(p_3, NULL);
        pthread_join(c_1, NULL);
        pthread_join(c_2, NULL);
        printf("%s\n\n", label);
        return !g_failed;
}

static int
run_counted(struct ring_buffer_t *buffer,
            void *(*publisher)(void *),
            const char *label)
{
        pthread_t pub;
        pthread_t cons;
        struct counting_args args;

        g_failed = 0;
        ring_buffer_init(buffer);
        args.buffer = buffer;
        args.seen = 0;

        if (!create_thread(&cons, &args, entry_processor_counting_thread)) {
                fail("could not create counting processor");
                return 0;
        }
        sleep(1);
        create_thread(&pub, buffer, publisher);
        pthread_join(pub, NULL);
        pthread_join(cons, NULL);

        if (args.seen != ENTRIES_TO_GENERATE) {
                printf("Counting mismatch: seen=%" PRIuFAST64 " expected=%d\n",
                       args.seen, ENTRIES_TO_GENERATE);
                g_failed = 1;
        }
        printf("%s (seen=%" PRIuFAST64 ")\n\n", label, args.seen);
        return !g_failed;
}

int
main(void)
{
        struct ring_buffer_t *ring_buffer_heap;
        struct ring_buffer_t ring_buffer_stack;
        int ok = 1;

        ring_buffer_heap = ring_buffer_malloc();
        if (!ring_buffer_heap) {
                printf("Malloc ring buffer - ERROR\n");
                return EXIT_FAILURE;
        }

        ok = run_mp_pair(&ring_buffer, entry_publisher_nonblocking_thread,
                         "As-Global-Variable (non-blocking) test done") && ok;
        ok = run_mp_pair(&ring_buffer_stack, entry_publisher_blocking_thread,
                         "As-Stack-Variable (blocking) test done") && ok;
        ok = run_mp_pair(ring_buffer_heap, entry_publisher_blocking_thread,
                         "On-The-Heap (blocking) test done") && ok;

        ok = run_counted(&ring_buffer, entry_publisher_sp_thread,
                         "Single-producer batch test done") && ok;
        ok = run_counted(&ring_buffer_stack, entry_publisher_batch_thread,
                         "Multi-producer batch (1P) test done") && ok;

        {
                struct cursor_t cursor;

                ring_buffer_init(ring_buffer_heap);
                if (!publisher_next_entry_nonblocking(ring_buffer_heap, &cursor)) {
                        fail("nonblocking next on empty ring failed");
                } else {
                        ring_buffer_acquire_entry(ring_buffer_heap, &cursor)->content = cursor.sequence;
                        if (!publisher_commit_entry_nonblocking(ring_buffer_heap, &cursor))
                                fail("nonblocking commit failed");
                }
                printf("Non-blocking commit smoke test done\n");
        }

        free(ring_buffer_heap);
        return (ok && !g_failed) ? EXIT_SUCCESS : EXIT_FAILURE;
}
