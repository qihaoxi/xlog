/* =====================================================================================
 *       Filename:  test_rotate.c
 *    Description:  Test cases for log rotation logic
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rotate.h"
#include "platform.h"

#define TEST_DIR "/tmp/xlog_rotate_test"
#define TEST_BASE "pel"
#define TEST_EXT ".log"

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

static void cleanup_test_dir(void)
{
	/* Remove all files in test directory */
	char path[512];

	/* Remove active file */
	snprintf(path, sizeof(path), "%s/%s%s", TEST_DIR, TEST_BASE, TEST_EXT);
	xlog_remove(path);

	/* Get current date for cleanup */
	time_t now = time(NULL);
	struct tm tm_info;
	xlog_get_localtime(now, &tm_info);
	int year = tm_info.tm_year + 1900;
	int month = tm_info.tm_mon + 1;
	int day = tm_info.tm_mday;

	/* Clean up files from current date */
	snprintf(path, sizeof(path), "%s/%s-%04d%02d%02d%s",
	         TEST_DIR, TEST_BASE, year, month, day, TEST_EXT);
	xlog_remove(path);

	for (int seq = 1; seq <= 20; seq++)
	{
		snprintf(path, sizeof(path), "%s/%s-%04d%02d%02d-%02d%s",
		         TEST_DIR, TEST_BASE, year, month, day, seq, TEST_EXT);
		xlog_remove(path);
	}

	/* Also clean up files from a range of dates (Feb and Mar 2026) */
	for (int m = 2; m <= 3; m++)
	{
		for (int d = 1; d <= 31; d++)
		{
			snprintf(path, sizeof(path), "%s/%s-2026%02d%02d%s",
			         TEST_DIR, TEST_BASE, m, d, TEST_EXT);
			xlog_remove(path);

			for (int seq = 1; seq <= 20; seq++)
			{
				snprintf(path, sizeof(path), "%s/%s-2026%02d%02d-%02d%s",
				         TEST_DIR, TEST_BASE, m, d, seq, TEST_EXT);
				xlog_remove(path);
			}
		}
	}
}

static void create_test_file(const char *path, size_t size)
{
	FILE *fp = fopen(path, "w");
	if (!fp)
    { return; }

	char buf[1024];
	memset(buf, 'X', sizeof(buf));

	while (size > 0)
	{
		size_t to_write = (size > sizeof(buf)) ? sizeof(buf) : size;
		fwrite(buf, 1, to_write, fp);
		size -= to_write;
	}

	fclose(fp);
}

/* ============================================================================
 * Test Cases
 * ============================================================================ */

static void test_path_generation(void)
{
	printf("\n=== Test: Path Generation ===\n");

	rotate_config config = {
			.base_name = "pel",
			.extension = ".log",
			.directory = "/var/log"
	};

	char path[256];

	/* Test active path */
	rotate_gen_active_path(path, sizeof(path), &config);
	TEST_ASSERT(strcmp(path, "/var/log/pel.log") == 0, "Active path should be /var/log/pel.log");
	TEST_PASS("Active path generation");

	/* Test dated path */
	rotate_gen_dated_path(path, sizeof(path), &config, 2026, 2, 9);
	TEST_ASSERT(strcmp(path, "/var/log/pel-20260209.log") == 0,
	            "Dated path should be /var/log/pel-20260209.log");
	TEST_PASS("Dated path generation");

	/* Test sequenced path */
	rotate_gen_sequenced_path(path, sizeof(path), &config, 2026, 2, 9, 1);
	TEST_ASSERT(strcmp(path, "/var/log/pel-20260209-01.log") == 0,
	            "Sequenced path should be /var/log/pel-20260209-01.log");
	TEST_PASS("Sequenced path generation (01)");

	rotate_gen_sequenced_path(path, sizeof(path), &config, 2026, 2, 9, 15);
	TEST_ASSERT(strcmp(path, "/var/log/pel-20260209-15.log") == 0,
	            "Sequenced path should be /var/log/pel-20260209-15.log");
	TEST_PASS("Sequenced path generation (15)");

	/* Test archive pattern */
	rotate_gen_archive_pattern(path, sizeof(path), &config);
	TEST_ASSERT(strcmp(path, "pel-*.log*") == 0, "Pattern should be pel-*.log*");
	TEST_PASS("Archive pattern generation");
}

