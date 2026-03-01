/* =====================================================================================
 *       Filename:  rotate.c
 *    Description:  Log rotation strategy implementation
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
#include "rotate.h"
#include "platform.h"
#include "compress.h"

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

static char *str_dup(const char *s)
{
	if (!s)
	{
		return NULL;
	}
	size_t len = strlen(s) + 1;
	char *dup = malloc(len);
	if (dup)
	{
		memcpy(dup, s, len);
	}
	return dup;
}

static void get_current_date(int *year, int *month, int *day)
{
	time_t now = time(NULL);
	struct tm tm_info;
	xlog_get_localtime(now, &tm_info);
	*year = tm_info.tm_year + 1900;
	*month = tm_info.tm_mon + 1;
	*day = tm_info.tm_mday;
}

static bool date_changed(const rotate_state *state)
{
	int year, month, day;
	get_current_date(&year, &month, &day);
	return (year != state->current_year ||
	        month != state->current_month ||
	        day != state->current_day);
}

static void update_current_date(rotate_state *state)
{
	get_current_date(&state->current_year, &state->current_month, &state->current_day);
}

static bool open_current_file(rotate_state *state)
{
	if (state->fp)
	{
		fclose(state->fp);
		state->fp = NULL;
	}

	state->fp = fopen(state->current_path, "a");
	if (!state->fp)
	{
		return false;
	}

	/* Get current file size */
	int64_t size = xlog_file_size(state->current_path);
	state->current_size = (size > 0) ? (uint64_t) size : 0;

	return true;
}

/* Compare function for sorting archive file names */
static int archive_name_compare(const void *a, const void *b)
{
	return strcmp(*(const char **) a, *(const char **) b);
}

/* ============================================================================
 * Path Generation Functions
 * ============================================================================ */

void rotate_gen_active_path(char *out, size_t out_size, const rotate_config *config)
{
	xlog_snprintf(out, out_size, "%s%c%s%s",
	              config->directory, XLOG_PATH_SEP,
	              config->base_name, config->extension);
}

void rotate_gen_dated_path(char *out, size_t out_size, const rotate_config *config,
                           int year, int month, int day)
{
	xlog_snprintf(out, out_size, "%s%c%s-%04d%02d%02d%s",
	              config->directory, XLOG_PATH_SEP,
	              config->base_name, year, month, day, config->extension);
}

void rotate_gen_sequenced_path(char *out, size_t out_size, const rotate_config *config,
                               int year, int month, int day, int sequence)
{
	xlog_snprintf(out, out_size, "%s%c%s-%04d%02d%02d-%02d%s",
	              config->directory, XLOG_PATH_SEP,
	              config->base_name, year, month, day, sequence, config->extension);
}

void rotate_gen_archive_pattern(char *out, size_t out_size, const rotate_config *config)
{
	xlog_snprintf(out, out_size, "%s-*%s*", config->base_name, config->extension);
}

/* ============================================================================
 * Directory Management Functions
 * ============================================================================ */

int64_t rotate_calc_dir_size(const rotate_state *state)
{
	char pattern[128];
	int64_t total = 0;

	/* Size of current active file */
	int64_t active_size = xlog_file_size(state->current_path);
	if (active_size > 0)
	{
		total += active_size;
	}

	/* Size of all archived files */
	rotate_gen_archive_pattern(pattern, sizeof(pattern), &state->config);
	int64_t archive_size = xlog_dir_used_space(state->dir_path, pattern);
	if (archive_size > 0)
	{
		total += archive_size;
	}

	return total;
}

/* Callback context for collecting file names */
typedef struct
{
	char **files;
	int count;
	int capacity;
	const char *dir_path;
} file_collector_t;

