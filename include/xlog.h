/* =====================================================================================
 *       Filename:  xlog.h
 *    Description:  xlog - High Performance Async Logging Library for C
 *                  Single header API - include this file only
 *        Version:  1.0.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 *
 * QUICK START
 * ===========
 *
 *   #include <xlog.h>
 *
 *   int main(void) {
 *       // Option 1: Simple console logging
 *       xlog_init_console(XLOG_LEVEL_DEBUG);
 *
 *       // Option 2: Console + file logging
 *       xlog_init_file("./logs", "myapp", XLOG_LEVEL_INFO);
 *
 *       // Option 3: Full control with builder
 *       xlog_builder *cfg = xlog_builder_new();
 *       xlog_builder_set_name(cfg, "myapp");
 *       xlog_builder_enable_file(cfg, true);
 *       xlog_builder_file_directory(cfg, "./logs");
 *       xlog_builder_file_max_size(cfg, 50 * XLOG_1MB);
 *       xlog_builder_apply(cfg);
 *       xlog_builder_free(cfg);
 *
 *       // Use logging
 *       LOG_DEBUG("Debug message");
 *       LOG_INFO("User %s logged in", "john");
 *       LOG_ERROR("Failed: %d", errno);
 *
 *       xlog_shutdown();
 *       return 0;
 *   }
 *
 * COMPILE
 * =======
 *   gcc -o myapp myapp.c -lxlog -lpthread
 *
 * =====================================================================================
 */

#ifndef XLOG_H
#define XLOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Version
 * ============================================================================ */

#define XLOG_VERSION_MAJOR  1
#define XLOG_VERSION_MINOR  0
#define XLOG_VERSION_PATCH  0
#define XLOG_VERSION_STRING "1.0.0"

/* ============================================================================
 * Size Constants
 * ============================================================================ */

#define XLOG_1KB    (1024ULL)
#define XLOG_1MB    (1024ULL * 1024ULL)
#define XLOG_1GB    (1024ULL * 1024ULL * 1024ULL)

/* ============================================================================
 * Log Levels
 * ============================================================================ */

#ifndef XLOG_LEVEL_H
typedef enum xlog_level
{
	XLOG_LEVEL_TRACE = 0,  /* Most verbose */
	XLOG_LEVEL_DEBUG = 1,
	XLOG_LEVEL_INFO = 2,
	XLOG_LEVEL_WARNING = 3,
	XLOG_LEVEL_ERROR = 4,
	XLOG_LEVEL_FATAL = 5,  /* Least verbose */
	XLOG_LEVEL_OFF = 6   /* Disable all logging */
} xlog_level;
#endif

/* ============================================================================
 * Configuration Enums
 * ============================================================================ */

#ifndef XLOG_BUILDER_H
typedef enum xlog_mode
{
	XLOG_MODE_ASYNC = 0,    /* Asynchronous logging (default, fast) */
	XLOG_MODE_SYNC = 1     /* Synchronous logging (immediate) */
} xlog_mode;
#endif

#ifndef XLOG_BUILDER_H
typedef enum xlog_queue_full_policy
{
	XLOG_QUEUE_DROP = 0,         /* Drop newest message when queue is full */
	XLOG_QUEUE_SPIN = 1,         /* Spin briefly waiting for queue space */
	XLOG_QUEUE_DROP_OLDEST = 2,  /* Overwrite the oldest queued message */
	XLOG_QUEUE_BLOCK = 3         /* Block until queue space is available */
} xlog_queue_full_policy;
#endif

#ifndef XLOG_BUILDER_H
typedef enum xlog_console_target
{
	XLOG_CONSOLE_STDOUT = 0,
	XLOG_CONSOLE_STDERR = 1
} xlog_console_target;
#endif