static void test_basic_init(void)
{
	printf("\n=== Test: Basic Initialization ===\n");

	cleanup_test_dir();
	xlog_mkdir_p(TEST_DIR);

	rotate_config config = {
			.base_name = TEST_BASE,
			.extension = TEST_EXT,
			.directory = TEST_DIR,
			.max_file_size = 1 * XLOG_MB,
			.max_dir_size = 10 * XLOG_MB,
			.max_files = 10,
			.rotate_on_start = false
	};

	rotate_state state;
	TEST_ASSERT(rotate_init(&state, &config), "rotate_init should succeed");
	TEST_PASS("Initialization");

	/* Check current path */
	const char *path = rotate_get_current_path(&state);
	TEST_ASSERT(path != NULL, "Current path should not be NULL");
	char expected_path[256];
	snprintf(expected_path, sizeof(expected_path), "%s/%s%s", TEST_DIR, TEST_BASE, TEST_EXT);
	TEST_ASSERT(strcmp(path, expected_path) == 0, "Current path should match");
	TEST_PASS("Current path is correct");

	/* Check file was created */
	TEST_ASSERT(state.fp != NULL, "File should be open");
	TEST_PASS("File is open");

	rotate_cleanup(&state);
	TEST_PASS("Cleanup");
}

static void test_write_and_size_tracking(void)
{
	printf("\n=== Test: Write and Size Tracking ===\n");

	cleanup_test_dir();

	rotate_config config = {
			.base_name = TEST_BASE,
			.extension = TEST_EXT,
			.directory = TEST_DIR,
			.max_file_size = 1 * XLOG_MB,
			.max_dir_size = 10 * XLOG_MB,
			.max_files = 10
	};

	rotate_state state;
	TEST_ASSERT(rotate_init(&state, &config), "rotate_init should succeed");
	TEST_PASS("Initialization");

	/* Write some data */
	const char *test_data = "Hello, rotation test!\n";
	size_t data_len = strlen(test_data);

	int64_t written = rotate_write(&state, test_data, data_len);
	TEST_ASSERT(written == (int64_t) data_len, "Should write all data");
	TEST_PASS("Write succeeded");

	TEST_ASSERT(rotate_get_current_size(&state) == data_len, "Size should match written bytes");
	TEST_PASS("Size tracking correct");

	/* Write more data */
	written = rotate_write(&state, test_data, data_len);
	TEST_ASSERT(written == (int64_t) data_len, "Second write should succeed");
	TEST_ASSERT(rotate_get_current_size(&state) == 2 * data_len, "Size should be doubled");
	TEST_PASS("Multiple writes tracked correctly");

	rotate_cleanup(&state);

	/* Verify file size on disk */
	char path[256];
	snprintf(path, sizeof(path), "%s/%s%s", TEST_DIR, TEST_BASE, TEST_EXT);
	int64_t disk_size = xlog_file_size(path);
	TEST_ASSERT(disk_size == (int64_t) (2 * data_len), "File size on disk should match");
	TEST_PASS("Disk size matches");
}

