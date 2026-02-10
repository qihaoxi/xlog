/* =====================================================================================
 *       Filename:  batch_writer.h
 *    Description:  High-performance batch writer for file I/O
 *                  Buffers multiple log entries before flushing to disk
 *                  Cross-platform support: Linux, macOS, Windows
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#ifndef XLOG_BATCH_WRITER_H
#define XLOG_BATCH_WRITER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

/* Default batch buffer size (4KB - optimal for most file systems) */
#define XLOG_BATCH_DEFAULT_SIZE         (4 * 1024)

/* Maximum batch buffer size (64KB) */
#define XLOG_BATCH_MAX_SIZE             (64 * 1024)

/* Minimum batch buffer size (512 bytes) */
#define XLOG_BATCH_MIN_SIZE             512

/* Default flush threshold (buffer 80% full triggers flush) */
#define XLOG_BATCH_FLUSH_THRESHOLD      0.8

/* Default max pending entries before forced flush */
#define XLOG_BATCH_MAX_PENDING          64

/* ============================================================================
 * Batch Writer Statistics
 * ============================================================================ */

typedef struct batch_writer_stats
{
	uint64_t bytes_written;         /* Total bytes written to buffer */
	uint64_t bytes_flushed;         /* Total bytes flushed to file */
	uint64_t entries_written;       /* Total entries written */
	uint64_t flush_count;           /* Number of flush operations */
	uint64_t forced_flushes;        /* Flushes due to buffer full */
	uint64_t auto_flushes;          /* Flushes due to threshold/timeout */
	uint64_t write_errors;          /* Number of write errors */
} batch_writer_stats;

/* ============================================================================
 * Batch Writer Configuration
 * ============================================================================ */

typedef struct batch_writer_config
{
	size_t buffer_size;        /* Size of the batch buffer */
	float flush_threshold;    /* Flush when buffer reaches this ratio (0.0-1.0) */
	uint32_t max_pending;        /* Max entries before forced flush */
	uint32_t flush_timeout_ms;   /* Max time before auto flush (0 = disabled) */
	bool use_direct_io;      /* Use O_DIRECT on Linux (bypasses page cache) */
	bool use_write_combine;  /* Combine small writes into single syscall */
} batch_writer_config;

/* Default configuration */
#define BATCH_WRITER_CONFIG_DEFAULT { \
    .buffer_size = XLOG_BATCH_DEFAULT_SIZE, \
    .flush_threshold = XLOG_BATCH_FLUSH_THRESHOLD, \
    .max_pending = XLOG_BATCH_MAX_PENDING, \
    .flush_timeout_ms = 100, \
    .use_direct_io = false, \
    .use_write_combine = true \
}

/* ============================================================================
 * Batch Writer Handle
 * ============================================================================ */

typedef struct batch_writer batch_writer;

/* ============================================================================
 * Batch Writer API
 * ============================================================================ */

/**
 * Create a new batch writer.
 *
 * @param fp        File pointer to write to (must be opened in binary mode)
 * @param config    Configuration (NULL for defaults)
 * @return          Batch writer handle, or NULL on failure
 */
batch_writer *batch_writer_create(FILE *fp, const batch_writer_config *config);

/**
 * Create a batch writer with default configuration.
 *
 * @param fp        File pointer to write to
 * @return          Batch writer handle, or NULL on failure
 */
batch_writer *batch_writer_create_default(FILE *fp);

/**
 * Destroy a batch writer (flushes remaining data first).
 *
 * @param writer    Batch writer handle
 */
void batch_writer_destroy(batch_writer *writer);

/**
 * Write data to the batch buffer.
 * Data is buffered until flush threshold is reached or flush() is called.
 *
 * @param writer    Batch writer handle
 * @param data      Data to write
 * @param len       Length of data
 * @return          Number of bytes written (buffered), or -1 on error
 */
ssize_t batch_writer_write(batch_writer *writer, const char *data, size_t len);

/**
 * Write formatted string to the batch buffer.
 *
 * @param writer    Batch writer handle
 * @param fmt       Format string
 * @param ...       Format arguments
 * @return          Number of bytes written, or -1 on error
 */
int batch_writer_printf(batch_writer *writer, const char *fmt, ...);

/**
 * Flush the batch buffer to the underlying file.
 *
 * @param writer    Batch writer handle
 * @return          true on success, false on error
 */
bool batch_writer_flush(batch_writer *writer);

/**
 * Check if the buffer should be flushed (threshold reached).
 *
 * @param writer    Batch writer handle
 * @return          true if flush is recommended
 */
bool batch_writer_should_flush(batch_writer *writer);

/**
 * Get current buffer usage.
 *
 * @param writer    Batch writer handle
 * @return          Number of bytes currently in buffer
 */
size_t batch_writer_pending(batch_writer *writer);

/**
 * Get buffer capacity.
 *
 * @param writer    Batch writer handle
 * @return          Total buffer capacity
 */
size_t batch_writer_capacity(batch_writer *writer);

/**
 * Get statistics.
 *
 * @param writer    Batch writer handle
 * @param stats     Output statistics structure
 */
void batch_writer_get_stats(batch_writer *writer, batch_writer_stats *stats);

/**
 * Reset statistics.
 *
 * @param writer    Batch writer handle
 */
void batch_writer_reset_stats(batch_writer *writer);

/**
 * Update the file pointer (e.g., after log rotation).
 *
 * @param writer    Batch writer handle
 * @param fp        New file pointer
 * @return          true on success
 */
bool batch_writer_set_file(batch_writer *writer, FILE *fp);

/**
 * Reserve space in the buffer without writing.
 * Returns a pointer to the reserved space for direct writing.
 * Must call batch_writer_commit() after writing to the reserved space.
 *
 * @param writer    Batch writer handle
 * @param len       Number of bytes to reserve
 * @return          Pointer to reserved space, or NULL if insufficient space
 */
char *batch_writer_reserve(batch_writer *writer, size_t len);

/**
 * Commit previously reserved space.
 *
 * @param writer    Batch writer handle
 * @param len       Actual number of bytes written (may be <= reserved)
 */
void batch_writer_commit(batch_writer *writer, size_t len);

/* ============================================================================
 * Advanced: Direct I/O (Linux-specific, requires aligned buffers)
 * ============================================================================ */

#ifdef XLOG_PLATFORM_LINUX

/**
 * Create a batch writer with direct I/O (O_DIRECT).
 * Bypasses the page cache for better performance with large writes.
 * Note: Buffer must be aligned to 512 bytes.
 *
 * @param path      File path
 * @param config    Configuration
 * @return          Batch writer handle, or NULL on failure
 */
batch_writer *batch_writer_create_direct(const char *path,
                                         const batch_writer_config *config);

#endif /* XLOG_PLATFORM_LINUX */

/* ============================================================================
 * Advanced: Memory-mapped I/O
 * ============================================================================ */

/**
 * Create a batch writer using memory-mapped I/O.
 * Can provide better performance for sequential writes.
 *
 * @param path      File path
 * @param initial_size  Initial file size
 * @param config    Configuration
 * @return          Batch writer handle, or NULL on failure
 */
batch_writer *batch_writer_create_mmap(const char *path,
                                       size_t initial_size,
                                       const batch_writer_config *config);

#ifdef __cplusplus
}
#endif

#endif /* XLOG_BATCH_WRITER_H */

