/* =====================================================================================
 *       Filename:  rotate.h
 *    Description:  Log rotation strategy with size/date/directory limit support
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#ifndef XLOG_ROTATE_H
#define XLOG_ROTATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Rotation Strategy Configuration
 * ============================================================================
 *
 * Example: pel.log with date 20260209
 *   - Base name: pel
 *   - Extension: .log
 *   - Max single file size: 50MB
 *   - Max total directory size: 500MB
 *
 * Rotation naming:
 *   pel.log              (current active file)
 *   pel-20260209.log     (first archive of the day)
 *   pel-20260209-01.log  (second archive when size exceeded)
 *   pel-20260209-02.log  (third archive)
 *   pel-20260208.log     (previous day's archive)
 *   ...
 */

/* Size constants */
#define XLOG_KB (1024ULL)
#define XLOG_MB (1024ULL * 1024ULL)
#define XLOG_GB (1024ULL * 1024ULL * 1024ULL)

/* Default limits */
#define XLOG_ROTATE_DEFAULT_MAX_FILE_SIZE   (50 * XLOG_MB)    /* 50 MB per file */
#define XLOG_ROTATE_DEFAULT_MAX_DIR_SIZE    (500 * XLOG_MB)   /* 500 MB total */
#define XLOG_ROTATE_DEFAULT_MAX_FILES       100               /* Max archived files */

/* ============================================================================
 * Rotation Configuration
 * ============================================================================ */

typedef struct rotate_config
{
	/* File naming */
	const char *base_name;         /* Base name without extension (e.g., "pel") */
	const char *extension;         /* File extension with dot (e.g., ".log") */
	const char *directory;         /* Log directory path */

	/* Size limits */
	uint64_t max_file_size;      /* Max size per file before rotation (bytes) */
	uint64_t max_dir_size;       /* Max total size of all log files (bytes) */
	uint32_t max_files;          /* Max number of archived files to keep */

	/* Behavior flags */
	bool rotate_on_start;    /* Check and rotate on startup */
	bool compress_old;       /* Compress old files (not implemented yet) */
} rotate_config;

/* ============================================================================
 * Rotation State
 * ============================================================================ */

typedef struct rotate_state
{
	/* Current file info */
	char current_path[512];  /* Full path to current log file */
	FILE *fp;                /* Current file handle */
	uint64_t current_size;       /* Current file size in bytes */

	/* Date tracking */
	int current_year;       /* Current year (e.g., 2026) */
	int current_month;      /* Current month (1-12) */
	int current_day;        /* Current day (1-31) */
	int current_sequence;   /* Current sequence number for today */

	/* Configuration (copy) */
	rotate_config config;

	/* Cached paths */
	char dir_path[256];      /* Directory path */
	char base_name[64];      /* Base name */
	char extension[16];      /* Extension */

	/* Statistics */
	uint64_t total_rotations;    /* Total number of rotations */
	uint64_t total_bytes_written;/* Total bytes written */
	uint64_t files_deleted;      /* Files deleted due to limits */
} rotate_state;

/* ============================================================================
 * Core Rotation API
 * ============================================================================ */

/**
 * Initialize rotation state with configuration.
 * This will also check if rotation is needed on startup.
 *
 * @param state     Rotation state to initialize
 * @param config    Configuration
 * @return          true on success, false on failure
 */
bool rotate_init(rotate_state *state, const rotate_config *config);

/**
 * Cleanup rotation state and close any open files.
 *
 * @param state     Rotation state
 */
void rotate_cleanup(rotate_state *state);

/**
 * Check if rotation is needed (based on size or date change).
 * Does NOT perform the rotation.
 *
 * @param state     Rotation state
 * @return          true if rotation is needed
 */
bool rotate_needed(rotate_state *state);

/**
 * Perform rotation if needed.
 *
 * @param state     Rotation state
 * @return          true if rotation was performed or not needed, false on error
 */
bool rotate_check_and_rotate(rotate_state *state);