static void test_size_based_rotation(void)
{
	printf("\n=== Test: Size-Based Rotation ===\n");

	cleanup_test_dir();

	/* Use small max size for testing */
	rotate_config config = {
			.base_name = TEST_BASE,
			.extension = TEST_EXT,
			.directory = TEST_DIR,
			.max_file_size = 1024,  /* 1 KB */
			.max_dir_size = 100 * XLOG_MB,
			.max_files = 100
	};

	rotate_state state;
	TEST_ASSERT(rotate_init(&state, &config), "rotate_init should succeed");
	TEST_PASS("Initialization with 1KB max size");

	/* Write data to trigger rotation */
	char buf[512];
	memset(buf, 'A', sizeof(buf));
	buf[sizeof(buf) - 1] = '\n';

	/* First write - should not rotate */
	rotate_write(&state, buf, sizeof(buf));
	TEST_ASSERT(state.total_rotations == 0, "Should not rotate yet");
	TEST_PASS("First write (no rotation)");

	/* Second write - should trigger rotation */
	rotate_write(&state, buf, sizeof(buf));
	TEST_ASSERT(state.total_rotations == 0, "Still under limit");
	TEST_PASS("Second write (still under limit)");

	/* Third write - should trigger rotation */
	rotate_write(&state, buf, sizeof(buf));
	TEST_ASSERT(state.total_rotations == 1, "Should have rotated once");
	TEST_PASS("Third write triggered rotation");

	/* Check archive file exists */
	int year, month, day;
	time_t now = time(NULL);
	struct tm tm_info;
	xlog_get_localtime(now, &tm_info);
	year = tm_info.tm_year + 1900;
	month = tm_info.tm_mon + 1;
	day = tm_info.tm_mday;

	char archive_path[256];
	rotate_gen_dated_path(archive_path, sizeof(archive_path), &config, year, month, day);
	TEST_ASSERT(xlog_file_exists(archive_path), "Archive file should exist");
	TEST_PASS("Archive file created");

	printf("  Archive: %s\n", archive_path);

	/* Continue writing to trigger more rotations */
	rotate_write(&state, buf, sizeof(buf));
	rotate_write(&state, buf, sizeof(buf));
	rotate_write(&state, buf, sizeof(buf));

	TEST_ASSERT(state.total_rotations >= 2, "Should have rotated at least twice");
	TEST_PASS("Multiple rotations");

	/* Check sequenced archive exists */
	rotate_gen_sequenced_path(archive_path, sizeof(archive_path), &config, year, month, day, 1);
	TEST_ASSERT(xlog_file_exists(archive_path), "Sequenced archive should exist");
	TEST_PASS("Sequenced archive file created");

	printf("  Sequenced archive: %s\n", archive_path);
	printf("  Total rotations: %lu\n", state.total_rotations);

	rotate_cleanup(&state);
}

static void test_sequence_number_finding(void)
{
	printf("\n=== Test: Sequence Number Finding ===\n");

	cleanup_test_dir();
	xlog_mkdir_p(TEST_DIR);

	/* Get current date */
	int year, month, day;
	time_t now = time(NULL);
	struct tm tm_info;
	xlog_get_localtime(now, &tm_info);
	year = tm_info.tm_year + 1900;
	month = tm_info.tm_mon + 1;
	day = tm_info.tm_mday;

	rotate_config config = {
			.base_name = TEST_BASE,
			.extension = TEST_EXT,
			.directory = TEST_DIR,
			.max_file_size = 1 * XLOG_MB,
			.max_dir_size = 10 * XLOG_MB,
			.max_files = 10
	};

	rotate_state state;
	memset(&state, 0, sizeof(state));
	memcpy(&state.config, &config, sizeof(config));
	xlog_strncpy(state.dir_path, config.directory, sizeof(state.dir_path));
	state.current_year = year;
	state.current_month = month;
	state.current_day = day;

	/* No archives exist - should return 0 */
	int seq = rotate_find_next_sequence(&state);
	TEST_ASSERT(seq == 0, "No archives - sequence should be 0");
	TEST_PASS("No archives: sequence = 0");

	/* Create dated archive */
	char path[256];
	rotate_gen_dated_path(path, sizeof(path), &config, year, month, day);
	create_test_file(path, 100);

	seq = rotate_find_next_sequence(&state);
	TEST_ASSERT(seq == 1, "Dated exists - sequence should be 1");
	TEST_PASS("After dated archive: sequence = 1");

	/* Create sequence 01 */
	rotate_gen_sequenced_path(path, sizeof(path), &config, year, month, day, 1);
	create_test_file(path, 100);

	seq = rotate_find_next_sequence(&state);
	TEST_ASSERT(seq == 2, "01 exists - sequence should be 2");
	TEST_PASS("After sequence 01: sequence = 2");

	/* Create sequence 02, skip 03, create 04 */
	rotate_gen_sequenced_path(path, sizeof(path), &config, year, month, day, 2);
	create_test_file(path, 100);

	seq = rotate_find_next_sequence(&state);
	TEST_ASSERT(seq == 3, "02 exists - sequence should be 3");
	TEST_PASS("After sequence 02: sequence = 3");

	cleanup_test_dir();
}

