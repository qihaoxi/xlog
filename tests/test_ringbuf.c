/* =====================================================================================
 *       Filename:  test_ringbuf.c
 *    Description:  Test suite for ringbuf module (using log_record)
 *        Version:  2.0
 *        Created:  2026-02-10
 *       Compiler:  gcc (C11)
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "ringbuf.h"
#include "log_record.h"

#define TEST_PASS "\033[32m✓\033[0m"
#define TEST_FAIL "\033[31m✗\033[0m"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  %s %s\n", TEST_PASS, msg); \
        tests_passed++; \
    } else { \
        printf("  %s %s (FAILED)\n", TEST_FAIL, msg); \
        tests_failed++; \
    } \
} while(0)

/* ============================================================================
 * Test: Basic Operations
 * ============================================================================ */
static void test_basic_operations(void) {
    printf("\n=== Test: Basic Operations ===\n");

    ring_buffer *rb = rb_create(1024, RB_POLICY_DROP);
    ASSERT(rb != NULL, "rb_create succeeded");
    ASSERT(rb_is_empty(rb), "New buffer is empty");
    ASSERT(!rb_is_full(rb), "New buffer is not full");
    ASSERT(rb_size(rb) == 0, "Size is 0");

    /* Reserve and commit a record */
    log_record *rec = rb_reserve(rb);
    ASSERT(rec != NULL, "rb_reserve succeeded");

    rec->level = XLOG_LEVEL_INFO;
    rec->timestamp_ns = 1234567890ULL;
    rec->thread_id = 42;
    rec->fmt = "Test message";

    rb_commit(rb, rec);
    ASSERT(rb_size(rb) == 1, "Size is 1 after push");
    ASSERT(!rb_is_empty(rb), "Buffer not empty after push");

    /* Peek and consume */
    log_record *peeked = rb_peek(rb);
    ASSERT(peeked != NULL, "rb_peek succeeded");
    ASSERT(peeked->level == XLOG_LEVEL_INFO, "Level matches");
    ASSERT(peeked->thread_id == 42, "Thread ID matches");

    rb_consume(rb);
    ASSERT(rb_is_empty(rb), "Buffer empty after consume");
    ASSERT(rb_size(rb) == 0, "Size is 0 after consume");

    /* Check stats */
    rb_stats stats = rb_get_stats(rb);
    ASSERT(stats.pushed == 1, "Pushed count is 1");
    ASSERT(stats.popped == 1, "Popped count is 1");
    ASSERT(stats.dropped == 0, "Dropped count is 0");

    rb_destroy(rb);
    printf("  Basic operations test completed\n");
}

/* ============================================================================
 * Test: Drop Policy
 * ============================================================================ */
static void test_drop_policy(void) {
    printf("\n=== Test: Drop Policy ===\n");

    /* Small buffer to test drop */
    ring_buffer *rb = rb_create(4, RB_POLICY_DROP);
    ASSERT(rb != NULL, "Created small buffer");

    /* Fill the buffer */
    for (int i = 0; i < 4; i++) {
        log_record *rec = rb_reserve(rb);
        ASSERT(rec != NULL, "Reserve succeeded");
        rec->level = XLOG_LEVEL_DEBUG;
        rec->thread_id = i;
        rb_commit(rb, rec);
    }

    ASSERT(rb_is_full(rb), "Buffer is full");
    ASSERT(rb_size(rb) == 4, "Size is 4");

    /* Try to push more - should be dropped */
    log_record *rec = rb_reserve(rb);
    ASSERT(rec == NULL, "Reserve returns NULL when full (DROP policy)");

    rb_stats stats = rb_get_stats(rb);
    ASSERT(stats.dropped == 1, "One record dropped");

    /* Consume one and try again */
    rb_consume(rb);
    rec = rb_reserve(rb);
    ASSERT(rec != NULL, "Reserve succeeded after consume");
    rb_commit(rb, rec);

    rb_destroy(rb);
    printf("  Drop policy test completed\n");
}

/* ============================================================================
 * Test: Pop Operation
 * ============================================================================ */