#ifndef XLOG_COLOR_H
typedef enum xlog_color_mode
{
	XLOG_COLOR_AUTO = 0,  /* Auto-detect TTY */
	XLOG_COLOR_ALWAYS = 1,  /* Force colors */
	XLOG_COLOR_NEVER = 2   /* Disable colors */
} xlog_color_mode;
#endif

#ifndef XLOG_BUILDER_H
typedef enum xlog_syslog_facility
{
	XLOG_SYSLOG_USER = 1,
	XLOG_SYSLOG_DAEMON = 3,
	XLOG_SYSLOG_AUTH = 4,
	XLOG_SYSLOG_LOCAL0 = 16,
	XLOG_SYSLOG_LOCAL1 = 17,
	XLOG_SYSLOG_LOCAL2 = 18,
	XLOG_SYSLOG_LOCAL3 = 19,
	XLOG_SYSLOG_LOCAL4 = 20,
	XLOG_SYSLOG_LOCAL5 = 21,
	XLOG_SYSLOG_LOCAL6 = 22,
	XLOG_SYSLOG_LOCAL7 = 23
} xlog_syslog_facility;
#endif

/* ============================================================================
 * Statistics
 * ============================================================================ */

typedef struct xlog_stats
{
	uint64_t logged;         /* Total messages logged */
	uint64_t dropped;        /* Messages dropped (queue full) */
	uint64_t processed;      /* Messages processed by backend */
	uint64_t flushed;        /* Flush operations */
	uint64_t format_errors;  /* Format errors */
} xlog_stats;

/* ============================================================================
 * Builder (Opaque Type)
 * ============================================================================ */

typedef struct xlog_builder xlog_builder;

/* ============================================================================
 * Sink (Opaque Type for Custom Sinks)
 * ============================================================================ */

typedef struct sink_t sink_t;

/* ============================================================================
 * Quick Initialization API
 * ============================================================================ */

/**
 * Initialize with console output only.
 * @param level  Minimum log level
 * @return       true on success
 */
bool xlog_init_console(xlog_level level);

/**
 * Initialize with console and file output.
 * @param directory  Log file directory
 * @param name       Base filename (without extension)
 * @param level      Minimum log level
 * @return           true on success
 */
bool xlog_init_file(const char *directory, const char *name, xlog_level level);

/**
 * Initialize with console, file, and syslog output.
 * @param directory  Log file directory
 * @param name       Base filename / syslog identifier
 * @param level      Minimum log level
 * @return           true on success
 */
bool xlog_init_full(const char *directory, const char *name, xlog_level level);

/**
 * Initialize for daemon (file + syslog, no console).
 * @param directory  Log file directory
 * @param name       Base filename / syslog identifier
 * @param level      Minimum log level
 * @return           true on success
 */
bool xlog_init_daemon(const char *directory, const char *name, xlog_level level);

/* ============================================================================
 * Core API
 * ============================================================================ */

/**
 * Initialize xlog with default settings.
 * @return  true on success
 */
bool xlog_init(void);

/**
 * Shutdown xlog and flush all pending logs.
 */
void xlog_shutdown(void);

/**
 * Check if xlog is initialized.
 * @return  true if initialized
 */
bool xlog_is_initialized(void);

/**
 * Flush all pending logs to sinks.
 */
void xlog_flush(void);

/**
 * Set the global minimum log level.
 * @param level  Minimum level (messages below this are ignored)
 */
void xlog_set_level(xlog_level level);

/**
 * Get the current global minimum log level.
 * @return  Current level
 */
xlog_level xlog_get_level(void);

/**
 * Check if a log level is enabled.
 * @param level  Level to check
 * @return       true if enabled
 */
bool xlog_level_enabled(xlog_level level);

/* doc166 §3a:判级快路径镜像——xlog_level_enabled 是跨 TU 函数调用,热路径
 * 每日志宏一次;此 volatile int 由 xlog.c 在级别写点(init 配置应用/
 * xlog_set_level)同步,宏内联比较。允许保守滞后(写点覆盖所有公开改级路径;
 * 对齐 int 读无撕裂)。 */