static void test_directory_limit_enforcement(void)
{
	printf("\n=== Test: Directory Limit Enforcement ===\n");

	cleanup_test_dir();
	xlog_mkdir_p(TEST_DIR);

	/* Get current date */
	int year, month, day;
	time_t now = time(NULL);
	struct tm tm_info;
	xlog_get_localtime(now, &tm_info);
	year = tm_info.tm_year + 1900;
	month = tm_info.tm_mon + 1;
	day = tm_info.tm_mday;

	/*
	 * Test scenario:
	 * - max_files = 5 (total files including active file)
	 * - max_dir_size = 10KB
	 * - Create 6 archive files of 2KB each = 12KB total
	 *
	 * Expected behavior on init:
	 * - Should delete oldest files until under limits
	 * - Either by file count (keep 4 archives + 1 active = 5)
	 * - Or by size (keep ~5 files to stay under 10KB)
	 */
	rotate_config config = {
			.base_name = TEST_BASE,
			.extension = TEST_EXT,
			.directory = TEST_DIR,
			.max_file_size = 1 * XLOG_KB,
			.max_dir_size = 10 * XLOG_KB,  /* 10KB limit */
			.max_files = 5                  /* Total 5 files (including active) */
	};

	/* Create 6 archive files (exceeds max_files - 1 = 4 archives allowed) */
	char path[256];
	printf("  Creating archive files:\n");
	for (int i = 1; i <= 6; i++)
	{
		rotate_gen_sequenced_path(path, sizeof(path), &config, year, month, day, i);
		create_test_file(path, 2 * XLOG_KB);
		printf("    %s (2KB)\n", path);
	}

	/* Also create dated archive */
	rotate_gen_dated_path(path, sizeof(path), &config, year, month, day);
	create_test_file(path, 2 * XLOG_KB);
	printf("    %s (2KB)\n", path);

	/* Total: 7 archives * 2KB = 14KB, max_dir_size = 10KB */
	printf("  Total before init: 7 archives x 2KB = 14KB\n");
	printf("  Limits: max_files=%u, max_dir_size=%lu bytes\n",
	       config.max_files, (unsigned long) config.max_dir_size);

	rotate_state state;
	TEST_ASSERT(rotate_init(&state, &config), "rotate_init should succeed");
	TEST_PASS("Initialization");

	/* Check files were deleted */
	printf("  Files deleted by enforcement: %lu\n", state.files_deleted);
	TEST_ASSERT(state.files_deleted >= 3, "Should have deleted at least 3 files");
	TEST_PASS("Old files deleted");

	/* List remaining archives */
	int count;
	char **archives = rotate_list_archives(&state, &count);
	printf("  Remaining archives: %d (expected: <= %u - 1 = %u)\n",
	       count, config.max_files, config.max_files - 1);
	for (int i = 0; i < count; i++)
	{
		printf("    %s\n", archives[i]);
	}

	/* max_files includes active file, so archives should be <= max_files - 1 */
	TEST_ASSERT(count <= (int) (config.max_files - 1), "Archive count within limit");
	TEST_PASS("File count within limit");

	/* Check total directory size is under limit */
	int64_t total_size = rotate_calc_dir_size(&state);
	printf("  Total directory size: %ld bytes (limit: %lu)\n",
	       total_size, (unsigned long) config.max_dir_size);
	TEST_ASSERT(total_size <= (int64_t) config.max_dir_size, "Directory size within limit");
	TEST_PASS("Directory size within limit");

	rotate_free_archive_list(archives, count);
	rotate_cleanup(&state);
}

