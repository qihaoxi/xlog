/* =====================================================================================
 *       Filename:  xlog_builder.h
 *    Description:  Unified configuration API with builder pattern for xlog
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#ifndef XLOG_BUILDER_H
#define XLOG_BUILDER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Check if public API header is already included */
#ifdef XLOG_H
/* Types already defined in xlog.h, just need internal types */
#else
/* Include internal headers when xlog.h is not available */
#include "level.h"
#include "color.h"

#endif

#include "log_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct xlog_builder xlog_builder;
typedef struct xlog_console_config xlog_console_config;
typedef struct xlog_file_config xlog_file_config;
typedef struct xlog_syslog_config xlog_syslog_config;

/* ============================================================================
 * Size Constants (only if not defined in xlog.h)
 * ============================================================================ */

#ifndef XLOG_1KB
#define XLOG_1KB    (1024ULL)
#define XLOG_1MB    (1024ULL * 1024ULL)
#define XLOG_1GB    (1024ULL * 1024ULL * 1024ULL)
#endif

/* ============================================================================
 * Enums (only if not defined in xlog.h)
 * ============================================================================ */

#ifndef XLOG_H

typedef enum xlog_console_target
{
	XLOG_CONSOLE_STDOUT = 0,
	XLOG_CONSOLE_STDERR = 1
} xlog_console_target;

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

typedef enum xlog_mode
{
	XLOG_MODE_ASYNC = 0,
	XLOG_MODE_SYNC = 1
} xlog_mode;

#endif /* XLOG_H */

/* ============================================================================
 * Console Sink Configuration
 * ============================================================================ */

struct xlog_console_config
{
	bool enabled;
	log_level level;
	xlog_console_target target;
	xlog_color_mode color_mode;
	bool flush_on_write;
};

/* Console config builder macros */
#define XLOG_CONSOLE_DEFAULT() ((xlog_console_config){ \
    .enabled = true, \
    .level = LOG_LEVEL_DEBUG, \
    .target = XLOG_CONSOLE_STDOUT, \
    .color_mode = XLOG_COLOR_AUTO, \
    .flush_on_write = true \
})

/* ============================================================================
 * File Sink Configuration
 * ============================================================================ */

struct xlog_file_config
{
	bool enabled;
	log_level level;
	const char *directory;
	const char *base_name;
	const char *extension;
	uint64_t max_file_size;
	uint64_t max_dir_size;
	uint32_t max_files;
	bool rotate_on_start;
	bool flush_on_write;
	bool compress_old;       /* Compress rotated log files (gzip) */
};

/* File config builder macros */
#define XLOG_FILE_DEFAULT() ((xlog_file_config){ \
    .enabled = false, \
    .level = LOG_LEVEL_DEBUG, \
    .directory = "./logs", \
    .base_name = "app", \
    .extension = ".log", \
    .max_file_size = 50 * XLOG_1MB, \
    .max_dir_size = 500 * XLOG_1MB, \
    .max_files = 100, \
    .rotate_on_start = true, \
    .flush_on_write = false, \
    .compress_old = false \
})

/* ============================================================================
 * Syslog Sink Configuration (POSIX only)
 * ============================================================================ */

struct xlog_syslog_config
{
	bool enabled;
	log_level level;
	const char *ident;
	xlog_syslog_facility facility;
	bool include_pid;
};

/* Syslog config builder macros */
#define XLOG_SYSLOG_DEFAULT() ((xlog_syslog_config){ \
    .enabled = false, \
    .level = LOG_LEVEL_INFO, \
    .ident = NULL, \
    .facility = XLOG_SYSLOG_USER, \
    .include_pid = true \
})

/* ============================================================================
 * Log Format Configuration
 * ============================================================================ */

typedef enum xlog_format_style
{
	XLOG_FORMAT_DEFAULT = 0,    /* [time  level  T:tid  file:line] message */
	XLOG_FORMAT_SIMPLE,         /* [time  level] message */
	XLOG_FORMAT_DETAILED,       /* [time  level  T:tid  module#tag  trace  file:line] message */
	XLOG_FORMAT_JSON,           /* {"timestamp":"...", "level":"...", "message":"..."} */
	XLOG_FORMAT_RAW,            /* Only message content, no metadata */
	XLOG_FORMAT_CUSTOM          /* User-defined pattern */
} xlog_format_style;

