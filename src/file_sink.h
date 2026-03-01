/* =====================================================================================
 *       Filename:  file_sink.h
 *    Description:  File sink with advanced log rotation support
 *        Version:  2.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#ifndef XLOG_FILE_SINK_H
#define XLOG_FILE_SINK_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "sink.h"
#include "rotate.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * File Sink Configuration (using rotate module)
 * ============================================================================
 *
 * Example: Create a file sink for /var/log/pel.log
 *
 *   file_sink_config config = {
 *       .directory = "/var/log",
 *       .base_name = "pel",
 *       .extension = ".log",
 *       .max_file_size = 50 * XLOG_MB,    // 50 MB per file
 *       .max_dir_size = 500 * XLOG_MB,    // 500 MB total
 *       .max_files = 100,
 *       .rotate_on_start = true,
 *       .flush_on_write = false
 *   };
 *   sink_t *sink = file_sink_create(&config, LOG_LEVEL_INFO);
 *
 * Archive naming:
 *   pel.log              (current active file)
 *   pel-20260209.log     (first archive of the day)
 *   pel-20260209-01.log  (second archive when size exceeded)
 *   pel-20260209-02.log  (third archive)
 */

typedef struct file_sink_config
{
	/* File naming */
	const char *directory;         /* Log directory path */
	const char *base_name;         /* Base name without extension (e.g., "pel") */
	const char *extension;         /* File extension with dot (e.g., ".log"), default ".log" */

	/* Size limits */
	uint64_t max_file_size;      /* Max size per file (bytes), default 50MB */
	uint64_t max_dir_size;       /* Max total directory size (bytes), default 500MB */
	uint32_t max_files;          /* Max number of archived files, default 100 */

	/* Behavior */
	bool rotate_on_start;    /* Check and rotate on startup, default true */
	bool flush_on_write;     /* Flush after each write, default false */
	bool compress_old;       /* Compress rotated files (gzip), default false */
} file_sink_config;

/* Default values for config */
#define FILE_SINK_V2_DEFAULT_MAX_FILE_SIZE  (50 * XLOG_MB)
#define FILE_SINK_V2_DEFAULT_MAX_DIR_SIZE   (500 * XLOG_MB)
#define FILE_SINK_V2_DEFAULT_MAX_FILES      100

/* ============================================================================
 * File Sink API
 * ============================================================================ */

/**
 * Create a file sink with advanced rotation support.
 *
 * @param config    Configuration
 * @param level     Minimum log level for this sink
 * @return          Pointer to created sink, or NULL on failure
 */
sink_t *file_sink_create(const file_sink_config *config, log_level level);

/**
 * Create a simple file sink with sensible defaults.
 * Uses: 50MB per file, 500MB total, 100 max files, rotate on start.
 *
 * @param directory Log directory path
 * @param base_name Base name (e.g., "app" for app.log)
 * @param level     Minimum log level
 * @return          Pointer to created sink, or NULL on failure
 */
sink_t *file_sink_create_default(const char *directory, const char *base_name, log_level level);

/**
 * Create a file sink with custom size limits.
 *
 * @param directory     Log directory path
 * @param base_name     Base name
 * @param max_file_size Max size per file in bytes
 * @param max_dir_size  Max total directory size in bytes
 * @param level         Minimum log level
 * @return              Pointer to created sink, or NULL on failure
 */
sink_t *file_sink_create_with_limits(const char *directory, const char *base_name,
                                     uint64_t max_file_size, uint64_t max_dir_size,
                                     log_level level);

/**
 * Force a log rotation on the file sink.
 *
 * @param sink      The file sink
 * @return          true on success
 */
bool file_sink_force_rotate(sink_t *sink);

/**
 * Get the current log file path.
 *
 * @param sink      The file sink
 * @return          Path string, or NULL on error
 */
const char *file_sink_get_path(sink_t *sink);

/**
 * Get the current file size.
 *
 * @param sink      The file sink
 * @return          Current size in bytes
 */
uint64_t file_sink_get_size(sink_t *sink);

/**
 * Get rotation statistics.
 *
 * @param sink              The file sink
 * @param total_rotations   Output: total rotation count
 * @param total_bytes       Output: total bytes written
 * @param files_deleted     Output: files deleted due to limits
 */
void file_sink_get_stats(sink_t *sink, uint64_t *total_rotations,
                         uint64_t *total_bytes, uint64_t *files_deleted);

/* ============================================================================
 * Convenience API
 * ============================================================================ */

/**
 * Create a file sink from a simple path (convenience wrapper).
 * Parses path to extract directory, base name, and extension.
 * Uses default rotation settings.
 *
 * @param path      Full path to log file (e.g., "/var/log/app.log")
 * @param level     Minimum log level
 * @return          Pointer to created sink, or NULL on failure
 */
sink_t *file_sink_create_simple(const char *path, log_level level);

#ifdef __cplusplus
}
#endif

#endif /* XLOG_FILE_SINK_H */

