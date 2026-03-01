/* =====================================================================================
 *       Filename:  compress.h
 *    Description:  Log file compression using miniz (gzip format)
 *        Version:  1.0
 *        Created:  2026-03-01
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 *
 * Cross-platform gzip compression for rotated log files.
 * Uses miniz library (single header, no external dependencies).
 *
 * Features:
 *   - Cross-platform (Windows/Linux/macOS)
 *   - No external library dependencies (miniz bundled)
 *   - gzip format output (.gz files)
 *   - Async compression support
 *
 * Note: Compression is disabled in single-header mode (XLOG_SINGLE_HEADER_H)
 *       due to miniz size. Use multi-file build for compression support.
 * =====================================================================================
 */

#ifndef XLOG_COMPRESS_H
#define XLOG_COMPRESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Feature Detection
 * ============================================================================ */

/* Disable compression in single-header mode */
#ifdef XLOG_SINGLE_HEADER_H
#define XLOG_NO_COMPRESS 1
#endif

/* Allow user to explicitly disable compression */
#ifndef XLOG_NO_COMPRESS

/* ============================================================================
 * Configuration
 * ============================================================================ */

/* Compression levels (maps to miniz/zlib levels) */
#define XLOG_COMPRESS_LEVEL_NONE    0   /* No compression, just store */
#define XLOG_COMPRESS_LEVEL_FAST    1   /* Fast compression */
#define XLOG_COMPRESS_LEVEL_DEFAULT 6   /* Default (good balance) */
#define XLOG_COMPRESS_LEVEL_BEST    9   /* Best compression */

/* Buffer sizes */
#define XLOG_COMPRESS_BUFFER_SIZE   (128 * 1024)  /* 128KB I/O buffer */

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum
{
	XLOG_COMPRESS_OK = 0,
	XLOG_COMPRESS_ERR_MEMORY = -1,      /* Memory allocation failed */
	XLOG_COMPRESS_ERR_INPUT = -2,       /* Cannot open input file */
	XLOG_COMPRESS_ERR_OUTPUT = -3,      /* Cannot create output file */
	XLOG_COMPRESS_ERR_READ = -4,        /* Read error */
	XLOG_COMPRESS_ERR_WRITE = -5,       /* Write error */
	XLOG_COMPRESS_ERR_COMPRESS = -6,    /* Compression failed */
	XLOG_COMPRESS_ERR_INVALID = -7      /* Invalid argument */
} xlog_compress_error;

/* ============================================================================
 * Compression Statistics
 * ============================================================================ */

typedef struct xlog_compress_stats
{
	uint64_t original_size;     /* Original file size */
	uint64_t compressed_size;   /* Compressed file size */
	double ratio;             /* Compression ratio (compressed/original) */
	uint64_t time_us;           /* Time taken in microseconds */
} xlog_compress_stats;

/* ============================================================================
 * High-level API - File Compression
 * ============================================================================ */

/**
 * Compress a file to gzip format.
 *
 * @param src_path      Source file path
 * @param dst_path      Destination file path (should end with .gz)
 *                      If NULL, will use src_path + ".gz"
 * @param level         Compression level (0-9, use XLOG_COMPRESS_LEVEL_*)
 * @param delete_src    Delete source file after successful compression
 * @param stats         Optional: compression statistics (can be NULL)
 * @return              XLOG_COMPRESS_OK on success, error code on failure
 */
xlog_compress_error xlog_compress_file(const char *src_path,
                                       const char *dst_path,
                                       int level,
                                       bool delete_src,
                                       xlog_compress_stats *stats);

/**
 * Compress a file to gzip format with default settings.
 * Output file will be src_path + ".gz", source file deleted after success.
 *
 * @param src_path      Source file path
 * @return              XLOG_COMPRESS_OK on success, error code on failure
 */
xlog_compress_error xlog_compress_file_default(const char *src_path);

/**
 * Check if a file is already compressed (has .gz extension or gzip magic).
 *
 * @param path          File path to check
 * @return              true if file appears to be gzip compressed
 */
bool xlog_is_compressed(const char *path);

/**
 * Generate compressed filename by appending .gz extension.
 *
 * @param out           Output buffer
 * @param out_size      Output buffer size
 * @param src_path      Source path
 */
