/* =====================================================================================
 *       Filename:  test_compress.c
 *    Description:  Test for log file compression
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
#include <assert.h>

#include "compress.h"
#include "platform.h"

#define TEST_DIR "./compress_test"
#define TEST_FILE TEST_DIR "/test.log"
#define TEST_FILE_GZ TEST_DIR "/test.log.gz"

static void create_test_file(const char *path, size_t size)
{
    /* Ensure directory exists */
    xlog_mkdir_p(TEST_DIR);

    FILE *fp = fopen(path, "w");
    assert(fp != NULL);

    /* Write repetitive log-like content for good compression ratio */
    const char *line = "2026-03-01 12:00:00.123 [INFO] [main.c:42] This is a test log message with some data: value=12345\n";
    size_t line_len = strlen(line);
    size_t written = 0;

    while (written < size) {
        size_t to_write = (size - written < line_len) ? (size - written) : line_len;
        fwrite(line, 1, to_write, fp);
        written += to_write;
    }

    fclose(fp);
}

static void test_compress_file(void)
{
    printf("Test: xlog_compress_file\n");

    /* Create test directory */
    xlog_mkdir_p(TEST_DIR);

    /* Create a 1MB test file */
    create_test_file(TEST_FILE, 1024 * 1024);

    /* Verify file exists */
    assert(xlog_file_exists(TEST_FILE));
    int64_t orig_size = xlog_file_size(TEST_FILE);
    printf("  Original file size: %ld bytes\n", (long)orig_size);

    /* Compress the file */
    xlog_compress_stats stats;
    xlog_compress_error err = xlog_compress_file(TEST_FILE, TEST_FILE_GZ,
                                                  XLOG_COMPRESS_LEVEL_DEFAULT,
                                                  false, &stats);

    assert(err == XLOG_COMPRESS_OK);
    assert(xlog_file_exists(TEST_FILE_GZ));

    printf("  Compressed size: %lu bytes\n", (unsigned long)stats.compressed_size);
    printf("  Compression ratio: %.2f%%\n", stats.ratio * 100.0);
    printf("  Time: %lu us\n", (unsigned long)stats.time_us);

    /* Verify compression ratio (log files should compress well) */
    assert(stats.ratio < 0.2);  /* Should be less than 20% of original */

    /* Verify compressed file is valid gzip */
    assert(xlog_is_compressed(TEST_FILE_GZ));
    assert(!xlog_is_compressed(TEST_FILE));

    /* Cleanup */
    xlog_remove(TEST_FILE);
    xlog_remove(TEST_FILE_GZ);

    printf("  PASSED\n\n");
}

static void test_compress_with_delete(void)
{
    printf("Test: xlog_compress_file with delete_src\n");

    /* Create test file */
    create_test_file(TEST_FILE, 100 * 1024);  /* 100KB */
    assert(xlog_file_exists(TEST_FILE));

    /* Compress and delete source */
    xlog_compress_error err = xlog_compress_file(TEST_FILE, NULL,
                                                  XLOG_COMPRESS_LEVEL_FAST,
                                                  true, NULL);

    assert(err == XLOG_COMPRESS_OK);

    /* Source should be deleted */
    assert(!xlog_file_exists(TEST_FILE));

    /* Compressed file should exist */
    assert(xlog_file_exists(TEST_FILE_GZ));

    /* Cleanup */
    xlog_remove(TEST_FILE_GZ);

    printf("  PASSED\n\n");
}

static void test_compress_async(void)
{
    printf("Test: xlog_compress_async\n");

    /* Create test file */
    create_test_file(TEST_FILE, 500 * 1024);  /* 500KB */

    /* Start async compression */
    xlog_compress_task *task = xlog_compress_async(TEST_FILE, NULL,
                                                    XLOG_COMPRESS_LEVEL_DEFAULT,
                                                    true);

#ifndef XLOG_NO_COMPRESS
    assert(task != NULL);

    /* Wait for completion */
    xlog_compress_stats stats;
    xlog_compress_error err = xlog_compress_wait(task, &stats);

    assert(err == XLOG_COMPRESS_OK);
    assert(!xlog_file_exists(TEST_FILE));
    assert(xlog_file_exists(TEST_FILE_GZ));

    printf("  Async compression completed\n");
    printf("  Compressed size: %lu bytes\n", (unsigned long)stats.compressed_size);

    /* Cleanup */
    xlog_remove(TEST_FILE_GZ);
#else
    assert(task == NULL);  /* Should return NULL when disabled */
    xlog_remove(TEST_FILE);
    printf("  Compression disabled - stub test passed\n");
#endif

    printf("  PASSED\n\n");
}

static void test_gen_path(void)
{
    printf("Test: xlog_compress_gen_path\n");

    char path[256];

    xlog_compress_gen_path(path, sizeof(path), "/var/log/app.log");
#ifndef XLOG_NO_COMPRESS
    assert(strcmp(path, "/var/log/app.log.gz") == 0);
#endif

    printf("  Generated path: %s\n", path);
    printf("  PASSED\n\n");
}

static void test_is_compressed(void)
{
    printf("Test: xlog_is_compressed\n");

#ifndef XLOG_NO_COMPRESS
    /* Test by extension */
    assert(xlog_is_compressed("file.log.gz") == true);
    assert(xlog_is_compressed("file.tar.gz") == true);
    assert(xlog_is_compressed("file.log") == false);

    /* Create actual gzip file and test by magic */
    create_test_file(TEST_FILE, 1024);
    xlog_compress_file(TEST_FILE, TEST_FILE_GZ, XLOG_COMPRESS_LEVEL_FAST, false, NULL);

    assert(xlog_is_compressed(TEST_FILE_GZ) == true);
    assert(xlog_is_compressed(TEST_FILE) == false);

    xlog_remove(TEST_FILE);
    xlog_remove(TEST_FILE_GZ);
#else
    assert(xlog_is_compressed("file.log.gz") == false);
#endif

    printf("  PASSED\n\n");
}

int main(void)
{
    printf("\n");
    printf("=========================================\n");
    printf("  xlog Compression Tests\n");
    printf("=========================================\n\n");

#ifdef XLOG_NO_COMPRESS
    printf("NOTE: Compression is DISABLED\n\n");
#else
    printf("Compression: ENABLED (miniz)\n\n");
#endif

    test_gen_path();
    test_is_compressed();

#ifndef XLOG_NO_COMPRESS
    test_compress_file();
    test_compress_with_delete();
    test_compress_async();
#endif

    printf("=========================================\n");
    printf("  All tests PASSED!\n");
    printf("=========================================\n\n");

    return 0;
}