extern volatile int xlog_min_level_fast;

#define XLOG_LEVEL_ENABLED_FAST(level) ((int)(level) >= xlog_min_level_fast)

/**
 * Get logging statistics.
 * @param stats  Output statistics structure
 */
void xlog_get_stats(xlog_stats *stats);

/**
 * Reset logging statistics.
 */
void xlog_reset_stats(void);

/**
 * Add a custom sink to the logger.
 * @param sink  Sink to add (must remain valid while xlog is running)
 * @return      true on success
 */
bool xlog_add_sink(sink_t *sink);

/**
 * Remove a sink from the logger.
 * @param sink  Sink to remove
 * @return      true if found and removed
 */
bool xlog_remove_sink(sink_t *sink);

/**
 * Get the number of active sinks.
 * @return  Number of active sinks
 */
size_t xlog_sink_count(void);

/* ============================================================================
 * Thread-Local Trace Context (MDC-style)
 * ============================================================================
 * Ambient distributed-tracing context for the calling thread. When active,
 * it is stamped onto every record produced by the XLOG_ and LOG_ macros
 * and by xlog_log(); an explicit context passed to xlog_log_ctx() takes
 * precedence.
 *
 * 128-bit trace IDs follow the W3C Trace Context / OpenTelemetry layout:
 * trace_hi is the most significant half. JSON output renders
 * "trace_id" as 32 hex chars when trace_hi is set (16 otherwise),
 * plus "span_id" / "parent_span_id" when nonzero.
 *
 * Zero = absent. Calling xlog_trace_set(0,0,0,0) is equivalent to clear.
 * When nothing is set, log calls pay only a single flag check - no output,
 * no allocation. Works before xlog_init().
 */

/**
 * Set the ambient trace context for the calling thread.
 * All parameters of 0 deactivate the context.
 */
void xlog_trace_set(uint64_t trace_hi, uint64_t trace_lo,
                    uint64_t span_id, uint64_t parent_span_id);

/**
 * Clear the ambient trace context for the calling thread.
 */
void xlog_trace_clear(void);

/**
 * Read back the ambient trace context.
 * @param trace_hi        Out: upper 64 bits of trace ID (may be NULL)
 * @param trace_lo        Out: lower 64 bits of trace ID (may be NULL)
 * @param span_id         Out: span ID (may be NULL)
 * @param parent_span_id  Out: parent span ID (may be NULL)
 * @return                true if the context is active (any id nonzero)
 */
bool xlog_trace_get(uint64_t *trace_hi, uint64_t *trace_lo,
                    uint64_t *span_id, uint64_t *parent_span_id);

/* ============================================================================
 * Builder API - Creation/Destruction
 * ============================================================================ */

/**
 * Create a new builder with default values.
 * @return  New builder (must be freed with xlog_builder_free)
 */
xlog_builder *xlog_builder_new(void);

/**
 * Free a builder.
 * @param cfg  Builder to free
 */
void xlog_builder_free(xlog_builder *cfg);

/**
 * Apply configuration and initialize xlog.
 * @param cfg  Builder with configuration
 * @return     true on success
 */
bool xlog_builder_apply(xlog_builder *cfg);

/**
 * Dump configuration to a string (for debugging).
 * @param cfg     Builder
 * @param buffer  Output buffer
 * @param size    Buffer size
 * @return        Number of characters written
 */
int xlog_builder_dump(const xlog_builder *cfg, char *buffer, size_t size);

/* ============================================================================
 * Builder API - Global Settings
 * ============================================================================ */

xlog_builder *xlog_builder_set_name(xlog_builder *cfg, const char *name);

xlog_builder *xlog_builder_set_level(xlog_builder *cfg, xlog_level level);

xlog_builder *xlog_builder_set_mode(xlog_builder *cfg, xlog_mode mode);