typedef struct xlog_format_config
{
	xlog_format_style style;
	bool show_timestamp;
	bool show_level;
	bool show_thread_id;
	bool show_file_line;
	bool show_function;
	bool show_module;
	bool show_tag;
	bool show_trace_id;
	bool append_newline;           /* Append newline after each log entry */
	const char *timestamp_format;  /* Custom timestamp format (future) */
	const char *custom_pattern;    /* For XLOG_FORMAT_CUSTOM */
} xlog_format_config;

#define XLOG_FORMAT_DEFAULT_CONFIG() ((xlog_format_config){ \
    .style = XLOG_FORMAT_DEFAULT, \
    .show_timestamp = true, \
    .show_level = true, \
    .show_thread_id = true, \
    .show_file_line = true, \
    .show_function = false, \
    .show_module = false, \
    .show_tag = false, \
    .show_trace_id = false, \
    .append_newline = true, \
    .timestamp_format = NULL, \
    .custom_pattern = NULL \
})

/* ============================================================================
 * Main Configuration Structure
 * ============================================================================ */


struct xlog_builder
{
	/* Global settings */
	const char *app_name;          /* Application name */
	log_level global_level;       /* Global minimum log level */
	xlog_mode mode;               /* Async or sync mode */

	/* Ring buffer settings (for async mode) */
	uint32_t ring_buffer_size;   /* Number of slots (power of 2) */

	/* Format settings */
	xlog_format_config format;

	/* Sink configurations */
	xlog_console_config console;
	xlog_file_config file;
	xlog_syslog_config syslog;

	/* Internal state - do not modify */
	bool _initialized;
};

/* ============================================================================
 * Builder Pattern API (Chain Style)
 * ============================================================================ */

/**
 * Create a new configuration with default values.
 * Start of the builder chain.
 */
xlog_builder *xlog_builder_new(void);

/**
 * Free a configuration object.
 */
void xlog_builder_free(xlog_builder *cfg);

/* --- Global Settings --- */

/** Set application name */
xlog_builder *xlog_builder_set_name(xlog_builder *cfg, const char *name);

/** Set global minimum log level */
xlog_builder *xlog_builder_set_level(xlog_builder *cfg, log_level level);

/** Set logging mode (async/sync) */
xlog_builder *xlog_builder_set_mode(xlog_builder *cfg, xlog_mode mode);

/** Set ring buffer size (for async mode) */
xlog_builder *xlog_builder_set_buffer_size(xlog_builder *cfg, uint32_t size);

/* --- Format Settings --- */

/** Set format style */
xlog_builder *xlog_builder_set_format(xlog_builder *cfg, xlog_format_style style);

/** Show/hide timestamp */
xlog_builder *xlog_builder_show_timestamp(xlog_builder *cfg, bool show);

/** Show/hide log level */
xlog_builder *xlog_builder_show_level(xlog_builder *cfg, bool show);

/** Show/hide thread ID */
xlog_builder *xlog_builder_show_thread_id(xlog_builder *cfg, bool show);

/** Show/hide file and line number */
xlog_builder *xlog_builder_show_file_line(xlog_builder *cfg, bool show);

/** Show/hide function name */
xlog_builder *xlog_builder_show_function(xlog_builder *cfg, bool show);

/** Show/hide module name */
xlog_builder *xlog_builder_show_module(xlog_builder *cfg, bool show);

/** Show/hide tag */
xlog_builder *xlog_builder_show_tag(xlog_builder *cfg, bool show);

/* --- Console Sink Settings --- */

/** Enable/disable console sink */
xlog_builder *xlog_builder_enable_console(xlog_builder *cfg, bool enable);

/** Set console log level */
xlog_builder *xlog_builder_console_level(xlog_builder *cfg, log_level level);

/** Set console output target (stdout/stderr) */
xlog_builder *xlog_builder_console_target(xlog_builder *cfg, xlog_console_target target);

/** Set console color mode */
xlog_builder *xlog_builder_console_color(xlog_builder *cfg, xlog_color_mode mode);

/** Set console flush on write */
xlog_builder *xlog_builder_console_flush(xlog_builder *cfg, bool flush);

/* --- File Sink Settings --- */

/** Enable/disable file sink */
xlog_builder *xlog_builder_enable_file(xlog_builder *cfg, bool enable);

/** Set file log level */
xlog_builder *xlog_builder_file_level(xlog_builder *cfg, log_level level);

