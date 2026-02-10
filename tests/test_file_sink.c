/* =====================================================================================
 *       Filename:  test_file_sink.c
 *    Description:  Integration test for file sink with advanced rotation
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
#include "file_sink.h"
#include "platform.h"

#define TEST_DIR "/tmp/xlog_file_sink_test"
#define TEST_BASE "myapp"

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAILED: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    printf("  ✓ %s\n", msg); \
    tests_passed++; \
} while(0)

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static void cleanup_test_dir(void) {
    char path[512];

    /* Remove active file */
    snprintf(path, sizeof(path), "%s/%s.log", TEST_DIR, TEST_BASE);
    xlog_remove(path);

    /* Remove dated and sequenced archives */
    for (int m = 1; m <= 12; m++) {
        for (int d = 1; d <= 31; d++) {
            snprintf(path, sizeof(path), "%s/%s-2026%02d%02d.log", TEST_DIR, TEST_BASE, m, d);
            xlog_remove(path);

            for (int seq = 1; seq <= 20; seq++) {
                snprintf(path, sizeof(path), "%s/%s-2026%02d%02d-%02d.log",
                         TEST_DIR, TEST_BASE, m, d, seq);
                xlog_remove(path);
            }
        }
    }
}

/* ============================================================================
 * Test Cases
 * ============================================================================ */

static void test_basic_creation(void) {
    printf("\n=== Test: Basic Creation ===\n");

    cleanup_test_dir();
    xlog_mkdir_p(TEST_DIR);

    /* Create sink with default settings */
    sink_t *sink = file_sink_create_default(TEST_DIR, TEST_BASE, LOG_LEVEL_DEBUG);
    TEST_ASSERT(sink != NULL, "file_sink_create_default should succeed");
    TEST_PASS("Sink created");

    /* Check path */
    const char *path = file_sink_get_path(sink);
    TEST_ASSERT(path != NULL, "Path should not be NULL");
    printf("  Current path: %s\n", path);

    char expected[256];
    snprintf(expected, sizeof(expected), "%s/%s.log", TEST_DIR, TEST_BASE);
    TEST_ASSERT(strcmp(path, expected) == 0, "Path should match expected");
    TEST_PASS("Path is correct");

    /* Write some data */
    const char *msg = "Hello, file sink!\n";
    sink->write(sink, msg, strlen(msg));
    sink->flush(sink);

    uint64_t size = file_sink_get_size(sink);
    TEST_ASSERT(size == strlen(msg), "Size should match written bytes");
    TEST_PASS("Write and size tracking");

    /* Cleanup */
    sink_destroy(sink);
    TEST_PASS("Sink destroyed");

    /* Verify file exists and has content */
    int64_t file_size = xlog_file_size(expected);
    TEST_ASSERT(file_size == (int64_t)strlen(msg), "File should have correct size");
    TEST_PASS("File persisted correctly");
}

