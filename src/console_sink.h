/* =====================================================================================
 *       Filename:  console_sink.h
 *    Description:  Console sink with color support
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#ifndef XLOG_CONSOLE_SINK_H
#define XLOG_CONSOLE_SINK_H

#include <stdbool.h>
#include "sink.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Console Output Target
 * ============================================================================ */

typedef enum console_target
{
	CONSOLE_STDOUT = 0,     /* Output to stdout */
	CONSOLE_STDERR = 1,     /* Output to stderr */
} console_target;

/* ============================================================================
 * Console Sink Configuration
 * ============================================================================ */

typedef struct console_sink_config
{
	console_target target;         /* Output target (stdout/stderr) */
	bool use_colors;     /* Enable ANSI color codes */
	bool flush_on_write; /* Flush after each write */
} console_sink_config;

/* ============================================================================
 * Console Sink API
 * ============================================================================ */

/**
 * Create a console sink with the specified configuration.
 *
 * @param config    Configuration for the console sink
 * @param level     Minimum log level for this sink
 * @return          Pointer to the created sink, or NULL on failure
 */
sink_t *console_sink_create(const console_sink_config *config, log_level level);

/**
 * Create a simple console sink to stdout with auto color detection.
 *
 * @param level     Minimum log level for this sink
 * @return          Pointer to the created sink, or NULL on failure
 */
sink_t *console_sink_create_stdout(log_level level);

/**
 * Create a simple console sink to stderr with auto color detection.
 *
 * @param level     Minimum log level for this sink
 * @return          Pointer to the created sink, or NULL on failure
 */
sink_t *console_sink_create_stderr(log_level level);

/**
 * Check if the console sink is outputting to a TTY (terminal).
 *
 * @param sink      The console sink
 * @return          true if output is to a TTY, false otherwise
 */
bool console_sink_is_tty(sink_t *sink);

/**
 * Enable or disable color output for the console sink.
 *
 * @param sink      The console sink
 * @param enable    true to enable colors, false to disable
 */
void console_sink_set_colors(sink_t *sink, bool enable);

/* ============================================================================
 * ANSI Color Codes
 * ============================================================================ */

#define ANSI_RESET      "\033[0m"
#define ANSI_BOLD       "\033[1m"
#define ANSI_DIM        "\033[2m"

#define ANSI_BLACK      "\033[30m"
#define ANSI_RED        "\033[31m"
#define ANSI_GREEN      "\033[32m"
#define ANSI_YELLOW     "\033[33m"
#define ANSI_BLUE       "\033[34m"
#define ANSI_MAGENTA    "\033[35m"
#define ANSI_CYAN       "\033[36m"
#define ANSI_WHITE      "\033[37m"

#define ANSI_BG_RED     "\033[41m"
#define ANSI_BG_YELLOW  "\033[43m"

/* Level-specific colors */
#define LOG_COLOR_TRACE     ANSI_DIM ANSI_WHITE
#define LOG_COLOR_DEBUG     ANSI_CYAN
#define LOG_COLOR_INFO      ANSI_GREEN
#define LOG_COLOR_WARN      ANSI_YELLOW
#define LOG_COLOR_ERROR     ANSI_RED
#define LOG_COLOR_FATAL     ANSI_BOLD ANSI_BG_RED ANSI_WHITE

/**
 * Get the ANSI color code for a log level.
 *
 * @param level     The log level
 * @return          ANSI color code string
 */
const char *log_level_color(log_level level);

#ifdef __cplusplus
}
#endif

#endif /* XLOG_CONSOLE_SINK_H */

