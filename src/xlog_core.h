/* =====================================================================================
 *       Filename:  xlog_core.h
 *    Description:  Internal xlog core implementation
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */
#ifndef XLOG_CORE_H
#define XLOG_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "level.h"
#include "sink.h"
#include "log_record.h"
#include "ringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Format Style (for JSON, etc.)
 * ============================================================================ */
typedef enum xlog_output_format
{
	XLOG_OUTPUT_DEFAULT = 0, /* [time  level  T:tid  file:line] message */
	XLOG_OUTPUT_SIMPLE,      /* [time  level] message */
	XLOG_OUTPUT_DETAILED,    /* [time  level  T:tid  module#tag  trace  file:line] message */
	XLOG_OUTPUT_JSON,        /* {"timestamp":"...", "level":"...", "message":"..."} */
	XLOG_OUTPUT_RAW,         /* Only message content, no metadata */
	XLOG_OUTPUT_CUSTOM       /* User-supplied format function (config.custom_format) */
} xlog_output_format;

/* Custom format function: renders one log_record into buf (NUL-terminated).
 * Runs on the backend thread (or the logging thread in sync mode), so it
 * must be thread-safe against itself only if sinks format concurrently -
 * in practice it is called serially, like the built-in formatters.
 * Return value: bytes written excluding NUL. Return 0 to drop the record
 * (counted in format_errors stats).
 * The record pointer is only valid for the duration of the call; strings
 * inside it (fmt, loc.file, ctx strings, STR_INLINE args) must not be kept. */
typedef size_t (*xlog_custom_format_fn)(const log_record *rec, void *ctx,
                                        char *buf, size_t buf_size);

/* ============================================================================
 * Core Configuration (Low-level, internal use)
 * ============================================================================ */
typedef struct xlog_config
{
	size_t queue_capacity;         /* Ring buffer capacity (default: 65536) */
	size_t format_buffer_size;     /* Format buffer size (default: 4096) */
	xlog_level min_level;              /* Minimum log level (default: DEBUG) */
	bool async;                  /* Async mode (default: true) */
  rb_full_policy queue_full_policy;  /* Full queue behavior */
  uint64_t queue_spin_timeout_ns;    /* Spin timeout for async/full queue */
  uint64_t queue_block_timeout_ns;   /* Block timeout for full queue */
	bool auto_flush;             /* Auto flush (default: false) */
	uint32_t batch_size;             /* Batch size (default: 64) */
	uint64_t flush_interval_ms;      /* Flush interval (default: 1000) */
	xlog_output_format format_style;  /* Output format (default: DEFAULT) */
	xlog_custom_format_fn custom_format;     /* Used when format_style == XLOG_OUTPUT_CUSTOM */
	void *custom_format_ctx;                 /* Opaque context passed to custom_format */
} xlog_config;

#define XLOG_DEFAULT_QUEUE_CAPACITY     8192
#define XLOG_DEFAULT_FORMAT_BUF_SIZE    4096
#define XLOG_DEFAULT_BATCH_SIZE         64
#define XLOG_DEFAULT_FLUSH_INTERVAL_MS  1000

/* xlog_stats - skip if public API already defined it */
#ifndef XLOG_H
typedef struct xlog_stats
{
	uint64_t logged;
	uint64_t dropped;
	uint64_t processed;
	uint64_t flushed;
	uint64_t format_errors;
} xlog_stats;
#endif /* XLOG_H */

/* Internal API - always declared (needed by implementation) */
bool xlog_init_with_config(const xlog_config *config);

/* Color and sink type configuration */
void xlog_set_console_colors(bool enable);

void xlog_set_has_file_sink(bool has_file);

/* Format style configuration */
void xlog_set_format_style(xlog_output_format style);

/* Set/replace the custom format function (activates XLOG_OUTPUT_CUSTOM).
 * Passing NULL clears it; a NULL function with CUSTOM style counts every
 * record as a format error. Not thread-safe against concurrent logging. */
void xlog_set_custom_format(xlog_custom_format_fn fn, void *ctx);

/* Core API declarations - skip if public API already declared them */
#ifndef XLOG_H

bool xlog_init(void);

void xlog_shutdown(void);

bool xlog_is_initialized(void);

void xlog_flush(void);

void xlog_set_level(xlog_level level);

xlog_level xlog_get_level(void);

bool xlog_level_enabled(xlog_level level);

/* doc166 §3a:与公共头同式的判级快路径。amalgamation 下 PART 1 已定义;
 * 此处兜底以支持仅含内部头的构建。 */
#ifndef XLOG_LEVEL_ENABLED_FAST
extern volatile int xlog_min_level_fast;
#define XLOG_LEVEL_ENABLED_FAST(level) ((int)(level) >= xlog_min_level_fast)
#endif

void xlog_get_stats(xlog_stats *stats);

void xlog_reset_stats(void);

bool xlog_add_sink(sink_t *sink);

bool xlog_remove_sink(sink_t *sink);

size_t xlog_sink_count(void);

/* Thread-local ambient trace context (see include/xlog.h for semantics) */
void xlog_trace_set(uint64_t trace_hi, uint64_t trace_lo,
                    uint64_t span_id, uint64_t parent_span_id);

