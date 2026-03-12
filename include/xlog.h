/* =====================================================================================
 *       Filename:  xlog.h
 *    Description:  xlog - High Performance Async Logging Library for C
 *                  Single header API - include this file only
 *        Version:  1.0.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
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
 * @return  Number of sinks
 */
size_t xlog_sink_count(void);

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

#define XLOG_TRACE(fmt, ...) \
    xlog_log(XLOG_LEVEL_TRACE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define XLOG_DEBUG(fmt, ...) \
    xlog_log(XLOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define XLOG_INFO(fmt, ...) \
    xlog_log(XLOG_LEVEL_INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define XLOG_WARN(fmt, ...) \
    xlog_log(XLOG_LEVEL_WARNING, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define XLOG_ERROR(fmt, ...) \
    xlog_log(XLOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define XLOG_FATAL(fmt, ...) \
    xlog_log(XLOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

/* Conditional logging */
#define XLOG_TRACE_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(XLOG_LEVEL_TRACE)) \
        XLOG_TRACE(fmt, ##__VA_ARGS__); } while(0)

#define XLOG_DEBUG_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(XLOG_LEVEL_DEBUG)) \
        XLOG_DEBUG(fmt, ##__VA_ARGS__); } while(0)

#define XLOG_INFO_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(XLOG_LEVEL_INFO)) \
        XLOG_INFO(fmt, ##__VA_ARGS__); } while(0)

#define XLOG_WARN_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(XLOG_LEVEL_WARNING)) \
        XLOG_WARN(fmt, ##__VA_ARGS__); } while(0)

#define XLOG_ERROR_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(XLOG_LEVEL_ERROR)) \
        XLOG_ERROR(fmt, ##__VA_ARGS__); } while(0)

#define XLOG_FATAL_IF(cond, fmt, ...) \
    do { if ((cond) && xlog_level_enabled(XLOG_LEVEL_FATAL)) \
        XLOG_FATAL(fmt, ##__VA_ARGS__); } while(0)

/* ============================================================================
 * Legacy Macros (Backward Compatibility)
 * ============================================================================
 * Define XLOG_NO_LEGACY_MACROS before including to disable these.
 */

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

#endif /* XLOG_H */

