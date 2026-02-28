/* =====================================================================================
 *       Filename:  xlog_formatter.h
 *    Description:  Formatter abstraction for xlog - supports Text, Raw, and JSON formats
 *                  Decouples formatting logic from output (Sink) logic for flexibility.
 *        Version:  1.0
 *        Created:  2026-02-21
 *       Compiler:  gcc/clang (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#ifndef XLOG_FORMATTER_H
#define XLOG_FORMATTER_H

#include <stddef.h>
#include <stdbool.h>

#include "log_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */
typedef struct xlog_formatter xlog_formatter;

/* ============================================================================
 * Format Function Type
 * ============================================================================
 * Converts a log_record to a formatted string in the output buffer.
 *
 * @param formatter  The formatter instance (can access ctx for custom data)
 * @param rec        The log record to format
 * @param buf        Output buffer
 * @param buf_size   Size of output buffer
 * @return           Number of bytes written (not including null terminator),
 *                   or -1 on error
 */
typedef size_t (*format_fn)(xlog_formatter* formatter, const log_record* rec,
                            char* buf, size_t buf_size);

/* ============================================================================
 * Format Type Enumeration
 * ============================================================================ */
typedef enum xlog_format_type
{
	XLOG_FMT_RAW = 0,    /* Raw: only message content, no metadata */
	XLOG_FMT_TEXT = 1,   /* Text: human-readable format with timestamp, level, etc. */
	XLOG_FMT_JSON = 2    /* JSON: structured format for ELK, ClickHouse, etc. */
} xlog_format_type;

/* ============================================================================
 * Formatter Structure
 * ============================================================================
 * Abstraction layer that decouples formatting from output (Sink).
 * Each Sink can have its own formatter, allowing different output formats
 * for console vs file vs network endpoints.
 */
typedef struct xlog_formatter
{
	/* Formatter type identifier */
	xlog_format_type type;

	/* Format function pointer - converts log_record to string */
	format_fn format;

	/* Custom context for formatter-specific data (e.g., JSON schema config) */
	void* ctx;

	/* Whether to append newline automatically */
	bool append_newline;

	/* For JSON formatter: whether to pretty-print */
	bool pretty_print;
} xlog_formatter;

/* ============================================================================
 * Formatter Factory Functions
 * ============================================================================ */

/**
 * Create a generic formatter with custom format function.
 *
 * @param type    Formatter type
 * @param format  Custom format function
 * @param ctx     Custom context (can be NULL)
 * @return        New formatter instance, or NULL on failure
 */
xlog_formatter* xlog_formatter_create(xlog_format_type type, format_fn format, void* ctx);

/**
 * Destroy a formatter and free resources.
 *
 * @param f  Formatter to destroy
 */
void xlog_formatter_destroy(xlog_formatter* f);

/**
 * Create a default text formatter.
 * Uses the current global log pattern (LOG_PATTERN_DEFAULT by default).
 *
 * @return  New text formatter instance
 */
xlog_formatter* xlog_text_formatter_create(void);

/**
 * Create a raw formatter (message only, no metadata).
 * Ideal for high-performance data pipelines where formatting is done externally.
 *
 * @return  New raw formatter instance
 */
xlog_formatter* xlog_raw_formatter_create(void);

/**
 * Create a JSON formatter with default schema.
 * Default fields: timestamp, level, thread_id, file, line, message, fields
 *
 * @return  New JSON formatter instance
 */
xlog_formatter* xlog_json_formatter_create(void);

/* ============================================================================
 * Convenience Format Functions (can be used directly or as format_fn)
 * ============================================================================ */

/**
 * Format as raw message only (no timestamp, level, etc.)
 * Just the interpolated message content.
 */
size_t xlog_format_raw(xlog_formatter* formatter, const log_record* rec,
                       char* buf, size_t buf_size);

/**
 * Format as human-readable text (default format).
 */
size_t xlog_format_text(xlog_formatter* formatter, const log_record* rec,
                        char* buf, size_t buf_size);

/**
 * Format as JSON string.
 * Handles proper escaping of special characters.
 */
size_t xlog_format_json(xlog_formatter* formatter, const log_record* rec,
                        char* buf, size_t buf_size);

/* ============================================================================
 * JSON Utility Functions
 * ============================================================================ */

/**
 * Escape a string for JSON output.
 * Handles: " \ / \b \f \n \r \t and control characters.
 *
 * @param src       Source string
 * @param src_len   Source string length (-1 for null-terminated)
 * @param dst       Destination buffer
 * @param dst_size  Destination buffer size
 * @return          Number of bytes written (not including null terminator),
 *                  or required size if dst_size is 0
 */
size_t xlog_json_escape_string(const char* src, size_t src_len,
                               char* dst, size_t dst_size);

/**
 * SIMD-optimized JSON string escaping (uses SSE4.2/AVX2/NEON when available).
 * Falls back to scalar implementation on unsupported platforms.
 */
size_t xlog_json_escape_string_simd(const char* src, size_t src_len,
                                    char* dst, size_t dst_size);

/* ============================================================================
 * Inline Format Functions (direct use without formatter object)
 * ============================================================================ */

/**
 * Format log record as JSON (inline version).
 *
 * @param rec       Log record to format
 * @param buf       Output buffer
 * @param buf_size  Output buffer size
 * @return          Number of bytes written, or -1 on error
 */
int log_record_format_json_inline(const log_record* rec, char* buf, size_t buf_size);

/**
 * Format log record as raw message only (inline version).
 *
 * @param rec       Log record to format
 * @param buf       Output buffer
 * @param buf_size  Output buffer size
 * @return          Number of bytes written, or -1 on error
 */
int log_record_format_raw_inline(const log_record* rec, char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* XLOG_FORMATTER_H */