void xlog_trace_clear(void);

bool xlog_trace_get(uint64_t *trace_hi, uint64_t *trace_lo,
                    uint64_t *span_id, uint64_t *parent_span_id);

#endif /* XLOG_H */

void xlog_log(xlog_level level, const char *file, uint32_t line,
              const char *func, const char *fmt, ...);

void xlog_log_ctx(xlog_level level, const log_context *ctx,
                  const char *file, uint32_t line, const char *func,
                  const char *fmt, ...);

bool xlog_submit(log_record *record);

/* ============================================================================
 * Logging Macros (skip if public API already defined them)
 * ============================================================================ */
#ifndef XLOG_TRACE
#define XLOG_TRACE(fmt, ...) \
    xlog_log(LOG_LEVEL_TRACE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif
#ifndef XLOG_DEBUG
#define XLOG_DEBUG(fmt, ...) \
    xlog_log(XLOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif
#ifndef XLOG_INFO
#define XLOG_INFO(fmt, ...) \
    xlog_log(XLOG_LEVEL_INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif
#ifndef XLOG_WARN
#define XLOG_WARN(fmt, ...) \
    xlog_log(XLOG_LEVEL_WARNING, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif
#ifndef XLOG_ERROR
#define XLOG_ERROR(fmt, ...) \
    xlog_log(XLOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif
#ifndef XLOG_FATAL
#define XLOG_FATAL(fmt, ...) \
    xlog_log(LOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif

#ifndef XLOG_TRACE_IF
#define XLOG_TRACE_IF(cond, fmt, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(LOG_LEVEL_TRACE)) \
        XLOG_TRACE(fmt, ##__VA_ARGS__); } while(0)
#endif
#ifndef XLOG_DEBUG_IF
#define XLOG_DEBUG_IF(cond, fmt, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(LOG_LEVEL_DEBUG)) \
        XLOG_DEBUG(fmt, ##__VA_ARGS__); } while(0)
#endif
#ifndef XLOG_INFO_IF
#define XLOG_INFO_IF(cond, fmt, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(LOG_LEVEL_INFO)) \
        XLOG_INFO(fmt, ##__VA_ARGS__); } while(0)
#endif
#ifndef XLOG_WARN_IF
#define XLOG_WARN_IF(cond, fmt, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(LOG_LEVEL_WARNING)) \
        XLOG_WARN(fmt, ##__VA_ARGS__); } while(0)
#endif
#ifndef XLOG_ERROR_IF
#define XLOG_ERROR_IF(cond, fmt, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(LOG_LEVEL_ERROR)) \
        XLOG_ERROR(fmt, ##__VA_ARGS__); } while(0)
#endif
#ifndef XLOG_FATAL_IF
#define XLOG_FATAL_IF(cond, fmt, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(LOG_LEVEL_FATAL)) \
        XLOG_FATAL(fmt, ##__VA_ARGS__); } while(0)
#endif

#define XLOG_CTX(level, ctx, fmt, ...) \
    xlog_log_ctx(level, ctx, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define XLOG_MODULE(level, module_name, fmt, ...) \
    do { \
        log_context _ctx = {.module = (module_name), .flags = LOG_CTX_HAS_MODULE}; \
        XLOG_CTX(level, &_ctx, fmt, ##__VA_ARGS__); \
    } while(0)
#define XLOG_TAG(level, tag_name, fmt, ...) \
    do { \
        log_context _ctx = {.tag = (tag_name), .flags = LOG_CTX_HAS_TAG}; \
        XLOG_CTX(level, &_ctx, fmt, ##__VA_ARGS__); \
    } while(0)

/* ============================================================================
 * Legacy Macro Compatibility (LOG_* -> XLOG_*)
 * ============================================================================ */
#ifndef XLOG_NO_LEGACY_MACROS

#ifndef LOG_TRACE
#define LOG_TRACE(fmt, ...) XLOG_TRACE(fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG(fmt, ...) XLOG_DEBUG(fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...)  XLOG_INFO(fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(fmt, ...)  XLOG_WARN(fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) XLOG_ERROR(fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_FATAL
#define LOG_FATAL(fmt, ...) XLOG_FATAL(fmt, ##__VA_ARGS__)
#endif

#ifndef LOG_TRACE_IF
#define LOG_TRACE_IF(cond, fmt, ...) XLOG_TRACE_IF(cond, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_DEBUG_IF
#define LOG_DEBUG_IF(cond, fmt, ...) XLOG_DEBUG_IF(cond, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_INFO_IF
#define LOG_INFO_IF(cond, fmt, ...)  XLOG_INFO_IF(cond, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_WARN_IF
#define LOG_WARN_IF(cond, fmt, ...)  XLOG_WARN_IF(cond, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR_IF
#define LOG_ERROR_IF(cond, fmt, ...) XLOG_ERROR_IF(cond, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_FATAL_IF
#define LOG_FATAL_IF(cond, fmt, ...) XLOG_FATAL_IF(cond, fmt, ##__VA_ARGS__)
#endif

#endif /* XLOG_NO_LEGACY_MACROS */

#ifdef __cplusplus
}
#endif
#endif /* XLOG_CORE_H */