static void test_rotation_on_size(void) {
    printf("\n=== Test: Rotation on Size ===\n");

    cleanup_test_dir();
    xlog_mkdir_p(TEST_DIR);

    /* Create sink with small size limit */
    file_sink_config config = {
        .directory = TEST_DIR,
        .base_name = TEST_BASE,
        .extension = ".log",
        .max_file_size = 1 * XLOG_KB,   /* 1 KB */
        .max_dir_size = 100 * XLOG_MB,
        .max_files = 100,
        .rotate_on_start = true,
        .flush_on_write = true
    };

    sink_t *sink = file_sink_create(&config, LOG_LEVEL_DEBUG);
    TEST_ASSERT(sink != NULL, "Sink creation should succeed");
    TEST_PASS("Sink created with 1KB limit");

    /* Write data to trigger rotation */
    char buf[256];
    memset(buf, 'X', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\n';

    /* Write multiple times */
    for (int i = 0; i < 10; i++) {
        sink->write(sink, buf, sizeof(buf));
    }

    /* Get stats */
    uint64_t rotations, total_bytes, deleted;
    file_sink_get_stats(sink, &rotations, &total_bytes, &deleted);

    printf("  Rotations: %lu\n", rotations);
    printf("  Total bytes: %lu\n", total_bytes);
    printf("  Files deleted: %lu\n", deleted);

    TEST_ASSERT(rotations >= 2, "Should have rotated at least twice");
    TEST_PASS("Rotation occurred");

    TEST_ASSERT(total_bytes == 10 * sizeof(buf), "Total bytes should be correct");
    TEST_PASS("Byte tracking correct");

    sink_destroy(sink);

    /* Verify archives exist */
    int year, month, day;
    time_t now = time(NULL);
    struct tm tm_info;
    xlog_get_localtime(now, &tm_info);
    year = tm_info.tm_year + 1900;
    month = tm_info.tm_mon + 1;
    day = tm_info.tm_mday;

    char archive_path[256];
    snprintf(archive_path, sizeof(archive_path), "%s/%s-%04d%02d%02d.log",
             TEST_DIR, TEST_BASE, year, month, day);

    TEST_ASSERT(xlog_file_exists(archive_path), "Dated archive should exist");
    TEST_PASS("Archive file created");
    printf("  Archive: %s\n", archive_path);
}

static void test_directory_limits(void) {
    printf("\n=== Test: Directory Size Limits ===\n");

    cleanup_test_dir();
    xlog_mkdir_p(TEST_DIR);

    /* Create sink with small directory limit */
    file_sink_config config = {
        .directory = TEST_DIR,
        .base_name = TEST_BASE,
        .extension = ".log",
        .max_file_size = 512,           /* 512 bytes per file */
        .max_dir_size = 3 * XLOG_KB,    /* 3 KB total */
        .max_files = 5,                 /* Max 5 files */
        .rotate_on_start = true,
        .flush_on_write = true
    };

    sink_t *sink = file_sink_create(&config, LOG_LEVEL_DEBUG);
    TEST_ASSERT(sink != NULL, "Sink creation should succeed");
    TEST_PASS("Sink created with small limits");

    /* Write lots of data to create many archives */
    char buf[128];
    memset(buf, 'Y', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\n';

    for (int i = 0; i < 50; i++) {
        sink->write(sink, buf, sizeof(buf));
    }

    uint64_t rotations, total_bytes, deleted;
    file_sink_get_stats(sink, &rotations, &total_bytes, &deleted);

    printf("  Rotations: %lu\n", rotations);
    printf("  Files deleted: %lu\n", deleted);

    TEST_ASSERT(deleted > 0, "Some files should have been deleted");
    TEST_PASS("Old files deleted due to limits");

    sink_destroy(sink);
}

static void test_convenience_api(void) {
    printf("\n=== Test: Convenience API ===\n");

    cleanup_test_dir();
    xlog_mkdir_p(TEST_DIR);

    char path[256];
    snprintf(path, sizeof(path), "%s/%s.log", TEST_DIR, TEST_BASE);

    /* Test file_sink_create_simple - parses path automatically */
    sink_t *sink = file_sink_create_simple(path, LOG_LEVEL_DEBUG);
    TEST_ASSERT(sink != NULL, "file_sink_create_simple should succeed");
    TEST_PASS("Simple path-based creation");

    sink->write(sink, "Simple test\n", 12);
    sink->flush(sink);
    sink_destroy(sink);

    /* Test file_sink_create_default */
    sink = file_sink_create_default(TEST_DIR, TEST_BASE, LOG_LEVEL_DEBUG);
    TEST_ASSERT(sink != NULL, "file_sink_create_default should succeed");
    TEST_PASS("Default creation with directory and base name");

    sink->write(sink, "Default test\n", 13);
    sink->flush(sink);
    sink_destroy(sink);

    /* Test file_sink_create_with_limits */
    sink = file_sink_create_with_limits(TEST_DIR, TEST_BASE,
                                         10 * XLOG_KB, 100 * XLOG_KB, LOG_LEVEL_DEBUG);
    TEST_ASSERT(sink != NULL, "file_sink_create_with_limits should succeed");
    TEST_PASS("Creation with custom limits");

    sink->write(sink, "Limits test\n", 12);
    sink_destroy(sink);
}

static void test_flush_on_write(void) {
    printf("\n=== Test: Flush on Write ===\n");

    cleanup_test_dir();
    xlog_mkdir_p(TEST_DIR);

    file_sink_config config = {
        .directory = TEST_DIR,
        .base_name = TEST_BASE,
        .extension = ".log",
        .max_file_size = 10 * XLOG_MB,
        .max_dir_size = 100 * XLOG_MB,
        .max_files = 10,
        .rotate_on_start = false,
        .flush_on_write = true  /* Enable immediate flush */
    };

    sink_t *sink = file_sink_create(&config, LOG_LEVEL_DEBUG);
    TEST_ASSERT(sink != NULL, "Sink creation should succeed");
    TEST_PASS("Sink created with flush_on_write");

    const char *msg = "Immediate flush test\n";
    sink->write(sink, msg, strlen(msg));

    /* File should have content immediately */
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.log", TEST_DIR, TEST_BASE);
    int64_t size = xlog_file_size(path);

    TEST_ASSERT(size == (int64_t)strlen(msg), "File should be flushed immediately");
    TEST_PASS("Immediate flush works");

    sink_destroy(sink);
}

static void test_force_rotate(void) {
    printf("\n=== Test: Force Rotation ===\n");

    cleanup_test_dir();
    xlog_mkdir_p(TEST_DIR);

    sink_t *sink = file_sink_create_default(TEST_DIR, TEST_BASE, LOG_LEVEL_DEBUG);
    TEST_ASSERT(sink != NULL, "Sink creation should succeed");

    /* Write some data */
    const char *msg = "Before rotation\n";
    sink->write(sink, msg, strlen(msg));
    sink->flush(sink);

    uint64_t rotations_before, bytes, deleted;
    file_sink_get_stats(sink, &rotations_before, &bytes, &deleted);

    /* Force rotation */
    bool result = file_sink_force_rotate(sink);
    TEST_ASSERT(result, "Force rotation should succeed");
    TEST_PASS("Force rotation succeeded");

    uint64_t rotations_after;
    file_sink_get_stats(sink, &rotations_after, &bytes, &deleted);

    TEST_ASSERT(rotations_after == rotations_before + 1, "Rotation count should increase");
    TEST_PASS("Rotation count increased");

    /* Write more after rotation */
    const char *msg2 = "After rotation\n";
    sink->write(sink, msg2, strlen(msg2));

    /* Current file should only have the new data */
    uint64_t size = file_sink_get_size(sink);
    TEST_ASSERT(size == strlen(msg2), "Current file should only have new data");
    TEST_PASS("New file started after rotation");

    sink_destroy(sink);
}

static void test_startup_rotation(void) {
    printf("\n=== Test: Startup Rotation ===\n");

    cleanup_test_dir();
    xlog_mkdir_p(TEST_DIR);

    /* Create a file that exceeds size limit */
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.log", TEST_DIR, TEST_BASE);

    FILE *fp = fopen(path, "w");
    TEST_ASSERT(fp != NULL, "Should be able to create test file");

    /* Write 2KB of data */
    char buf[1024];
    memset(buf, 'Z', sizeof(buf));
    fwrite(buf, 1, sizeof(buf), fp);
    fwrite(buf, 1, sizeof(buf), fp);
    fclose(fp);

    printf("  Created existing file: %s (2KB)\n", path);

    /* Create sink with 1KB limit and rotate_on_start */
    file_sink_config config = {
        .directory = TEST_DIR,
        .base_name = TEST_BASE,
        .extension = ".log",
        .max_file_size = 1 * XLOG_KB,
        .max_dir_size = 100 * XLOG_MB,
        .max_files = 100,
        .rotate_on_start = true,
        .flush_on_write = false
    };

    sink_t *sink = file_sink_create(&config, LOG_LEVEL_DEBUG);
    TEST_ASSERT(sink != NULL, "Sink creation should succeed");
    TEST_PASS("Sink created with rotate_on_start");

    /* Check that rotation happened on startup */
    uint64_t rotations, bytes, deleted;
    file_sink_get_stats(sink, &rotations, &bytes, &deleted);

    TEST_ASSERT(rotations == 1, "Should have rotated on startup");
    TEST_PASS("Rotated existing large file on startup");

    /* Current file should be empty or very small */
    uint64_t size = file_sink_get_size(sink);
    TEST_ASSERT(size == 0, "Current file should be empty");
    TEST_PASS("Started fresh after rotation");

    /* Archive should exist */
    int year, month, day;
    time_t now = time(NULL);
    struct tm tm_info;
    xlog_get_localtime(now, &tm_info);
    year = tm_info.tm_year + 1900;
    month = tm_info.tm_mon + 1;
    day = tm_info.tm_mday;

    char archive_path[256];
    snprintf(archive_path, sizeof(archive_path), "%s/%s-%04d%02d%02d.log",
             TEST_DIR, TEST_BASE, year, month, day);

    TEST_ASSERT(xlog_file_exists(archive_path), "Archive should exist");
    printf("  Archive: %s\n", archive_path);

    int64_t archive_size = xlog_file_size(archive_path);
    TEST_ASSERT(archive_size == 2048, "Archive should have original 2KB data");
    TEST_PASS("Archive contains original data");

    sink_destroy(sink);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("   File Sink V2 Integration Test Suite\n");
    printf("===========================================\n");

    xlog_mkdir_p(TEST_DIR);

    test_basic_creation();
    test_rotation_on_size();
    test_directory_limits();
    test_convenience_api();
    test_flush_on_write();
    test_force_rotate();
    test_startup_rotation();

    printf("\n===========================================\n");
    printf("   Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("===========================================\n");

    cleanup_test_dir();

    return tests_failed > 0 ? 1 : 0;
}