static void file_collector_callback(const char *filename, void *user_data)
{
	file_collector_t *collector = (file_collector_t *) user_data;

	if (collector->count >= collector->capacity)
	{
		int new_cap = collector->capacity * 2;
		char **new_files = realloc(collector->files, new_cap * sizeof(char *));
		if (!new_files)
		{
			return;
		}
		collector->files = new_files;
		collector->capacity = new_cap;
	}

	/* Store full path */
	char full_path[512];
	xlog_snprintf(full_path, sizeof(full_path), "%s%c%s",
	              collector->dir_path, XLOG_PATH_SEP, filename);
	collector->files[collector->count] = str_dup(full_path);
	if (collector->files[collector->count])
	{
		collector->count++;
	}
}

char **rotate_list_archives(const rotate_state *state, int *count_out)
{
	char pattern[128];
	rotate_gen_archive_pattern(pattern, sizeof(pattern), &state->config);

	file_collector_t collector =
			{
					.files = malloc(64 * sizeof(char *)),
					.count = 0,
					.capacity = 64,
					.dir_path = state->dir_path
			};

	if (!collector.files)
	{
		*count_out = 0;
		return NULL;
	}

	xlog_list_files(state->dir_path, pattern, file_collector_callback, &collector);

	/* Sort by name (which sorts by date due to YYYYMMDD format) */
	if (collector.count > 1)
	{
		qsort(collector.files, collector.count, sizeof(char *), archive_name_compare);
	}

	*count_out = collector.count;
	return collector.files;
}

void rotate_free_archive_list(char **list, int count)
{
	if (!list)
	{
		return;
	}
	for (int i = 0; i < count; i++)
	{
		free(list[i]);
	}
	free(list);
}

int rotate_enforce_dir_limit(rotate_state *state)
{
	int deleted = 0;
	int count;
	char **archives = rotate_list_archives(state, &count);

	if (!archives || count == 0)
	{
		rotate_free_archive_list(archives, count);
		return 0;
	}

	/* Check total size and file count limits */
	int64_t total_size = rotate_calc_dir_size(state);
	int total_files = count + 1;  /* archives + current active file */

	/* Delete oldest files until we're under limits */
	int idx = 0;
	while (idx < count &&
	       (total_size > (int64_t) state->config.max_dir_size ||
	        total_files > (int) state->config.max_files))
	{

		int64_t file_size = xlog_file_size(archives[idx]);
		if (xlog_remove(archives[idx]))
		{
			if (file_size > 0)
			{
				total_size -= file_size;
			}
			total_files--;
			deleted++;
			state->files_deleted++;
		}
		idx++;
	}

	rotate_free_archive_list(archives, count);
	return deleted;
}

/**
 * Find next sequence number for archives.
 * Returns the next available sequence number (1, 2, 3, ...)
 * Note: This does NOT check dated archive without sequence.
 */
static int find_next_sequence_number(const rotate_state *state)
{
	char path[512];

	/* Find next sequence number starting from 1 */
	for (int seq = 1; seq <= 99; seq++)
	{
		rotate_gen_sequenced_path(path, sizeof(path), &state->config,
		                          state->current_year, state->current_month,
		                          state->current_day, seq);
		if (!xlog_file_exists(path))
		{
			return seq;
		}
	}

	return 99;  /* Max sequence reached */
}

/**
 * Check if dated archive (without sequence) exists.
 * e.g., pel-20260210.log
 */
static bool dated_archive_exists(const rotate_state *state)
{
	char path[512];
	rotate_gen_dated_path(path, sizeof(path), &state->config,
	                      state->current_year, state->current_month, state->current_day);
	return xlog_file_exists(path);
}

/**
 * Find next sequence number for archives.
 * Returns:
 *   0 - If the dated archive (e.g., pel-20260210.log) doesn't exist yet
 *   N - The next available sequence number
 */
int rotate_find_next_sequence(const rotate_state *state)
{
	if (!dated_archive_exists(state))
	{
		return 0;  /* Dated archive doesn't exist, use it first */
	}

	return find_next_sequence_number(state);
}

/**
 * Normalize dated archive to sequenced format.
 * If pel-20260210.log exists, rename it to pel-20260210-01.log
 * This ensures all archives have consistent naming with sequence numbers.
 */