static void test_startup_rotation(void)
{
	printf("\n=== Test: Startup Rotation ===\n");

	cleanup_test_dir();
	xlog_mkdir_p(TEST_DIR);

	/* Create an existing log file that exceeds size limit */
	char active_path[256];
	snprintf(active_path, sizeof(active_path), "%s/%s%s", TEST_DIR, TEST_BASE, TEST_EXT);
	create_test_file(active_path, 2 * XLOG_KB);

	printf("  Created existing file: %s (2KB)\n", active_path);

	rotate_config config = {
			.base_name = TEST_BASE,
			.extension = TEST_EXT,
			.directory = TEST_DIR,
			.max_file_size = 1 * XLOG_KB,  /* 1KB limit */
			.max_dir_size = 10 * XLOG_MB,
			.max_files = 10,
			.rotate_on_start = true  /* Enable startup rotation */
	};

	rotate_state state;
	TEST_ASSERT(rotate_init(&state, &config), "rotate_init should succeed");
	TEST_PASS("Initialization with rotate_on_start");

	/* The existing file should have been rotated */
	TEST_ASSERT(state.total_rotations == 1, "Should have rotated on startup");
	TEST_PASS("Rotated on startup");

	/* Current file should be fresh */
	TEST_ASSERT(state.current_size == 0, "Current file should be empty");
	TEST_PASS("Current file is fresh");

	/* Check archive exists */
	int year, month, day;
	time_t now = time(NULL);
	struct tm tm_info;
	xlog_get_localtime(now, &tm_info);
	year = tm_info.tm_year + 1900;
	month = tm_info.tm_mon + 1;
	day = tm_info.tm_mday;

	char archive_path[256];
	rotate_gen_dated_path(archive_path, sizeof(archive_path), &config, year, month, day);
	TEST_ASSERT(xlog_file_exists(archive_path), "Archive should exist");
	TEST_PASS("Archive created from existing file");

	printf("  Archive: %s\n", archive_path);

	rotate_cleanup(&state);
}

static void test_listing_and_sorting(void)
{
	printf("\n=== Test: Archive Listing and Sorting ===\n");

	cleanup_test_dir();
	xlog_mkdir_p(TEST_DIR);

	rotate_config config = {
			.base_name = TEST_BASE,
			.extension = TEST_EXT,
			.directory = TEST_DIR,
			.max_file_size = 1 * XLOG_MB,
			.max_dir_size = 100 * XLOG_MB,
			.max_files = 100
	};

	/* Create archives from different dates */
	char path[256];

	/* Older dates */
	rotate_gen_dated_path(path, sizeof(path), &config, 2026, 2, 5);
	create_test_file(path, 100);

	rotate_gen_dated_path(path, sizeof(path), &config, 2026, 2, 7);
	create_test_file(path, 100);

	rotate_gen_sequenced_path(path, sizeof(path), &config, 2026, 2, 7, 1);
	create_test_file(path, 100);

	rotate_gen_dated_path(path, sizeof(path), &config, 2026, 2, 6);
	create_test_file(path, 100);

	rotate_state state;
	memset(&state, 0, sizeof(state));
	memcpy(&state.config, &config, sizeof(config));
	xlog_strncpy(state.dir_path, config.directory, sizeof(state.dir_path));

	int count;
	char **archives = rotate_list_archives(&state, &count);

	TEST_ASSERT(count == 4, "Should list 4 archives");
	TEST_PASS("Listed all archives");

	printf("  Archives (should be sorted oldest first):\n");
	for (int i = 0; i < count; i++)
	{
		printf("    %d: %s\n", i, archives[i]);
	}

	/* Check sorting - older dates should come first */
	/* Note: "-01" sorts before ".log" in ASCII (hyphen=45, period=46) */
	TEST_ASSERT(strstr(archives[0], "20260205") != NULL, "First should be 20260205");
	TEST_ASSERT(strstr(archives[1], "20260206") != NULL, "Second should be 20260206");
	/* pel-20260207-01.log comes before pel-20260207.log (- < .) */
	TEST_ASSERT(strstr(archives[2], "20260207-01") != NULL, "Third should be 20260207-01");
	TEST_ASSERT(strstr(archives[3], "20260207.log") != NULL, "Fourth should be 20260207.log");
	TEST_PASS("Archives sorted correctly");

	rotate_free_archive_list(archives, count);
	cleanup_test_dir();
}

/**
 * Test the specific scenario:
 * When pel-20260210.log exists and a new rotation is needed,
 * it should:
 * 1. Rename pel-20260210.log to pel-20260210-01.log
 * 2. Archive current pel.log to pel-20260210-02.log
 */
