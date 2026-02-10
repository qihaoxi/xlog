/* =====================================================================================
 *       Filename:  test_simd.c
 *    Description:  Test SIMD operations and batch writer
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "simd.h"
#include "batch_writer.h"

/* ============================================================================
 * Test Helpers
 * ============================================================================ */

#define TEST_PASS(name) printf("  ✓ %s\n", name)
#define TEST_FAIL(name, msg) printf("  ✗ %s: %s\n", name, msg)

static uint64_t get_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* ============================================================================
 * SIMD Tests
 * ============================================================================ */

static void test_cpu_features(void) {
    printf("\n=== CPU Feature Detection ===\n");

    const xlog_cpu_features *features = xlog_get_cpu_features();

    printf("  SSE2:  %s\n", features->sse2 ? "yes" : "no");
    printf("  SSE4.2: %s\n", features->sse42 ? "yes" : "no");
    printf("  AVX2:  %s\n", features->avx2 ? "yes" : "no");
    printf("  NEON:  %s\n", features->neon ? "yes" : "no");

    TEST_PASS("CPU feature detection");
}

static void test_simd_strlen(void) {
    printf("\n=== SIMD strlen ===\n");

    const char *test_strings[] = {
        "",
        "a",
        "hello",
        "hello world",
        "this is a longer string for testing",
        "0123456789012345",  /* 16 chars */
        "01234567890123456", /* 17 chars */
        "The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog."
    };

    for (size_t i = 0; i < sizeof(test_strings) / sizeof(test_strings[0]); i++) {
        size_t expected = strlen(test_strings[i]);
        size_t actual = xlog_simd_strlen(test_strings[i]);

        if (actual != expected) {
            char msg[100];
            snprintf(msg, sizeof(msg), "expected %zu, got %zu for \"%s\"",
                     expected, actual, test_strings[i]);
            TEST_FAIL("strlen", msg);
            return;
        }
    }

    TEST_PASS("strlen correctness");

#if defined(__linux__) || defined(__APPLE__)
    printf("  Note: On Linux/macOS, xlog_simd_strlen delegates to glibc/libSystem\n");
    printf("        which is already highly optimized (AVX2/AVX-512).\n");
#else
    /* Performance comparison only meaningful on Windows */
    const char *long_str = "The quick brown fox jumps over the lazy dog. "
                           "Pack my box with five dozen liquor jugs. "
                           "How vexingly quick daft zebras jump! "
                           "The five boxing wizards jump quickly.";

    int iterations = 1000000;

    uint64_t start = get_ns();
    volatile size_t len1 = 0;
    for (int i = 0; i < iterations; i++) {
        len1 = strlen(long_str);
    }
    uint64_t standard_time = get_ns() - start;

    start = get_ns();
    volatile size_t len2 = 0;
    for (int i = 0; i < iterations; i++) {
        len2 = xlog_simd_strlen(long_str);
    }
    uint64_t simd_time = get_ns() - start;

    printf("  Standard strlen: %lu ns total, %.2f ns/call\n",
           standard_time, (double)standard_time / iterations);
    printf("  SIMD strlen:     %lu ns total, %.2f ns/call\n",
           simd_time, (double)simd_time / iterations);
    printf("  Speedup: %.2fx\n", (double)standard_time / simd_time);

    (void)len1; (void)len2;
#endif
}

static void test_simd_memcpy(void) {
    printf("\n=== SIMD memcpy ===\n");

    size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024, 4096, 65536};

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        size_t size = sizes[i];
        char *src = malloc(size);
        char *dst1 = malloc(size);
        char *dst2 = malloc(size);

        /* Fill source with pattern */
        for (size_t j = 0; j < size; j++) {
            src[j] = (char)(j & 0xFF);
        }

        memcpy(dst1, src, size);
        xlog_simd_memcpy(dst2, src, size);

        if (memcmp(dst1, dst2, size) != 0) {
            char msg[100];
            snprintf(msg, sizeof(msg), "mismatch at size %zu", size);
            TEST_FAIL("memcpy", msg);
            free(src); free(dst1); free(dst2);
            return;
        }

        free(src);
        free(dst1);
        free(dst2);
    }

    TEST_PASS("memcpy correctness");

#if defined(__linux__) || defined(__APPLE__)
    printf("  Note: On Linux/macOS, xlog_simd_memcpy delegates to glibc/libSystem\n");
    printf("        which uses AVX2/AVX-512 optimized assembly.\n");