void xlog_compress_gen_path(char *out, size_t out_size, const char *src_path);

/**
 * Get error message for error code.
 *
 * @param err           Error code
 * @return              Human-readable error message
 */
const char *xlog_compress_strerror(xlog_compress_error err);

/* ============================================================================
 * Async Compression API (for background compression)
 * ============================================================================ */

/* Opaque handle for async compression task */
typedef struct xlog_compress_task xlog_compress_task;

/**
 * Start async file compression.
 * The compression runs in a background thread.
 *
 * @param src_path      Source file path
 * @param dst_path      Destination path (NULL for src_path + ".gz")
 * @param level         Compression level
 * @param delete_src    Delete source after compression
 * @return              Task handle, or NULL on failure
 */
xlog_compress_task *xlog_compress_async(const char *src_path,
                                        const char *dst_path,
                                        int level,
                                        bool delete_src);

/**
 * Check if async compression is complete.
 *
 * @param task          Task handle
 * @return              true if compression is complete
 */
bool xlog_compress_is_done(xlog_compress_task *task);

/**
 * Wait for async compression to complete and get result.
 *
 * @param task          Task handle (will be freed)
 * @param stats         Optional: compression statistics
 * @return              Compression result
 */
xlog_compress_error xlog_compress_wait(xlog_compress_task *task,
                                       xlog_compress_stats *stats);

/**
 * Cancel async compression task.
 *
 * @param task          Task handle (will be freed)
 */
void xlog_compress_cancel(xlog_compress_task *task);

#else /* XLOG_NO_COMPRESS */

/* ============================================================================
 * Stub API when compression is disabled
 * ============================================================================ */

#define XLOG_COMPRESS_LEVEL_NONE    0
#define XLOG_COMPRESS_LEVEL_FAST    1
#define XLOG_COMPRESS_LEVEL_DEFAULT 6
#define XLOG_COMPRESS_LEVEL_BEST    9

typedef enum {
	XLOG_COMPRESS_OK = 0,
	XLOG_COMPRESS_ERR_DISABLED = -100
} xlog_compress_error;

typedef struct xlog_compress_stats {
	uint64_t original_size;
	uint64_t compressed_size;
	double   ratio;
	uint64_t time_us;
} xlog_compress_stats;

typedef struct xlog_compress_task xlog_compress_task;

/* Stub functions that do nothing when compression is disabled */
static inline xlog_compress_error xlog_compress_file(const char *src_path,
													 const char *dst_path,
													 int level,
													 bool delete_src,
													 xlog_compress_stats *stats)
{
	(void)src_path; (void)dst_path; (void)level; (void)delete_src; (void)stats;
	return XLOG_COMPRESS_ERR_DISABLED;
}

static inline xlog_compress_error xlog_compress_file_default(const char *src_path)
{
	(void)src_path;
	return XLOG_COMPRESS_ERR_DISABLED;
}

static inline bool xlog_is_compressed(const char *path)
{
	(void)path;
	return false;
}

static inline void xlog_compress_gen_path(char *out, size_t out_size, const char *src_path)
{
	(void)out; (void)out_size; (void)src_path;
}

static inline const char *xlog_compress_strerror(xlog_compress_error err)
{
	(void)err;
	return "Compression disabled";
}

static inline xlog_compress_task *xlog_compress_async(const char *src_path,
													  const char *dst_path,
													  int level,
													  bool delete_src)
{
	(void)src_path; (void)dst_path; (void)level; (void)delete_src;
	return NULL;
}

static inline bool xlog_compress_is_done(xlog_compress_task *task)
{
	(void)task;
	return true;
}

static inline xlog_compress_error xlog_compress_wait(xlog_compress_task *task,
													 xlog_compress_stats *stats)
{
	(void)task; (void)stats;
	return XLOG_COMPRESS_ERR_DISABLED;
}

static inline void xlog_compress_cancel(xlog_compress_task *task)
{
	(void)task;
}

#endif /* XLOG_NO_COMPRESS */

#ifdef __cplusplus
}
#endif

#endif /* XLOG_COMPRESS_H */