static bool rotate_normalize_dated_archive(rotate_state *state)
{
	char dated_path[512];
	char sequenced_path[512];

	/* Check if dated archive without sequence exists */
	rotate_gen_dated_path(dated_path, sizeof(dated_path), &state->config,
	                      state->current_year, state->current_month, state->current_day);

	if (!xlog_file_exists(dated_path))
	{
		return true;  /* No dated archive, nothing to do */
	}

	/* Check if pel-20260210-01.log already exists */
	rotate_gen_sequenced_path(sequenced_path, sizeof(sequenced_path), &state->config,
	                          state->current_year, state->current_month,
	                          state->current_day, 1);

	if (xlog_file_exists(sequenced_path))
	{
		return true;  /* Already normalized or 01 exists from other source */
	}

	/* Rename dated archive to -01 */
	if (!xlog_rename(dated_path, sequenced_path))
	{
		return false;
	}

	return true;
}

/* ============================================================================
 * Core Rotation API
 * ============================================================================ */

bool rotate_init(rotate_state *state, const rotate_config *config)
{
	if (!state || !config || !config->base_name || !config->directory)
	{
		return false;
	}

	memset(state, 0, sizeof(rotate_state));

	/* Copy configuration */
	memcpy(&state->config, config, sizeof(rotate_config));

	/* Set defaults if not specified */
	if (state->config.max_file_size == 0)
	{
		state->config.max_file_size = XLOG_ROTATE_DEFAULT_MAX_FILE_SIZE;
	}
	if (state->config.max_dir_size == 0)
	{
		state->config.max_dir_size = XLOG_ROTATE_DEFAULT_MAX_DIR_SIZE;
	}
	if (state->config.max_files == 0)
	{
		state->config.max_files = XLOG_ROTATE_DEFAULT_MAX_FILES;
	}
	if (state->config.extension == NULL)
	{
		state->config.extension = ".log";
	}

	/* Cache paths */
	xlog_strncpy(state->dir_path, config->directory, sizeof(state->dir_path));
	xlog_strncpy(state->base_name, config->base_name, sizeof(state->base_name));
	xlog_strncpy(state->extension, state->config.extension, sizeof(state->extension));

	/* Ensure directory exists */
	if (!xlog_mkdir_p(state->dir_path))
	{
		return false;
	}

	/* Generate current active file path */
	rotate_gen_active_path(state->current_path, sizeof(state->current_path), &state->config);

	/* Initialize date tracking */
	update_current_date(state);

	/* Check for startup rotation */
	if (config->rotate_on_start)
	{
		int64_t existing_size = xlog_file_size(state->current_path);
		if (existing_size > 0)
		{
			/* File exists from previous run, rotate it */
			state->current_size = (uint64_t) existing_size;

			/* Check if we should rotate based on size or date */
			if (existing_size >= (int64_t) state->config.max_file_size)
			{
				rotate_force(state);
			}
		}
	}

	/* Open current file */
	if (!open_current_file(state))
	{
		return false;
	}

	/* Enforce directory limits on startup */
	rotate_enforce_dir_limit(state);

	return true;
}

void rotate_cleanup(rotate_state *state)
{
	if (!state)
	{
		return;
	}

	if (state->fp)
	{
		fflush(state->fp);
		fclose(state->fp);
		state->fp = NULL;
	}
}

bool rotate_needed(rotate_state *state)
{
	if (!state)
	{
		return false;
	}

	/* Check date change */
	if (date_changed(state))
	{
		return true;
	}

	/* Check size limit */
	if (state->current_size >= state->config.max_file_size)
	{
		return true;
	}

	return false;
}

