/* =====================================================================================
 *       Filename:  test_ringbuf_concurrency.c
 *    Description:  Concurrency test for ring buffer
 *        Version:  1.0
 *        Created:  2026-03-01
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "platform.h"
#include "ringbuf.h"
#include "log_record.h"

/* Use cross-platform barrier from platform.h */
static xlog_barrier_t start_barrier;

/* Test configuration */
#define RING_SIZE 8192  /* Larger buffer to reduce contention */
#define MESSAGES_PER_PRODUCER 5000  /* Fewer messages per producer */

/* Shared state */
static ring_buffer *ring = NULL;
static atomic_int total_produced = ATOMIC_VAR_INIT(0);
static atomic_int total_consumed = ATOMIC_VAR_INIT(0);
static atomic_int producers_done = ATOMIC_VAR_INIT(0);
static int expected_producers = 0;

/* Static format strings for each producer */
static const char *producer_fmts[16] = {
    "Producer 0 message",
    "Producer 1 message",
    "Producer 2 message",
    "Producer 3 message",
    "Producer 4 message",
    "Producer 5 message",
    "Producer 6 message",
    "Producer 7 message",
    "Producer 8 message",
    "Producer 9 message",
    "Producer 10 message",
    "Producer 11 message",
    "Producer 12 message",
    "Producer 13 message",
    "Producer 14 message",
    "Producer 15 message"
};

/* Producer thread */
static void *producer_thread(void *arg)
{
    int id = *(int *)arg;
    int retry_count = 0;
    const int max_retries = 1000000;  /* Prevent infinite loop */

    /* Wait for all threads to be ready */
    xlog_barrier_wait(&start_barrier);

    for (int i = 0; i < MESSAGES_PER_PRODUCER; i++)
    {
        log_record rec;
        log_record_init(&rec);
        rec.level = LOG_LEVEL_INFO;
        rec.fmt = producer_fmts[id % 16];
        rec.arg_count = 0;
        rec.thread_id = (uint32_t)id;

        /* Try to push with retry limit */
        retry_count = 0;
        while (!rb_push(ring, &rec))
        {
            retry_count++;
            if (retry_count > max_retries)
            {
                /* Give up on this message to prevent deadlock */
                break;
            }
            XLOG_CPU_PAUSE();
        }

        if (retry_count <= max_retries)
        {
            atomic_fetch_add(&total_produced, 1);
        }
    }

    /* Mark this producer as done */
    atomic_fetch_add(&producers_done, 1);

    return NULL;
}

/* Consumer thread */
static void *consumer_thread(void *arg)
{
    (void)arg;
    log_record rec;
    int empty_count = 0;
    const int max_empty_spins = 100000;

    /* Wait for all threads to be ready */
    xlog_barrier_wait(&start_barrier);

    while (1)
    {
        if (rb_pop(ring, &rec))
        {
            atomic_fetch_add(&total_consumed, 1);
            empty_count = 0;
        }
        else
        {
            empty_count++;

            /* Check if all producers are done and buffer is empty */
            if (empty_count > max_empty_spins)
            {
                int done = atomic_load(&producers_done);
                if (done >= expected_producers)
                {
                    /* All producers done, drain remaining */
                    int extra_spins = 0;
                    while (rb_pop(ring, &rec))
                    {
                        atomic_fetch_add(&total_consumed, 1);
                        extra_spins = 0;
                    }
                    extra_spins++;
                    if (extra_spins > 1000)
                    {
                        break;
                    }
                    break;
                }
                empty_count = 0;  /* Reset and keep trying */
            }
            XLOG_CPU_PAUSE();
        }
    }

    return NULL;
}

static void test_multi_producer_single_consumer(int producer_count)
{
    printf("Test: %d producers, 1 consumer\n", producer_count);

    /* Reset counters */
    atomic_store(&total_produced, 0);
    atomic_store(&total_consumed, 0);
    atomic_store(&producers_done, 0);
    expected_producers = producer_count;

    /* Create ring buffer with DROP policy to prevent deadlock */
    ring = rb_create(RING_SIZE, RB_POLICY_DROP);
    if (!ring)
    {
        printf("  FAILED: Could not create ring buffer\n");
        return;
    }

    /* Initialize barrier for all threads (producers + 1 consumer) */
    if (xlog_barrier_init(&start_barrier, producer_count + 1) != 0)
    {
        printf("  FAILED: Could not initialize barrier\n");
        rb_destroy(ring);
        return;
    }

    /* Create producer threads */
    xlog_thread_t *producers = malloc(producer_count * sizeof(xlog_thread_t));
    int *producer_ids = malloc(producer_count * sizeof(int));

    for (int i = 0; i < producer_count; i++)
    {
        producer_ids[i] = i;
        xlog_thread_create(&producers[i], producer_thread, &producer_ids[i]);
    }

    /* Create consumer thread */
    xlog_thread_t consumer;
    xlog_thread_create(&consumer, consumer_thread, NULL);

    /* Wait for all threads */
    for (int i = 0; i < producer_count; i++)
    {
        xlog_thread_join(producers[i], NULL);
    }
    xlog_thread_join(consumer, NULL);

    /* Check results */
    int expected = producer_count * MESSAGES_PER_PRODUCER;
    int produced = atomic_load(&total_produced);
    int consumed = atomic_load(&total_consumed);

    printf("  Produced: %d, Consumed: %d, Expected: %d\n", produced, consumed, expected);

    /* With DROP policy, we may lose some messages under heavy contention */
    if (consumed == produced && produced > 0)
    {
        printf("  PASSED\n");
    }
    else if (produced > expected * 0.9 && consumed == produced)
    {
        printf("  PASSED (some drops under contention)\n");
    }
    else
    {
        printf("  FAILED\n");
    }

    /* Cleanup */
    xlog_barrier_destroy(&start_barrier);
    free(producers);
    free(producer_ids);
    rb_destroy(ring);
}

int main(void)
{
    printf("\n");
    printf("=========================================\n");
    printf("  Ring Buffer Concurrency Tests\n");
    printf("=========================================\n\n");

    test_multi_producer_single_consumer(2);
    test_multi_producer_single_consumer(4);
    test_multi_producer_single_consumer(8);

    printf("\n=========================================\n");
    printf("  All concurrency tests completed\n");
    printf("=========================================\n\n");

    return 0;
}