xlog_builder *xlog_builder_set_buffer_size(xlog_builder *cfg, uint32_t size);

xlog_builder *xlog_builder_set_queue_policy(xlog_builder *cfg, xlog_queue_full_policy policy);

xlog_builder *xlog_builder_set_queue_spin_timeout(xlog_builder *cfg, uint32_t timeout_us);

xlog_builder *xlog_builder_set_queue_block_timeout(xlog_builder *cfg, uint32_t timeout_us);

/* ============================================================================
 * Builder API - Console Sink
 * ============================================================================ */

xlog_builder *xlog_builder_enable_console(xlog_builder *cfg, bool enable);

xlog_builder *xlog_builder_console_level(xlog_builder *cfg, xlog_level level);

xlog_builder *xlog_builder_console_target(xlog_builder *cfg, xlog_console_target target);

xlog_builder *xlog_builder_console_color(xlog_builder *cfg, xlog_color_mode mode);

xlog_builder *xlog_builder_console_flush(xlog_builder *cfg, bool flush);

/* ============================================================================
 * Builder API - File Sink
 * ============================================================================ */

xlog_builder *xlog_builder_enable_file(xlog_builder *cfg, bool enable);

xlog_builder *xlog_builder_file_level(xlog_builder *cfg, xlog_level level);

xlog_builder *xlog_builder_file_directory(xlog_builder *cfg, const char *dir);

xlog_builder *xlog_builder_file_name(xlog_builder *cfg, const char *name);

xlog_builder *xlog_builder_file_extension(xlog_builder *cfg, const char *ext);

xlog_builder *xlog_builder_file_max_size(xlog_builder *cfg, uint64_t size);

xlog_builder *xlog_builder_file_max_dir_size(xlog_builder *cfg, uint64_t size);

/**
 * @brief Set cross-directory total quota over the parent of the log directory
 * @param cfg   Builder handle
 * @param size  Total budget in bytes (0 = disabled, default)
 * @return      Builder handle for chaining
 *
 * At every rotation, archive files matching this logger's archive pattern
 * (base-YYYYMMDD*) across ALL sibling directories are summed; oldest-by-mtime
 * archives are evicted cross-directory until the total fits. Active files and
 * foreign-named files are never touched. Multi-process safe (mkdir lock).
 * For multi-instance layouts like <root>/<pid>/ where per-directory rotate
 * leaves the root unbounded.
 */
xlog_builder *xlog_builder_file_max_root_size(xlog_builder *cfg, uint64_t size);

xlog_builder *xlog_builder_file_max_files(xlog_builder *cfg, uint32_t count);

xlog_builder *xlog_builder_file_rotate_on_start(xlog_builder *cfg, bool rotate);

xlog_builder *xlog_builder_file_flush(xlog_builder *cfg, bool flush);

/* ============================================================================
 * Builder API - Syslog Sink (POSIX only)
 * ============================================================================ */

xlog_builder *xlog_builder_enable_syslog(xlog_builder *cfg, bool enable);

xlog_builder *xlog_builder_syslog_level(xlog_builder *cfg, xlog_level level);

xlog_builder *xlog_builder_syslog_ident(xlog_builder *cfg, const char *ident);

xlog_builder *xlog_builder_syslog_facility(xlog_builder *cfg, xlog_syslog_facility facility);

xlog_builder *xlog_builder_syslog_pid(xlog_builder *cfg, bool include);

/* ============================================================================
 * Builder API - Presets
 * ============================================================================ */

/**
 * Development preset: console with colors, DEBUG level.
 */
xlog_builder *xlog_preset_development(void);

/**
 * Production preset: file only, INFO level, standard rotation.
 */
xlog_builder *xlog_preset_production(const char *log_dir, const char *app_name);

/**
 * Testing preset: console + small files, TRACE level.
 */
xlog_builder *xlog_preset_testing(const char *log_dir);