/**
 * Force rotation regardless of size/date.
 *
 * @param state     Rotation state
 * @return          true on success, false on failure
 */
bool rotate_force(rotate_state *state);

/**
 * Write data to the log file, rotating if necessary.
 *
 * @param state     Rotation state
 * @param data      Data to write
 * @param len       Length of data
 * @return          Number of bytes written, or -1 on error
 */
int64_t rotate_write(rotate_state *state, const char *data, size_t len);

/**
 * Flush the current log file.
 *
 * @param state     Rotation state
 */
void rotate_flush(rotate_state *state);

/**
 * Get the current log file path.
 *
 * @param state     Rotation state
 * @return          Path to current log file
 */
const char *rotate_get_current_path(const rotate_state *state);

/**
 * Get the current log file size.
 *
 * @param state     Rotation state
 * @return          Current file size in bytes
 */
uint64_t rotate_get_current_size(const rotate_state *state);

/* ============================================================================
 * Path Generation Functions
 * ============================================================================ */

/**
 * Generate the active log file path.
 * Format: {directory}/{base_name}{extension}
 * Example: /var/log/pel.log
 *
 * @param out       Output buffer
 * @param out_size  Output buffer size
 * @param config    Configuration
 */
void rotate_gen_active_path(char *out, size_t out_size, const rotate_config *config);

/**
 * Generate a dated archive path (no sequence number).
 * Format: {directory}/{base_name}-{YYYYMMDD}{extension}
 * Example: /var/log/pel-20260209.log
 *
 * @param out       Output buffer
 * @param out_size  Output buffer size
 * @param config    Configuration
 * @param year      Year
 * @param month     Month (1-12)
 * @param day       Day (1-31)
 */
void rotate_gen_dated_path(char *out, size_t out_size, const rotate_config *config,
                           int year, int month, int day);

/**
 * Generate a dated archive path with sequence number.
 * Format: {directory}/{base_name}-{YYYYMMDD}-{NN}{extension}
 * Example: /var/log/pel-20260209-01.log
 *
 * @param out       Output buffer
 * @param out_size  Output buffer size
 * @param config    Configuration
 * @param year      Year
 * @param month     Month (1-12)
 * @param day       Day (1-31)
 * @param sequence  Sequence number (1-99)
 */
void rotate_gen_sequenced_path(char *out, size_t out_size, const rotate_config *config,
                               int year, int month, int day, int sequence);

/**
 * Generate a glob pattern to match all archived files.
 * Format: {base_name}-*{extension}
 * Example: pel-*.log
 *
 * @param out       Output buffer
 * @param out_size  Output buffer size
 * @param config    Configuration
 */
void rotate_gen_archive_pattern(char *out, size_t out_size, const rotate_config *config);

/* ============================================================================
 * Directory Management Functions
 * ============================================================================ */

/**
 * Calculate total size of all log files in directory.
 *
 * @param state     Rotation state
 * @return          Total size in bytes, or -1 on error
 */
int64_t rotate_calc_dir_size(const rotate_state *state);

/**
 * Enforce directory size limit by deleting oldest files.
 *
 * @param state     Rotation state
 * @return          Number of files deleted
 */
int rotate_enforce_dir_limit(rotate_state *state);

/**
 * Find the next available sequence number for today.
 *
 * @param state     Rotation state
 * @return          Next sequence number (0 if dated file doesn't exist yet)
 */
int rotate_find_next_sequence(const rotate_state *state);

/**
 * Get a list of all archived files sorted by date (oldest first).
 * Caller must free the returned array.
 *
 * @param state         Rotation state
 * @param count_out     Output: number of files
 * @return              Array of file paths (caller must free each and the array)
 */
char **rotate_list_archives(const rotate_state *state, int *count_out);

/**
 * Free the list returned by rotate_list_archives.
 *
 * @param list      File list
 * @param count     Number of files
 */
void rotate_free_archive_list(char **list, int count);

#ifdef __cplusplus
}
#endif

#endif /* XLOG_ROTATE_H */