#else
    /* Performance test only on Windows */
    size_t test_size = 4096;
    int iterations = 100000;

    char *src = aligned_alloc(64, test_size);
    char *dst = aligned_alloc(64, test_size);
    memset(src, 'A', test_size);

    uint64_t start = get_ns();
    for (int i = 0; i < iterations; i++) {
        memcpy(dst, src, test_size);
    }
    uint64_t standard_time = get_ns() - start;

    start = get_ns();
    for (int i = 0; i < iterations; i++) {
        xlog_simd_memcpy(dst, src, test_size);
    }
    uint64_t simd_time = get_ns() - start;

    printf("  Standard memcpy: %lu ns total (4KB x %d)\n", standard_time, iterations);
    printf("  SIMD memcpy:     %lu ns total\n", simd_time);
    printf("  Throughput: %.2f GB/s (standard), %.2f GB/s (SIMD)\n",
           (double)test_size * iterations / standard_time,
           (double)test_size * iterations / simd_time);

    free(src);
    free(dst);
#endif
}

static void test_simd_itoa(void) {
    printf("\n=== SIMD Integer to String ===\n");

    struct {
        uint64_t value;
        const char *expected;
    } test_cases[] = {
        {0, "0"},
        {1, "1"},
        {9, "9"},
        {10, "10"},
        {99, "99"},
        {100, "100"},
        {999, "999"},
        {1000, "1000"},
        {12345, "12345"},
        {123456789, "123456789"},
        {UINT64_MAX, "18446744073709551615"}
    };

    char buffer[32];

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        xlog_simd_u64toa(test_cases[i].value, buffer);
        if (strcmp(buffer, test_cases[i].expected) != 0) {
            char msg[100];
            snprintf(msg, sizeof(msg), "expected \"%s\", got \"%s\" for %lu",
                     test_cases[i].expected, buffer, test_cases[i].value);
            TEST_FAIL("u64toa", msg);
            return;
        }
    }

    TEST_PASS("u64toa correctness");

    /* Test i64toa */
    struct {
        int64_t value;
        const char *expected;
    } signed_cases[] = {
        {0, "0"},
        {-1, "-1"},
        {-12345, "-12345"},
        {INT64_MIN, "-9223372036854775808"}
    };

    for (size_t i = 0; i < sizeof(signed_cases) / sizeof(signed_cases[0]); i++) {
        xlog_simd_i64toa(signed_cases[i].value, buffer);
        if (strcmp(buffer, signed_cases[i].expected) != 0) {
            char msg[100];
            snprintf(msg, sizeof(msg), "expected \"%s\", got \"%s\"",
                     signed_cases[i].expected, buffer);
            TEST_FAIL("i64toa", msg);
            return;
        }
    }

    TEST_PASS("i64toa correctness");

    /* Performance comparison */
    int iterations = 1000000;
    uint64_t test_value = 123456789012345ULL;

    uint64_t start = get_ns();
    for (int i = 0; i < iterations; i++) {
        snprintf(buffer, sizeof(buffer), "%lu", test_value);
    }
    uint64_t sprintf_time = get_ns() - start;

    start = get_ns();
    for (int i = 0; i < iterations; i++) {
        xlog_simd_u64toa(test_value, buffer);
    }
    uint64_t simd_time = get_ns() - start;

    printf("  sprintf: %lu ns total, %.2f ns/call\n", sprintf_time, (double)sprintf_time / iterations);
    printf("  SIMD:    %lu ns total, %.2f ns/call\n", simd_time, (double)simd_time / iterations);
    printf("  Speedup: %.2fx\n", (double)sprintf_time / simd_time);
}

static void test_simd_datetime(void) {
    printf("\n=== SIMD Datetime Formatting ===\n");

    char buffer[32];

    int len = xlog_simd_format_datetime(2026, 2, 9, 14, 30, 45, buffer);

    if (len != 19 || strcmp(buffer, "2026-02-09 14:30:45") != 0) {
        char msg[100];
        snprintf(msg, sizeof(msg), "expected \"2026-02-09 14:30:45\", got \"%s\"", buffer);
        TEST_FAIL("datetime format", msg);
        return;
    }

    TEST_PASS("datetime format correctness");

    /* Test usec formatting */
    char usec_buf[16];
    xlog_simd_format_usec(123456, usec_buf);

    if (strcmp(usec_buf, ".123456") != 0) {
        char msg[100];
        snprintf(msg, sizeof(msg), "expected \".123456\", got \"%s\"", usec_buf);
        TEST_FAIL("usec format", msg);
        return;
    }

    TEST_PASS("usec format correctness");

    /* Performance */
    int iterations = 1000000;

    uint64_t start = get_ns();
    for (int i = 0; i < iterations; i++) {
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                 2026, 2, 9, 14, 30, 45);
    }
    uint64_t sprintf_time = get_ns() - start;

    start = get_ns();
    for (int i = 0; i < iterations; i++) {
        xlog_simd_format_datetime(2026, 2, 9, 14, 30, 45, buffer);
    }
    uint64_t simd_time = get_ns() - start;

    printf("  sprintf: %lu ns total, %.2f ns/call\n", sprintf_time, (double)sprintf_time / iterations);
    printf("  SIMD:    %lu ns total, %.2f ns/call\n", simd_time, (double)simd_time / iterations);
    printf("  Speedup: %.2fx\n", (double)sprintf_time / simd_time);
}