/* ============================================================================
 * Logging Function (Internal - use macros instead)
 * ============================================================================ */

void xlog_log(xlog_level level, const char *file, uint32_t line,
              const char *func, const char *fmt, ...);

void xlog_log_v(xlog_level level, const char *file, uint32_t line,
                const char *func, const char *fmt, va_list args);

/* ============================================================================
 * Logging Macros (Primary API)
 * ============================================================================ */

#define XLOG_TRACE(...) \
    do { if (XLOG_LEVEL_ENABLED_FAST(XLOG_LEVEL_TRACE)) \
        xlog_log(XLOG_LEVEL_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)

#define XLOG_DEBUG(...) \
    do { if (XLOG_LEVEL_ENABLED_FAST(XLOG_LEVEL_DEBUG)) \
        xlog_log(XLOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)

#define XLOG_INFO(...) \
    do { if (XLOG_LEVEL_ENABLED_FAST(XLOG_LEVEL_INFO)) \
        xlog_log(XLOG_LEVEL_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)

#define XLOG_WARN(...) \
    do { if (XLOG_LEVEL_ENABLED_FAST(XLOG_LEVEL_WARNING)) \
        xlog_log(XLOG_LEVEL_WARNING, __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)

/* ERROR/FATAL 保持无条件(doc163 约定:错误路径不设门槛,镜像滞后不丢错误日志) */
#define XLOG_ERROR(...) \
    xlog_log(XLOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define XLOG_FATAL(...) \
    xlog_log(XLOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

/* Conditional logging */
#define XLOG_TRACE_IF(cond, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(XLOG_LEVEL_TRACE)) \
        XLOG_TRACE(__VA_ARGS__); } while(0)

#define XLOG_DEBUG_IF(cond, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(XLOG_LEVEL_DEBUG)) \
        XLOG_DEBUG(__VA_ARGS__); } while(0)

#define XLOG_INFO_IF(cond, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(XLOG_LEVEL_INFO)) \
        XLOG_INFO(__VA_ARGS__); } while(0)

#define XLOG_WARN_IF(cond, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(XLOG_LEVEL_WARNING)) \
        XLOG_WARN(__VA_ARGS__); } while(0)

#define XLOG_ERROR_IF(cond, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(XLOG_LEVEL_ERROR)) \
        XLOG_ERROR(__VA_ARGS__); } while(0)

#define XLOG_FATAL_IF(cond, ...) \
    do { if ((cond) && XLOG_LEVEL_ENABLED_FAST(XLOG_LEVEL_FATAL)) \
        XLOG_FATAL(__VA_ARGS__); } while(0)

/* ============================================================================
 * Legacy Macros (Backward Compatibility)
 * ============================================================================
 * Define XLOG_NO_LEGACY_MACROS before including to disable these.
 */

#ifndef XLOG_NO_LEGACY_MACROS

#define LOG_TRACE(...) XLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) XLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)  XLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)  XLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) XLOG_ERROR(__VA_ARGS__)
#define LOG_FATAL(...) XLOG_FATAL(__VA_ARGS__)

#define LOG_TRACE_IF(cond, ...) XLOG_TRACE_IF(cond, __VA_ARGS__)
#define LOG_DEBUG_IF(cond, ...) XLOG_DEBUG_IF(cond, __VA_ARGS__)
#define LOG_INFO_IF(cond, ...)  XLOG_INFO_IF(cond, __VA_ARGS__)
#define LOG_WARN_IF(cond, ...)  XLOG_WARN_IF(cond, __VA_ARGS__)
#define LOG_ERROR_IF(cond, ...) XLOG_ERROR_IF(cond, __VA_ARGS__)
#define LOG_FATAL_IF(cond, ...) XLOG_FATAL_IF(cond, __VA_ARGS__)

#endif /* XLOG_NO_LEGACY_MACROS */

#ifdef __cplusplus
}
#endif

#endif /* XLOG_H */

