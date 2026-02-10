/* =====================================================================================
 *       Filename:  xlog_core.h
 *    Description:  Internal xlog core implementation
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
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

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Configuration (Low-level, internal use)
 * ============================================================================ */
typedef struct xlog_config
{
	size_t queue_capacity;         /* Ring buffer capacity (default: 65536) */
	size_t format_buffer_size;     /* Format buffer size (default: 4096) */
	log_level min_level;              /* Minimum log level (default: DEBUG) */
	bool async;                  /* Async mode (default: true) */
	bool auto_flush;             /* Auto flush (default: false) */
	uint32_t batch_size;             /* Batch size (default: 64) */
	uint64_t flush_interval_ms;      /* Flush interval (default: 1000) */
} xlog_config;

#define XLOG_DEFAULT_QUEUE_CAPACITY     65536
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

/* Core API declarations - skip if public API already declared them */
#ifndef XLOG_H

bool xlog_init(void);

void xlog_shutdown(void);

bool xlog_is_initialized(void);

void xlog_flush(void);

void xlog_set_level(log_level level);

log_level xlog_get_level(void);

bool xlog_level_enabled(log_level level);

void xlog_get_stats(xlog_stats *stats);

void xlog_reset_stats(void);

bool xlog_add_sink(sink_t *sink);

bool xlog_remove_sink(sink_t *sink);

size_t xlog_sink_count(void);

#endif /* XLOG_H */

void xlog_log(log_level level, const char *file, uint32_t line,
              const char *func, const char *fmt, ...);

void xlog_log_ctx(log_level level, const log_context *ctx,
                  const char *file, uint32_t line, const char *func,
                  const char *fmt, ...);

bool xlog_submit(log_record *record);

/* ============================================================================
 * Logging Macros
 * ============================================================================ */
#define XLOG_TRACE(fmt, ...) \
    xlog_log(LOG_LEVEL_TRACE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define XLOG_DEBUG(fmt, ...) \
    xlog_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define XLOG_INFO(fmt, ...) \
    xlog_log(LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define XLOG_WARN(fmt, ...) \
    xlog_log(LOG_LEVEL_WARNING, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define XLOG_ERROR(fmt, ...) \
    xlog_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define XLOG_FATAL(fmt, ...) \
    xlog_log(LOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define XLOG_TRACE_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(LOG_LEVEL_TRACE)) \
        XLOG_TRACE(fmt, ##__VA_ARGS__); } while(0)
#define XLOG_DEBUG_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(LOG_LEVEL_DEBUG)) \
        XLOG_DEBUG(fmt, ##__VA_ARGS__); } while(0)
#define XLOG_INFO_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(LOG_LEVEL_INFO)) \
        XLOG_INFO(fmt, ##__VA_ARGS__); } while(0)
#define XLOG_WARN_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(LOG_LEVEL_WARNING)) \
        XLOG_WARN(fmt, ##__VA_ARGS__); } while(0)
#define XLOG_ERROR_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(LOG_LEVEL_ERROR)) \
        XLOG_ERROR(fmt, ##__VA_ARGS__); } while(0)
#define XLOG_FATAL_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(LOG_LEVEL_FATAL)) \
        XLOG_FATAL(fmt, ##__VA_ARGS__); } while(0)

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

#define LOG_TRACE(fmt, ...) XLOG_TRACE(fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) XLOG_DEBUG(fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  XLOG_INFO(fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  XLOG_WARN(fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) XLOG_ERROR(fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) XLOG_FATAL(fmt, ##__VA_ARGS__)

#define LOG_TRACE_IF(cond, fmt, ...) XLOG_TRACE_IF(cond, fmt, ##__VA_ARGS__)
#define LOG_DEBUG_IF(cond, fmt, ...) XLOG_DEBUG_IF(cond, fmt, ##__VA_ARGS__)
#define LOG_INFO_IF(cond, fmt, ...)  XLOG_INFO_IF(cond, fmt, ##__VA_ARGS__)
#define LOG_WARN_IF(cond, fmt, ...)  XLOG_WARN_IF(cond, fmt, ##__VA_ARGS__)
#define LOG_ERROR_IF(cond, fmt, ...) XLOG_ERROR_IF(cond, fmt, ##__VA_ARGS__)
#define LOG_FATAL_IF(cond, fmt, ...) XLOG_FATAL_IF(cond, fmt, ##__VA_ARGS__)

#endif /* XLOG_NO_LEGACY_MACROS */

#ifdef __cplusplus
}
#endif
#endif /* XLOG_CORE_H */