/* ============================================================================
 * Batch Writer Tests
 * ============================================================================ */

static void test_batch_writer_basic(void) {
    printf("\n=== Batch Writer Basic ===\n");

    const char *test_file = "/tmp/test_batch_writer.log";
    FILE *fp = fopen(test_file, "wb");
    if (!fp) {
        TEST_FAIL("batch writer", "failed to open test file");
        return;
    }

    batch_writer *writer = batch_writer_create_default(fp);
    if (!writer) {
        TEST_FAIL("batch writer", "failed to create writer");
        fclose(fp);
        return;
    }

    /* Write some data */
    const char *lines[] = {
        "Line 1: Hello, World!\n",
        "Line 2: This is a test.\n",
        "Line 3: Batch writing is efficient.\n"
    };

    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        ssize_t written = batch_writer_write(writer, lines[i], strlen(lines[i]));
        if (written < 0) {
            TEST_FAIL("batch writer", "write failed");
            batch_writer_destroy(writer);
            fclose(fp);
            return;
        }
    }

    /* Test printf */
    int pret = batch_writer_printf(writer, "Formatted: %d + %d = %d\n", 1, 2, 3);
    if (pret < 0) {
        TEST_FAIL("batch writer", "printf failed");
        batch_writer_destroy(writer);
        fclose(fp);
        return;
    }

    /* Get stats before destroy */
    batch_writer_stats stats;
    batch_writer_get_stats(writer, &stats);

    batch_writer_destroy(writer);
    fclose(fp);

    /* Verify file contents */
    fp = fopen(test_file, "rb");
    if (!fp) {
        TEST_FAIL("batch writer", "failed to reopen test file");
        return;
    }

    char content[1024];
    size_t read_len = fread(content, 1, sizeof(content) - 1, fp);
    content[read_len] = '\0';
    fclose(fp);

    if (strstr(content, "Hello, World!") == NULL ||
        strstr(content, "Batch writing") == NULL ||
        strstr(content, "1 + 2 = 3") == NULL) {
        TEST_FAIL("batch writer", "content verification failed");
        return;
    }

    TEST_PASS("basic write operations");

    printf("  Entries written: %lu\n", stats.entries_written);
    printf("  Bytes written: %lu\n", stats.bytes_written);
    printf("  Flush count: %lu\n", stats.flush_count);

    remove(test_file);
}

