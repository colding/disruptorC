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

#ifdef HAVE_CONFIG_H
    #include "ac_config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/time.h>
#include <pthread.h>

#define YIELD() sched_yield()
#include "src/disruptor.h"

#define STOP UINT64_MAX
#define EVENTS_TO_GENERATE (400)
#define EVENT_BUFFER_SIZE (16)
#define MAX_EVENT_PROCESSORS (2)

DEFINE_EVENT_TYPE(uint64_t, event_t);
DEFINE_RING_BUFFER_TYPE(MAX_EVENT_PROCESSORS, EVENT_BUFFER_SIZE, event_t, ring_buffer_t);
DEFINE_RING_BUFFER_INIT(MAX_EVENT_PROCESSORS, EVENT_BUFFER_SIZE, ring_buffer_t);
DEFINE_EVENT_PROCESSOR_BARRIER_REGISTER_FUNCTION(ring_buffer_t);
DEFINE_EVENT_PROCESSOR_BARRIER_UNREGISTER_FUNCTION(ring_buffer_t);
DEFINE_EVENT_PROCESSOR_BARRIER_WAITFOR_FUNCTION(ring_buffer_t);
DEFINE_EVENT_PROCESSOR_BARRIER_GETENTRY_FUNCTION(event_t, ring_buffer_t);
DEFINE_EVENT_PUBLISHERPORT_NEXTENTRY_FUNCTION(ring_buffer_t);
DEFINE_EVENT_PUBLISHERPORT_COMMITENTRY_BLOCKING_FUNCTION(ring_buffer_t);

ring_buffer_t ring_buffer;

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
event_publisher_thread(void *arg)
{
        ring_buffer_t *buffer = (ring_buffer_t*)arg;
        cursor_t cursor;
        uint64_t reps = EVENTS_TO_GENERATE;

        do {
                publisher_port_nextEntry(buffer, &cursor);
                buffer->buffer[get_index(buffer->reduced_size.count, &cursor)].content = cursor.sequence;
                publisher_port_commitEntry_blocking(buffer, &cursor);
        } while (--reps);

        publisher_port_nextEntry(buffer, &cursor);
        buffer->buffer[get_index(buffer->reduced_size.count, &cursor)].content = STOP;
        publisher_port_commitEntry_blocking(buffer, &cursor);
        printf("Publisher done\n");

        return NULL;
}

static void*
event_processor_thread(void *arg)
{
        cursor_t n;
        ring_buffer_t *buffer = (ring_buffer_t*)arg;
        cursor_t cursor;
        cursor_t cursor_upper_limit;
        count_t reg_number;
        const event_t *event;

        // register and setup event processor
        cursor.sequence = 0;
        event_processor_barrier_register(buffer, &reg_number);

        // initialize event processing
        cursor.sequence = buffer->event_processor_cursors[reg_number.count].sequence;
        if (!cursor.sequence)
                cursor.sequence = 1;
        cursor_upper_limit.sequence = cursor.sequence;

        do {
                event_processor_barrier_waitFor(buffer, &cursor_upper_limit);
                for (n.sequence = cursor.sequence; n.sequence <= cursor_upper_limit.sequence; ++n.sequence) { // batching

                        event = event_processor_barrier_getEntry(buffer, &n);
                        if (STOP == event->content) {
                                printf("Event processor exiting normally\n");
                                goto out;
                        }

                        if (event->content != n.sequence) {
                                printf("Event processor - ERROR\n");
                                goto out;
                        }
                }
                if (!__sync_bool_compare_and_swap(&(buffer->event_processor_cursors[reg_number.count].sequence), buffer->event_processor_cursors[reg_number.count].sequence, cursor_upper_limit.sequence)) {
                        printf("Event processor could not set cursor - ERROR\n");
                        goto out;
                }

                ++cursor_upper_limit.sequence;
                cursor.sequence = cursor_upper_limit.sequence;
        } while (1);
out:
        event_processor_barrier_unregister(buffer, &reg_number);
        printf("Event processor done\n");

        return NULL;
}

int
main(int argc, char *argv[])
{
        pthread_t p_1; // event publisher
        pthread_t p_2;
        pthread_t p_3;
        pthread_t c_1; // consumer/event processor
        pthread_t c_2;

        ring_buffer_init(&ring_buffer);

        create_thread(&c_1, &ring_buffer, event_processor_thread);
        create_thread(&c_2, &ring_buffer, event_processor_thread);
        sleep(1);
        create_thread(&p_1, &ring_buffer, event_publisher_thread);
        create_thread(&p_2, &ring_buffer, event_publisher_thread);
        create_thread(&p_3, &ring_buffer, event_publisher_thread);

        // join event publishers
        pthread_join(p_1, NULL);
        pthread_join(p_2, NULL);
        pthread_join(p_3, NULL);

        // join event processors
        pthread_join(c_1, NULL);
        pthread_join(c_2, NULL);

        return 0;
}