static void test_dated_archive_normalization(void)
{
	printf("\n=== Test: Dated Archive Normalization ===\n");

	cleanup_test_dir();
	xlog_mkdir_p(TEST_DIR);

	/* Get current date */
	int year, month, day;
	time_t now = time(NULL);
	struct tm tm_info;
	xlog_get_localtime(now, &tm_info);
	year = tm_info.tm_year + 1900;
	month = tm_info.tm_mon + 1;
	day = tm_info.tm_mday;

	/* Create an existing dated archive (simulating previous run) */
	char dated_path[256];
	rotate_config config = {
			.base_name = TEST_BASE,
			.extension = TEST_EXT,
			.directory = TEST_DIR,
			.max_file_size = 1 * XLOG_KB,  /* 1KB for easy testing */
			.max_dir_size = 100 * XLOG_MB,
			.max_files = 100
	};
	rotate_gen_dated_path(dated_path, sizeof(dated_path), &config, year, month, day);
	create_test_file(dated_path, 2048);  /* 2KB dated archive */

	printf("  Created dated archive: %s (2KB)\n", dated_path);
	TEST_ASSERT(xlog_file_exists(dated_path), "Dated archive should exist");
	TEST_PASS("Pre-existing dated archive created");

	/* Create an active log file */
	char active_path[256];
	rotate_gen_active_path(active_path, sizeof(active_path), &config);
	create_test_file(active_path, 512);  /* 512B active file */

	printf("  Created active file: %s (512B)\n", active_path);
	TEST_ASSERT(xlog_file_exists(active_path), "Active file should exist");
	TEST_PASS("Active file created");

	/* Initialize rotate state */
	rotate_state state;
	TEST_ASSERT(rotate_init(&state, &config), "rotate_init should succeed");
	TEST_PASS("Initialization");

	/* Write enough data to trigger rotation */
	char buf[512];
	memset(buf, 'B', sizeof(buf));
	buf[sizeof(buf) - 1] = '\n';

	/* Write to exceed 1KB limit */
	rotate_write(&state, buf, sizeof(buf));
	rotate_write(&state, buf, sizeof(buf));
	rotate_write(&state, buf, sizeof(buf));

	printf("  Written data to trigger rotation\n");
	TEST_ASSERT(state.total_rotations >= 1, "Should have rotated");
	TEST_PASS("Rotation triggered");

	/* Check that dated archive was renamed to -01 */
	char seq01_path[256];
	rotate_gen_sequenced_path(seq01_path, sizeof(seq01_path), &config, year, month, day, 1);
	printf("  Checking for: %s\n", seq01_path);

	TEST_ASSERT(xlog_file_exists(seq01_path), "pel-YYYYMMDD-01.log should exist");
	TEST_PASS("Dated archive renamed to -01");

	/* Check that dated archive no longer exists (was renamed) */
	TEST_ASSERT(!xlog_file_exists(dated_path), "Original dated archive should be renamed");
	TEST_PASS("Original dated archive removed");

	/* Check that -02 exists (from active file rotation) */
	char seq02_path[256];
	rotate_gen_sequenced_path(seq02_path, sizeof(seq02_path), &config, year, month, day, 2);
	printf("  Checking for: %s\n", seq02_path);

	TEST_ASSERT(xlog_file_exists(seq02_path), "pel-YYYYMMDD-02.log should exist");
	TEST_PASS("Active file archived to -02");

	/* Active file should still be pel.log */
	TEST_ASSERT(xlog_file_exists(active_path), "Active file should exist");
	TEST_PASS("Active file is still pel.log");

	/* Print final state */
	int count;
	char **archives = rotate_list_archives(&state, &count);
	printf("  Final archives (%d files):\n", count);
	for (int i = 0; i < count; i++)
	{
		printf("    %s\n", archives[i]);
	}
	rotate_free_archive_list(archives, count);

	rotate_cleanup(&state);
	cleanup_test_dir();
}

/**
 * Test scenario where both pel-20260210.log AND pel-20260210-01.log exist.
 * This is an edge case - normally shouldn't happen.
 * Expected: just find next sequence and archive current file.
 */
