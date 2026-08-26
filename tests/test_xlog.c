/* =====================================================================================
 *       Filename:  test_xlog.c
 *    Description:  Test cases for xlog main API
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include "xlog.h"
#include "xlog_core.h"
#include "console_sink.h"
#include "file_sink.h"
#define TEST_LOG_FILE "/tmp/test_xlog.log"
static void test_basic_init_shutdown(void) {
    printf("\n=== Test 1: Basic init/shutdown ===\n");
    if (!xlog_init()) {
        printf("FAILED: xlog_init() returned false\n");
        return;
    }
    printf("✓ xlog_init() succeeded\n");
    if (!xlog_is_initialized()) {
        printf("FAILED: xlog_is_initialized() returned false\n");
        xlog_shutdown();
        return;
    }
    printf("✓ xlog_is_initialized() returned true\n");
    xlog_shutdown();
    if (xlog_is_initialized()) {
        printf("FAILED: xlog_is_initialized() should be false after shutdown\n");
        return;
    }
    printf("✓ xlog_shutdown() succeeded\n");
}
static void test_console_sink(void) {
    printf("\n=== Test 2: Console sink logging ===\n");
    if (!xlog_init()) {
        printf("FAILED: xlog_init() failed\n");
        return;
    }
    sink_t *console = console_sink_create_stdout(XLOG_LEVEL_DEBUG);
    if (!console) {
        printf("FAILED: console_sink_create_stdout() failed\n");
        xlog_shutdown();
        return;
    }
    if (!xlog_add_sink(console)) {
        printf("FAILED: xlog_add_sink() failed\n");
        sink_destroy(console);
        xlog_shutdown();
        return;
    }
    printf("✓ Console sink added\n");
    printf("Output from XLOG macros:\n");
    XLOG_DEBUG("Debug message: value=%d", 42);
    XLOG_INFO("Info message: hello %s", "world");
    XLOG_WARN("Warning message");
    XLOG_ERROR("Error message: code=%d", -1);
    xlog_flush();
    printf("✓ Logs flushed\n");
    xlog_stats stats;
    xlog_get_stats(&stats);
    printf("Stats: logged=%lu, processed=%lu, dropped=%lu\n",
           stats.logged, stats.processed, stats.dropped);
    xlog_shutdown();
    printf("✓ Test completed\n");
}
static void test_file_sink(void) {
    printf("\n=== Test 3: File sink logging ===\n");
    /* Clean up old test file */
    unlink(TEST_LOG_FILE);
    if (!xlog_init()) {
        printf("FAILED: xlog_init() failed\n");
        return;
    }
    sink_t *file = file_sink_create_simple(TEST_LOG_FILE, XLOG_LEVEL_DEBUG);
    if (!file) {
        printf("FAILED: file_sink_create_simple() failed\n");
        xlog_shutdown();
        return;
    }
    if (!xlog_add_sink(file)) {
        printf("FAILED: xlog_add_sink() for file failed\n");
        sink_destroy(file);
        xlog_shutdown();
        return;
    }
    printf("✓ File sink added: %s\n", TEST_LOG_FILE);
    XLOG_INFO("Test file logging: value=%d", 123);
    XLOG_DEBUG("Another message: str=%s", "test");
    xlog_flush();
    xlog_shutdown();
    /* Verify file content */
    FILE *fp = fopen(TEST_LOG_FILE, "r");
    if (!fp) {
        printf("FAILED: Could not open log file for verification\n");
        return;
    }
    char buffer[1024];
    printf("File content:\n");
    while (fgets(buffer, sizeof(buffer), fp)) {
        printf("  %s", buffer);
    }
    fclose(fp);
    printf("✓ Test completed\n");
}
static void test_log_levels(void) {
    printf("\n=== Test 4: Log level filtering ===\n");
    if (!xlog_init()) {
        printf("FAILED: xlog_init() failed\n");
        return;
    }
    sink_t *console = console_sink_create_stdout(XLOG_LEVEL_TRACE);
    xlog_add_sink(console);
    printf("Setting level to WARNING...\n");
    xlog_set_level(XLOG_LEVEL_WARNING);
    printf("The following DEBUG and INFO should NOT appear:\n");
    XLOG_DEBUG("This DEBUG should be filtered");
    XLOG_INFO("This INFO should be filtered");
    printf("The following WARNING and ERROR should appear:\n");
    XLOG_WARN("This WARNING should appear");
    XLOG_ERROR("This ERROR should appear");
    xlog_flush();
    xlog_stats stats;
    xlog_get_stats(&stats);
    printf("Stats: logged=%lu (should be 2)\n", stats.logged);
    xlog_shutdown();
    printf("✓ Test completed\n");
}
static void *logging_thread(void *arg) {
    int thread_num = *(int *)arg;
    for (int i = 0; i < 100; i++) {
        XLOG_INFO("Thread %d message %d", thread_num, i);
    }
    return NULL;
}
static void test_multithread(void) {
    printf("\n=== Test 5: Multi-threaded logging ===\n");
    if (!xlog_init()) {
        printf("FAILED: xlog_init() failed\n");
        return;
    }
    sink_t *console = console_sink_create_stdout(XLOG_LEVEL_INFO);
    xlog_add_sink(console);
    xlog_set_level(XLOG_LEVEL_INFO);
    const int num_threads = 4;
    pthread_t threads[num_threads];
    int thread_ids[num_threads];
    printf("Starting %d threads, each logging 100 messages...\n", num_threads);
    for (int i = 0; i < num_threads; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, logging_thread, &thread_ids[i]);
    }
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    xlog_flush();
    xlog_stats stats;
    xlog_get_stats(&stats);
    printf("Stats: logged=%lu, processed=%lu, dropped=%lu\n",
           stats.logged, stats.processed, stats.dropped);
    printf("Expected: %d messages\n", num_threads * 100);
    xlog_shutdown();
    printf("✓ Test completed\n");
}
static void test_sync_mode(void) {
    printf("\n=== Test 6: Synchronous logging mode ===\n");
    xlog_config config = {
        .queue_capacity = 1024,
        .format_buffer_size = 4096,
        .min_level = XLOG_LEVEL_DEBUG,
        .async = false,  /* Sync mode */
        .auto_flush = true,
        .batch_size = 64,
        .flush_interval_ms = 1000
    };
    if (!xlog_init_with_config(&config)) {
        printf("FAILED: xlog_init_with_config() failed\n");
        return;
    }
    sink_t *console = console_sink_create_stdout(XLOG_LEVEL_DEBUG);
    xlog_add_sink(console);
    printf("Logging in sync mode (should be immediate):\n");
    XLOG_INFO("Sync message 1");
    XLOG_INFO("Sync message 2");
    XLOG_INFO("Sync message 3");
    xlog_stats stats;
    xlog_get_stats(&stats);
    printf("Stats: logged=%lu, processed=%lu (should match in sync mode)\n",
           stats.logged, stats.processed);
    xlog_shutdown();
    printf("✓ Test completed\n");
}