static void test_batch_writer_performance(void) {
    printf("\n=== Batch Writer Performance ===\n");

    const char *test_file = "/tmp/test_batch_perf.log";

    const char *log_line = "2026-02-09 14:30:45.123456 [INFO] [thread-12345] This is a sample log message with some data: 12345\n";
    size_t line_len = strlen(log_line);
    int iterations = 100000;

    printf("  Test: %d log entries, each %zu bytes\n\n", iterations, line_len);

    /* ============================================
     * Test 1: Batch writer with optimal config
     * ============================================ */
    FILE *fp = fopen(test_file, "wb");
    /* Disable stdio buffering to see raw performance */
    setvbuf(fp, NULL, _IONBF, 0);

    batch_writer_config config = {
        .buffer_size = 32 * 1024,  /* 32KB buffer */
        .flush_threshold = 0.9,
        .max_pending = 1000,       /* Flush less frequently */
        .flush_timeout_ms = 0,     /* Disable timeout flush */
        .use_direct_io = false,
        .use_write_combine = true
    };
    batch_writer *writer = batch_writer_create(fp, &config);

    uint64_t start = get_ns();
    for (int i = 0; i < iterations; i++) {
        batch_writer_write(writer, log_line, line_len);
    }
    batch_writer_flush(writer);
    uint64_t batch_time = get_ns() - start;

    batch_writer_stats stats;
    batch_writer_get_stats(writer, &stats);

    batch_writer_destroy(writer);
    fclose(fp);
    remove(test_file);

    /* ============================================
     * Test 2: Direct write() syscall (no buffering)
     * ============================================ */
    int fd = open(test_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    start = get_ns();
    for (int i = 0; i < iterations; i++) {
        write(fd, log_line, line_len);
    }
    fsync(fd);
    uint64_t direct_syscall_time = get_ns() - start;

    close(fd);
    remove(test_file);

    /* ============================================
     * Test 3: fwrite with stdio buffering (default)
     * ============================================ */
    fp = fopen(test_file, "wb");
    /* Default stdio buffering (~4KB-8KB) */

    start = get_ns();
    for (int i = 0; i < iterations; i++) {
        fwrite(log_line, 1, line_len, fp);
    }
    fflush(fp);
    uint64_t fwrite_buffered_time = get_ns() - start;

    fclose(fp);
    remove(test_file);

    /* ============================================
     * Test 4: fwrite without stdio buffering
     * ============================================ */
    fp = fopen(test_file, "wb");
    setvbuf(fp, NULL, _IONBF, 0);  /* Disable buffering */

    start = get_ns();
    for (int i = 0; i < iterations; i++) {
        fwrite(log_line, 1, line_len, fp);
    }
    fflush(fp);
    uint64_t fwrite_unbuffered_time = get_ns() - start;

    fclose(fp);
    remove(test_file);

    /* ============================================
     * Results
     * ============================================ */
    double total_bytes = (double)line_len * iterations;

    printf("  Results (lower time = better):\n");
    printf("  ┌─────────────────────────────────┬────────────┬────────────┬─────────┐\n");
    printf("  │ Method                          │ Time (ms)  │ Throughput │ Syscalls│\n");
    printf("  ├─────────────────────────────────┼────────────┼────────────┼─────────┤\n");
    printf("  │ Batch Writer (32KB buf)         │ %8.2f   │ %6.0f MB/s │ %7lu │\n",
           batch_time / 1e6, total_bytes / batch_time * 1000, stats.flush_count);
    printf("  │ Direct write() syscall          │ %8.2f   │ %6.0f MB/s │ %7d │\n",
           direct_syscall_time / 1e6, total_bytes / direct_syscall_time * 1000, iterations);
    printf("  │ fwrite (with stdio buffer)      │ %8.2f   │ %6.0f MB/s │   ~few  │\n",
           fwrite_buffered_time / 1e6, total_bytes / fwrite_buffered_time * 1000);
    printf("  │ fwrite (no buffer, _IONBF)      │ %8.2f   │ %6.0f MB/s │ %7d │\n",
           fwrite_unbuffered_time / 1e6, total_bytes / fwrite_unbuffered_time * 1000, iterations);
    printf("  └─────────────────────────────────┴────────────┴────────────┴─────────┘\n");

    printf("\n  Analysis:\n");
    printf("  - Batch writer vs unbuffered: %.2fx speedup\n",
           (double)fwrite_unbuffered_time / batch_time);
    printf("  - Batch writer vs direct syscall: %.2fx speedup\n",
           (double)direct_syscall_time / batch_time);
    printf("  - fwrite already has ~4-8KB stdio buffer, so similar performance\n");
    printf("  - Batch writer value: when you need explicit control over flush timing\n");

    TEST_PASS("performance comparison");
}

static void test_batch_writer_reserve_commit(void) {
    printf("\n=== Batch Writer Reserve/Commit ===\n");

    const char *test_file = "/tmp/test_batch_reserve.log";
    FILE *fp = fopen(test_file, "wb");
    batch_writer *writer = batch_writer_create_default(fp);

    /* Reserve space and write directly */
    char *ptr = batch_writer_reserve(writer, 100);
    if (!ptr) {
        TEST_FAIL("reserve/commit", "reserve failed");
        batch_writer_destroy(writer);
        fclose(fp);
        return;
    }

    int len = sprintf(ptr, "Direct write to buffer: %d\n", 42);
    batch_writer_commit(writer, len);

    batch_writer_flush(writer);
    batch_writer_destroy(writer);
    fclose(fp);

    /* Verify */
    fp = fopen(test_file, "rb");
    char content[256];
    size_t read_len = fread(content, 1, sizeof(content) - 1, fp);
    content[read_len] = '\0';
    fclose(fp);

    if (strstr(content, "Direct write to buffer: 42") == NULL) {
        TEST_FAIL("reserve/commit", "content verification failed");
        return;
    }

    TEST_PASS("reserve/commit");
    remove(test_file);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("============================================\n");
    printf("  SIMD & Batch Writer Test Suite\n");
    printf("============================================\n");

    /* SIMD tests */
    test_cpu_features();
    test_simd_strlen();
    test_simd_memcpy();
    test_simd_itoa();
    test_simd_datetime();

    /* Batch writer tests */
    test_batch_writer_basic();
    test_batch_writer_performance();
    test_batch_writer_reserve_commit();

    printf("\n============================================\n");
    printf("  All tests completed!\n");
    printf("============================================\n");

    return 0;
}