bool rotate_force(rotate_state *state)
{
	if (!state)
	{
		return false;
	}

	char archive_path[512];
	char dated_path[512];
	char sequenced_path[512];

	/* Close current file */
	if (state->fp)
	{
		fflush(state->fp);
		fclose(state->fp);
		state->fp = NULL;
	}

	/* Check if dated archive exists (e.g., pel-20260210.log) */
	bool has_dated_archive = dated_archive_exists(state);

	if (!has_dated_archive)
	{
		/* First archive of the day - use dated name without sequence */
		/* pel.log -> pel-20260210.log */
		rotate_gen_dated_path(archive_path, sizeof(archive_path), &state->config,
		                      state->current_year, state->current_month, state->current_day);
		state->current_sequence = 1;
	}
	else
	{
		/* Dated archive (pel-20260210.log) exists, this is second rotation of the day
		 * Rename it to -01, then archive current file to -02
		 */
		rotate_gen_dated_path(dated_path, sizeof(dated_path), &state->config,
		                      state->current_year, state->current_month, state->current_day);

		/* Check if -01 already exists */
		rotate_gen_sequenced_path(sequenced_path, sizeof(sequenced_path), &state->config,
		                          state->current_year, state->current_month,
		                          state->current_day, 1);

		if (!xlog_file_exists(sequenced_path))
		{
			/* First time: rename pel-20260210.log to pel-20260210-01.log */
			xlog_rename(dated_path, sequenced_path);
		}

		/* Find next available sequence number for current file */
		int seq = find_next_sequence_number(state);

		/* Archive current file to pel-20260210-NN.log */
		rotate_gen_sequenced_path(archive_path, sizeof(archive_path), &state->config,
		                          state->current_year, state->current_month,
		                          state->current_day, seq);
		state->current_sequence = seq + 1;
	}

	/* Rename current file to archive */
	if (xlog_file_exists(state->current_path))
	{
		if (!xlog_rename(state->current_path, archive_path))
		{
			/* Rename failed, try to reopen the original file */
			open_current_file(state);
			return false;
		}

		/* Compress the archived file if configured */
		if (state->config.compress_old)
		{
			/* Use async compression to avoid blocking */
			xlog_compress_task *task = xlog_compress_async(
				archive_path, NULL,
				XLOG_COMPRESS_LEVEL_DEFAULT,
				true  /* delete source after compression */
			);
			/* Fire and forget - compression happens in background */
			/* Note: In production, you might want to track these tasks */
			(void)task;
		}
	}

	/* Update state */
	state->total_rotations++;

	/* Enforce directory limits */
	rotate_enforce_dir_limit(state);

	/* Open new file */
	if (!open_current_file(state))
	{
		return false;
	}

	return true;
}

bool rotate_check_and_rotate(rotate_state *state)
{
	if (!state)
	{
		return false;
	}

	/* Check for date change first */
	if (date_changed(state))
	{
		/* Date changed - update and reset sequence */
		update_current_date(state);
		state->current_sequence = 0;

		/* Rotate current file if it has content */
		if (state->current_size > 0)
		{
			return rotate_force(state);
		}
		return true;
	}

	/* Check size limit */
	if (state->current_size >= state->config.max_file_size)
	{
		return rotate_force(state);
	}

	return true;
}

int64_t rotate_write(rotate_state *state, const char *data, size_t len)
{
	if (!state || !data || len == 0)
	{
		return -1;
	}

	/* Check and rotate if needed */
	if (!rotate_check_and_rotate(state))
	{
		return -1;
	}

	/* Ensure file is open */
	if (!state->fp)
	{
		if (!open_current_file(state))
		{
			return -1;
		}
	}

	/* Write data */
	size_t written = fwrite(data, 1, len, state->fp);
	if (written > 0)
	{
		state->current_size += written;
		state->total_bytes_written += written;
	}

	return (int64_t) written;
}

void rotate_flush(rotate_state *state)
{
	if (state && state->fp)
	{
		fflush(state->fp);
	}
}

const char *rotate_get_current_path(const rotate_state *state)
{
	return state ? state->current_path : NULL;
}

uint64_t rotate_get_current_size(const rotate_state *state)
{
	return state ? state->current_size : 0;
}