static void test_pop_operation(void) {
    printf("\n=== Test: Pop Operation ===\n");

    ring_buffer *rb = rb_create(16, RB_POLICY_DROP);

    /* Push some records */
    for (int i = 0; i < 5; i++) {
        log_record *rec = rb_reserve(rb);
        rec->level = XLOG_LEVEL_INFO;
        rec->thread_id = i * 10;
        rec->fmt = "Message";
        rb_commit(rb, rec);
    }

    ASSERT(rb_size(rb) == 5, "Size is 5");

    /* Pop all records */
    log_record out;
    int count = 0;
    while (rb_pop(rb, &out)) {
        ASSERT(out.thread_id == count * 10, "Thread ID matches order");
        count++;
    }

    ASSERT(count == 5, "Popped 5 records");
    ASSERT(rb_is_empty(rb), "Buffer empty after pop all");

    /* Pop from empty buffer */
    bool result = rb_pop(rb, &out);
    ASSERT(!result, "Pop from empty returns false");

    rb_destroy(rb);
    printf("  Pop operation test completed\n");
}

/* ============================================================================
 * Test: Concurrent Access
 * ============================================================================ */
#define NUM_PRODUCERS 4
#define MESSAGES_PER_PRODUCER 1000

static ring_buffer *g_rb;
static atomic_int g_producer_done = 0;

static void *producer_thread(void *arg) {
    int id = *(int *)arg;

    for (int i = 0; i < MESSAGES_PER_PRODUCER; i++) {
        log_record *rec = rb_reserve(g_rb);
        if (rec) {
            rec->level = XLOG_LEVEL_INFO;
            rec->thread_id = id;
            rec->timestamp_ns = i;
            rb_commit(g_rb, rec);
        }
    }

    atomic_fetch_add(&g_producer_done, 1);
    return NULL;
}

static void test_concurrent_access(void) {
    printf("\n=== Test: Concurrent Access ===\n");

    g_rb = rb_create(8192, RB_POLICY_DROP);
    ASSERT(g_rb != NULL, "Created buffer for concurrent test");

    atomic_store(&g_producer_done, 0);

    xlog_thread_t producers[NUM_PRODUCERS];
    int ids[NUM_PRODUCERS];

    /* Start producers */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        ids[i] = i;
        xlog_thread_create(&producers[i], producer_thread, &ids[i]);
    }

    /* Consumer: drain the queue using zero-copy peek+consume */
    int consumed = 0;
    while (atomic_load(&g_producer_done) < NUM_PRODUCERS || !rb_is_empty(g_rb)) {
        log_record *rec = rb_peek(g_rb);
        if (rec) {
            rb_consume(g_rb);
            consumed++;
        } else {
            xlog_sleep_us(10);
        }
    }

    /* Wait for all producers */
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        xlog_thread_join(producers[i], NULL);
    }

    rb_stats stats = rb_get_stats(g_rb);
    int total_expected = NUM_PRODUCERS * MESSAGES_PER_PRODUCER;
    int total_processed = stats.pushed;
    int total_dropped = stats.dropped;

    printf("  Produced: %d, Consumed: %d, Dropped: %lu\n",
           total_expected, consumed, (unsigned long)total_dropped);

    ASSERT(total_processed + total_dropped == total_expected,
           "All messages accounted for");
    ASSERT(rb_is_empty(g_rb), "Buffer empty after test");

    rb_destroy(g_rb);
    printf("  Concurrent access test completed\n");
}

/* ============================================================================
 * Test: Push Copy
 * ============================================================================ */
static void test_push_copy(void) {
    printf("\n=== Test: Push Copy ===\n");

    ring_buffer *rb = rb_create(16, RB_POLICY_DROP);

    /* Create a local record */
    log_record local;
    log_record_init(&local);
    local.level = XLOG_LEVEL_ERROR;
    local.thread_id = 999;
    local.timestamp_ns = 12345678900ULL;
    local.fmt = "Error message";
    log_record_commit(&local);

    /* Push copy */
    bool ok = rb_push(rb, &local);
    ASSERT(ok, "rb_push succeeded");

    /* Pop and verify */
    log_record out;
    ok = rb_pop(rb, &out);
    ASSERT(ok, "rb_pop succeeded");
    ASSERT(out.level == XLOG_LEVEL_ERROR, "Level matches");
    ASSERT(out.thread_id == 999, "Thread ID matches");
    ASSERT(out.timestamp_ns == 12345678900ULL, "Timestamp matches");

    rb_destroy(rb);
    printf("  Push copy test completed\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */
int main(void) {
    printf("============================================\n");
    printf("   Ring Buffer Test Suite (log_record)\n");
    printf("============================================\n");

    test_basic_operations();
    test_drop_policy();
    test_pop_operation();
    test_push_copy();
    test_concurrent_access();

    printf("\n============================================\n");
    printf("   Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("============================================\n");

    return tests_failed > 0 ? 1 : 0;
}