/* --- Test 7: ambient trace context (thread-local, MDC-style) --- */
typedef struct {
    char buf[8192];
    size_t len;
} capture_ctx;

static void capture_write(sink_t *sink, const char *data, size_t len) {
    capture_ctx *c = (capture_ctx *) sink->ctx;
    size_t room = sizeof(c->buf) - c->len - 1;
    if (len > room) len = room;
    memcpy(c->buf + c->len, data, len);
    c->len += len;
    c->buf[c->len] = '\0';
}

static void capture_flush(sink_t *sink) { (void) sink; }

static void test_trace_context_tls(void) {
    printf("\n=== Test 7: Ambient trace context (TLS) ===\n");

    /* API is usable before init (pure thread-local state) */
    xlog_trace_set(0x1122334455667788ULL, 0x99aabbccddeeff00ULL,
                   0x0102030405060708ULL, 0x0f0e0d0c0b0a0908ULL);
    uint64_t hi, lo, sp, pa;
    assert(xlog_trace_get(&hi, &lo, &sp, &pa));
    assert(hi == 0x1122334455667788ULL && lo == 0x99aabbccddeeff00ULL);
    assert(sp == 0x0102030405060708ULL && pa == 0x0f0e0d0c0b0a0908ULL);
    xlog_trace_clear();
    assert(!xlog_trace_get(NULL, NULL, NULL, NULL));

    /* Stamping through the full pipeline: sync mode + JSON + capture sink */
    xlog_config config = {
        .queue_capacity = 1024,
        .format_buffer_size = 8192,
        .min_level = XLOG_LEVEL_DEBUG,
        .async = false,
        .auto_flush = true,
        .batch_size = 64,
        .flush_interval_ms = 1000,
        .format_style = XLOG_OUTPUT_JSON
    };
    if (!xlog_init_with_config(&config)) {
        printf("FAILED: xlog_init_with_config() failed\n");
        return;
    }
    capture_ctx cap;
    memset(&cap, 0, sizeof(cap));
    sink_t *sink = sink_create(&cap, capture_write, capture_flush, NULL,
                               XLOG_LEVEL_DEBUG, SINK_TYPE_CUSTOM);
    xlog_add_sink(sink);

    /* Without context: no trace keys in output */
    XLOG_INFO("no trace");
    assert(strstr(cap.buf, "\"trace_id\"") == NULL);

    /* With context: 32-hex trace_id (hi printed first) + span + parent */
    xlog_trace_set(0x1122334455667788ULL, 0x99aabbccddeeff00ULL,
                   0x0102030405060708ULL, 0x0f0e0d0c0b0a0908ULL);
    XLOG_INFO("with trace");
    assert(strstr(cap.buf, "\"trace_id\":\"112233445566778899aabbccddeeff00\"") != NULL);
    assert(strstr(cap.buf, "\"span_id\":\"0102030405060708\"") != NULL);
    assert(strstr(cap.buf, "\"parent_span_id\":\"0f0e0d0c0b0a0908\"") != NULL);

    /* Clear: keys disappear from subsequent records (check only the new tail) */
    xlog_trace_clear();
    size_t len_before_clear = cap.len;
    XLOG_INFO("cleared");
    assert(strstr(cap.buf + len_before_clear, "\"trace_id\"") == NULL);

    /* Sink ownership passed to the manager on add; shutdown frees it */
    xlog_shutdown();
    printf("Captured JSON sample: %.160s...\n", cap.buf);
    printf("✓ Test completed\n");
}
int main(void) {
    printf("===========================================\n");
    printf("   xlog API Test Suite\n");
    printf("===========================================\n");
    test_basic_init_shutdown();
    test_console_sink();
    test_file_sink();
    test_log_levels();
    test_multithread();
    test_sync_mode();
    test_trace_context_tls();
    printf("\n===========================================\n");
    printf("   All tests completed!\n");
    printf("===========================================\n");
    return 0;
}
