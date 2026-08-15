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

#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <pthread.h>

#ifdef HAVE_CONFIG_H
    #include "ac_config.h"
#endif
#include "src/disruptor.h"

#define STOP UINT_FAST64_MAX
#define ENTRIES_TO_GENERATE (50 * 1000 * 1000 * 5)
#define ENTRY_BUFFER_SIZE (1024*2) /* must be a power of two */
#define MAX_ENTRY_PROCESSORS (1)
#define PUBLISH_BATCH (64)

DEFINE_ENTRY_TYPE(uint_fast64_t, entry_t);
DEFINE_RING_BUFFER_TYPE(MAX_ENTRY_PROCESSORS, ENTRY_BUFFER_SIZE, entry_t, ring_buffer_t);
DEFINE_ALL_RING_BUFFER_FUNCTIONS(ENTRY_BUFFER_SIZE, entry_t, ring_buffer_t);

struct ring_buffer_t ring_buffer;
struct timeval start;
struct timeval end;

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
                        if (STOP == entry->content)
                                goto out;
                }
                entry_processor_barrier_release_entry(buffer, &reg_number, &cursor_upper_limit);

                ++cursor_upper_limit.sequence;
                cursor.sequence = cursor_upper_limit.sequence;
        } while (1);
out:
        gettimeofday(&end, NULL);

        entry_processor_barrier_unregister(buffer, &reg_number);
        printf("Entry processor done\n");

        return NULL;
}

static void
publish_mp_blocking(struct ring_buffer_t *rb)
{
        struct cursor_t cursor;
        uint_fast64_t reps = ENTRIES_TO_GENERATE;

        do {
                publisher_next_entry_blocking(rb, &cursor);
                ring_buffer_acquire_entry(rb, &cursor)->content = cursor.sequence;
                publisher_commit_entry_blocking(rb, &cursor);
        } while (--reps);

        publisher_next_entry_blocking(rb, &cursor);
        ring_buffer_acquire_entry(rb, &cursor)->content = STOP;
        publisher_commit_entry_blocking(rb, &cursor);
}

static void
publish_mp_nonblocking(struct ring_buffer_t *rb)
{
        struct cursor_t cursor;
        uint_fast64_t reps = ENTRIES_TO_GENERATE;

        do {
                while (!publisher_next_entry_nonblocking(rb, &cursor))
                        ;
                ring_buffer_acquire_entry(rb, &cursor)->content = cursor.sequence;
                publisher_commit_entry_blocking(rb, &cursor);
        } while (--reps);

        publisher_next_entry_blocking(rb, &cursor);
        ring_buffer_acquire_entry(rb, &cursor)->content = STOP;
        publisher_commit_entry_blocking(rb, &cursor);
}

static void
publish_sp_batch(struct ring_buffer_t *rb)
{
        struct cursor_t cursor;
        uint_fast64_t reps = ENTRIES_TO_GENERATE;

        while (reps) {
                const uint_fast64_t n = (reps >= PUBLISH_BATCH) ? PUBLISH_BATCH : reps;
                uint_fast64_t seq;
                uint_fast64_t first;

                sp_publisher_next_entries_blocking(rb, &cursor, n);
                first = cursor.sequence - n + 1;
                for (seq = first; seq <= cursor.sequence; ++seq)
                        ring_buffer_acquire_entry_at(rb, seq)->content = seq;
                sp_publisher_commit_entry(rb, &cursor);
                reps -= n;
        }

        sp_publisher_next_entry_blocking(rb, &cursor);
        ring_buffer_acquire_entry(rb, &cursor)->content = STOP;
        sp_publisher_commit_entry(rb, &cursor);
}

static int
run_case(struct ring_buffer_t *rb,
         void (*publish)(struct ring_buffer_t *),
         const char *label,
         double *avg)
{
        pthread_t thread_id;
        double start_time;
        double end_time;
        double eps;

        ring_buffer_init(rb);
        if (!create_thread(&thread_id, rb, entry_processor_thread)) {
                printf("could not create entry processor thread\n");
                return 0;
        }

        gettimeofday(&start, NULL);
        publish(rb);
        pthread_join(thread_id, NULL);
        printf("Publisher done\n");

        start_time = (double)start.tv_sec + (double)start.tv_usec / 1000000.0;
        end_time = (double)end.tv_sec + (double)end.tv_usec / 1000000.0;
        eps = (double)ENTRIES_TO_GENERATE / (end_time - start_time);
        printf("Elapsed time = %lf seconds\n", end_time - start_time);
        printf("Entries per second %lf\n", eps);
        printf("%s\n\n", label);
        *avg += eps;
        return 1;
}

int
main(void)
{
        double avg_sp = 0.0;
        double avg_mp = 0.0;
        struct ring_buffer_t *ring_buffer_heap;
        struct ring_buffer_t ring_buffer_stack;

        ring_buffer_heap = ring_buffer_malloc();
        if (!ring_buffer_heap) {
                printf("Malloc ring buffer - ERROR\n");
                return EXIT_FAILURE;
        }

        if (!run_case(&ring_buffer, publish_sp_batch,
                      "As-Global-Variable single-producer batch test done",
                      &avg_sp)
            || !run_case(&ring_buffer, publish_mp_nonblocking,
                         "As-Global-Variable multi-producer non-blocking test done",
                         &avg_mp)
            || !run_case(&ring_buffer, publish_mp_blocking,
                         "As-Global-Variable multi-producer blocking test done",
                         &avg_mp)
            || !run_case(&ring_buffer_stack, publish_sp_batch,
                         "As-Stack-Variable single-producer batch test done",
                         &avg_sp)
            || !run_case(ring_buffer_heap, publish_sp_batch,
                         "On-The-Heap single-producer batch test done",
                         &avg_sp)
            || !run_case(ring_buffer_heap, publish_mp_blocking,
                         "On-The-Heap multi-producer blocking test done",
                         &avg_mp)) {
                free(ring_buffer_heap);
                return EXIT_FAILURE;
        }

        printf("\n\nAverage single-producer batch:     %lf entries/s\n", avg_sp / 3.0);
        printf("Average multi-producer (1P API):   %lf entries/s\n\n", avg_mp / 3.0);

        free(ring_buffer_heap);
        return EXIT_SUCCESS;
}