/** Set log directory */
xlog_builder *xlog_builder_file_directory(xlog_builder *cfg, const char *dir);

/** Set base filename */
xlog_builder *xlog_builder_file_name(xlog_builder *cfg, const char *name);

/** Set file extension */
xlog_builder *xlog_builder_file_extension(xlog_builder *cfg, const char *ext);

/** Set max file size (bytes) */
xlog_builder *xlog_builder_file_max_size(xlog_builder *cfg, uint64_t size);

/** Set max directory size (bytes) */
xlog_builder *xlog_builder_file_max_dir_size(xlog_builder *cfg, uint64_t size);

/** Set max number of archived files */
xlog_builder *xlog_builder_file_max_files(xlog_builder *cfg, uint32_t count);

/** Set rotate on start */
xlog_builder *xlog_builder_file_rotate_on_start(xlog_builder *cfg, bool rotate);

/** Set file flush on write */
xlog_builder *xlog_builder_file_flush(xlog_builder *cfg, bool flush);

/** Enable/disable compression of rotated log files (gzip) */
xlog_builder *xlog_builder_file_compress(xlog_builder *cfg, bool compress);

/* --- Syslog Sink Settings (POSIX only) --- */

/** Enable/disable syslog sink */
xlog_builder *xlog_builder_enable_syslog(xlog_builder *cfg, bool enable);

/** Set syslog log level */
xlog_builder *xlog_builder_syslog_level(xlog_builder *cfg, log_level level);

/** Set syslog identifier */
xlog_builder *xlog_builder_syslog_ident(xlog_builder *cfg, const char *ident);

/** Set syslog facility */
xlog_builder *xlog_builder_syslog_facility(xlog_builder *cfg, xlog_syslog_facility facility);

/** Include PID in syslog messages */
xlog_builder *xlog_builder_syslog_pid(xlog_builder *cfg, bool include);

/* --- Build and Apply --- */

/**
 * Apply configuration and initialize xlog.
 * End of the builder chain.
 *
 * @param cfg   Configuration object
 * @return      true on success, false on failure
 */
bool xlog_builder_apply(xlog_builder *cfg);

/**
 * Get a human-readable summary of the configuration.
 *
 * @param cfg       Configuration object
 * @param buffer    Output buffer
 * @param size      Buffer size
 * @return          Number of characters written
 */
int xlog_builder_dump(const xlog_builder *cfg, char *buffer, size_t size);

/* ============================================================================
 * Quick Setup API (One-liner defaults)
 * ============================================================================ */

/**
 * Initialize xlog with console output only.
 * Simplest setup for quick debugging.
 *
 * Usage: xlog_init_console(LOG_LEVEL_DEBUG);
 */
bool xlog_init_console(log_level level);

/**
 * Initialize xlog with console and file output.
 *
 * Usage: xlog_init_file("./logs", "myapp", LOG_LEVEL_INFO);
 */
bool xlog_init_file(const char *directory, const char *name, log_level level);

/**
 * Initialize xlog with all sinks (console + file + syslog).
 *
 * Usage: xlog_init_full("./logs", "myapp", LOG_LEVEL_DEBUG);
 */
bool xlog_init_full(const char *directory, const char *name, log_level level);

/**
 * Initialize xlog for daemon/service use (file + syslog, no console).
 *
 * Usage: xlog_init_daemon("./logs", "mydaemon", LOG_LEVEL_INFO);
 */
bool xlog_init_daemon(const char *directory, const char *name, log_level level);

/* ============================================================================
 * Preset Configurations
 * ============================================================================ */

/**
 * Get a preset configuration for development.
 * - Console: enabled, DEBUG level, colors on
 * - File: disabled
 * - Format: detailed with file:line
 */
xlog_builder *xlog_preset_development(void);

/**
 * Get a preset configuration for production.
 * - Console: disabled
 * - File: enabled, INFO level, 50MB per file, 500MB total
 * - Format: default
 */
xlog_builder *xlog_preset_production(const char *log_dir, const char *app_name);

/**
 * Get a preset configuration for testing.
 * - Console: enabled, TRACE level
 * - File: enabled, small sizes for quick rotation testing
 * - Format: detailed
 */
xlog_builder *xlog_preset_testing(const char *log_dir);

#ifdef __cplusplus
}
#endif

#endif /* XLOG_BUILDER_H */