static void test_dated_and_seq01_both_exist(void)
{
	printf("\n=== Test: Both Dated and -01 Archives Exist ===\n");

	cleanup_test_dir();
	xlog_mkdir_p(TEST_DIR);

	/* Get current date */
	int year, month, day;
	time_t now = time(NULL);
	struct tm tm_info;
	xlog_get_localtime(now, &tm_info);
	year = tm_info.tm_year + 1900;
	month = tm_info.tm_mon + 1;
	day = tm_info.tm_mday;

	rotate_config config = {
			.base_name = TEST_BASE,
			.extension = TEST_EXT,
			.directory = TEST_DIR,
			.max_file_size = 1 * XLOG_KB,
			.max_dir_size = 100 * XLOG_MB,
			.max_files = 100
	};

	/* Create both pel-20260210.log and pel-20260210-01.log (edge case) */
	char dated_path[256];
	rotate_gen_dated_path(dated_path, sizeof(dated_path), &config, year, month, day);
	create_test_file(dated_path, 2048);
	printf("  Created: %s (2KB)\n", dated_path);

	char seq01_path[256];
	rotate_gen_sequenced_path(seq01_path, sizeof(seq01_path), &config, year, month, day, 1);
	create_test_file(seq01_path, 1024);
	printf("  Created: %s (1KB)\n", seq01_path);

	TEST_ASSERT(xlog_file_exists(dated_path), "Dated archive should exist");
	TEST_ASSERT(xlog_file_exists(seq01_path), "-01 archive should exist");
	TEST_PASS("Both dated and -01 archives created");

	/* Create active file */
	char active_path[256];
	rotate_gen_active_path(active_path, sizeof(active_path), &config);
	create_test_file(active_path, 512);
	printf("  Created: %s (512B)\n", active_path);

	/* Initialize and write to trigger rotation */
	rotate_state state;
	TEST_ASSERT(rotate_init(&state, &config), "rotate_init should succeed");
	TEST_PASS("Initialization");

	/* Write to trigger rotation */
	char buf[512];
	memset(buf, 'C', sizeof(buf));
	buf[sizeof(buf) - 1] = '\n';
	rotate_write(&state, buf, sizeof(buf));
	rotate_write(&state, buf, sizeof(buf));
	rotate_write(&state, buf, sizeof(buf));

	printf("  Written data to trigger rotation\n");
	TEST_ASSERT(state.total_rotations >= 1, "Should have rotated");
	TEST_PASS("Rotation triggered");

	/* -01 should still exist */
	TEST_ASSERT(xlog_file_exists(seq01_path), "-01 should still exist");
	TEST_PASS("-01 archive preserved");

	/* -02 should exist (from current file) */
	char seq02_path[256];
	rotate_gen_sequenced_path(seq02_path, sizeof(seq02_path), &config, year, month, day, 2);
	printf("  Checking for: %s\n", seq02_path);
	TEST_ASSERT(xlog_file_exists(seq02_path), "pel-YYYYMMDD-02.log should exist");
	TEST_PASS("Active file archived to -02");

	/* List final state */
	int count;
	char **archives = rotate_list_archives(&state, &count);
	printf("  Final archives (%d files):\n", count);
	for (int i = 0; i < count; i++)
	{
		printf("    %s\n", archives[i]);
	}
	rotate_free_archive_list(archives, count);

	rotate_cleanup(&state);
	cleanup_test_dir();
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
	printf("===========================================\n");
	printf("   Log Rotation Test Suite\n");
	printf("===========================================\n");

	/* Ensure test directory exists */
	xlog_mkdir_p(TEST_DIR);

	/* Run tests */
	test_path_generation();
	test_basic_init();
	test_write_and_size_tracking();
	test_size_based_rotation();
	test_sequence_number_finding();
	test_directory_limit_enforcement();
	test_startup_rotation();
	test_listing_and_sorting();
	test_dated_archive_normalization();
	test_dated_and_seq01_both_exist();

	/* Summary */
	printf("\n===========================================\n");
	printf("   Results: %d passed, %d failed\n", tests_passed, tests_failed);
	printf("===========================================\n");

	/* Cleanup */
	cleanup_test_dir();

	return tests_failed > 0 ? 1 : 0;
}

