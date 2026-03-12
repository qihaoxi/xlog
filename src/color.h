/* =====================================================================================
 *       Filename:  color.h
 *    Description:  ANSI color output support for terminal logging
 *                  Cross-platform: Linux, macOS, Windows 10+
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#ifndef XLOG_COLOR_H
#define XLOG_COLOR_H

#include <stdbool.h>
#include <stddef.h>
#include "level.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * ANSI Escape Codes
 * ============================================================================ */

/* Reset */
#define XLOG_ANSI_RESET         "\033[0m"

/* Text Styles */
#define XLOG_ANSI_BOLD          "\033[1m"
#define XLOG_ANSI_DIM           "\033[2m"
#define XLOG_ANSI_ITALIC        "\033[3m"
#define XLOG_ANSI_UNDERLINE     "\033[4m"
#define XLOG_ANSI_BLINK         "\033[5m"
#define XLOG_ANSI_REVERSE       "\033[7m"
#define XLOG_ANSI_HIDDEN        "\033[8m"
#define XLOG_ANSI_STRIKETHROUGH "\033[9m"

/* Foreground Colors (Standard) */
#define XLOG_ANSI_BLACK         "\033[30m"
#define XLOG_ANSI_RED           "\033[31m"
#define XLOG_ANSI_GREEN         "\033[32m"
#define XLOG_ANSI_YELLOW        "\033[33m"
#define XLOG_ANSI_BLUE          "\033[34m"
#define XLOG_ANSI_MAGENTA       "\033[35m"
#define XLOG_ANSI_CYAN          "\033[36m"
#define XLOG_ANSI_WHITE         "\033[37m"
#define XLOG_ANSI_DEFAULT       "\033[39m"

/* Foreground Colors (Bright/High Intensity) */
#define XLOG_ANSI_BRIGHT_BLACK   "\033[90m"
#define XLOG_ANSI_BRIGHT_RED     "\033[91m"
#define XLOG_ANSI_BRIGHT_GREEN   "\033[92m"
#define XLOG_ANSI_BRIGHT_YELLOW  "\033[93m"
#define XLOG_ANSI_BRIGHT_BLUE    "\033[94m"
#define XLOG_ANSI_BRIGHT_MAGENTA "\033[95m"
#define XLOG_ANSI_BRIGHT_CYAN    "\033[96m"
#define XLOG_ANSI_BRIGHT_WHITE   "\033[97m"

/* Background Colors (Standard) */
#define XLOG_ANSI_BG_BLACK      "\033[40m"
#define XLOG_ANSI_BG_RED        "\033[41m"
#define XLOG_ANSI_BG_GREEN      "\033[42m"
#define XLOG_ANSI_BG_YELLOW     "\033[43m"
#define XLOG_ANSI_BG_BLUE       "\033[44m"
#define XLOG_ANSI_BG_MAGENTA    "\033[45m"
#define XLOG_ANSI_BG_CYAN       "\033[46m"
#define XLOG_ANSI_BG_WHITE      "\033[47m"
#define XLOG_ANSI_BG_DEFAULT    "\033[49m"

/* Background Colors (Bright/High Intensity) */
#define XLOG_ANSI_BG_BRIGHT_BLACK   "\033[100m"
#define XLOG_ANSI_BG_BRIGHT_RED     "\033[101m"
#define XLOG_ANSI_BG_BRIGHT_GREEN   "\033[102m"
#define XLOG_ANSI_BG_BRIGHT_YELLOW  "\033[103m"
#define XLOG_ANSI_BG_BRIGHT_BLUE    "\033[104m"
#define XLOG_ANSI_BG_BRIGHT_MAGENTA "\033[105m"
#define XLOG_ANSI_BG_BRIGHT_CYAN    "\033[106m"
#define XLOG_ANSI_BG_BRIGHT_WHITE   "\033[107m"

/* ============================================================================
 * Log Level Color Schemes
 * ============================================================================ */

/* Default color scheme for log levels */
#define XLOG_COLOR_TRACE    XLOG_ANSI_DIM XLOG_ANSI_WHITE       /* Dim white */
#define XLOG_COLOR_DEBUG    XLOG_ANSI_CYAN                       /* Cyan */
#define XLOG_COLOR_INFO     XLOG_ANSI_GREEN                      /* Green */
#define XLOG_COLOR_WARN     XLOG_ANSI_BOLD XLOG_ANSI_YELLOW      /* Bold Yellow */
#define XLOG_COLOR_ERROR    XLOG_ANSI_BOLD XLOG_ANSI_RED         /* Bold Red */
#define XLOG_COLOR_FATAL    XLOG_ANSI_BOLD XLOG_ANSI_BG_RED XLOG_ANSI_WHITE  /* Bold White on Red */

/* Additional semantic colors */
#define XLOG_COLOR_TIMESTAMP XLOG_ANSI_BRIGHT_BLACK              /* Gray */
#define XLOG_COLOR_THREAD    XLOG_ANSI_BRIGHT_BLUE               /* Bright Blue */
#define XLOG_COLOR_FILE      XLOG_ANSI_BRIGHT_BLACK              /* Gray */
#define XLOG_COLOR_MODULE    XLOG_ANSI_MAGENTA                   /* Magenta */
#define XLOG_COLOR_TAG       XLOG_ANSI_BRIGHT_CYAN               /* Bright Cyan */

/* ============================================================================
 * Color Configuration
 * ============================================================================ */

/* Color mode - use public API definition if available */
#ifndef XLOG_H
typedef enum xlog_color_mode
{
	XLOG_COLOR_AUTO = 0,    /* Auto-detect (TTY = colors, non-TTY = no colors) */
	XLOG_COLOR_ALWAYS,      /* Always use colors */
	XLOG_COLOR_NEVER        /* Never use colors */
} xlog_color_mode;
#endif /* XLOG_H */

/* Color scheme (predefined color sets) */
typedef enum xlog_color_scheme
{
	XLOG_SCHEME_DEFAULT = 0,    /* Default color scheme */
	XLOG_SCHEME_VIVID,          /* High contrast vivid colors */
	XLOG_SCHEME_PASTEL,         /* Soft pastel colors */
	XLOG_SCHEME_MONOCHROME,     /* Grayscale only */
	XLOG_SCHEME_CUSTOM          /* User-defined colors */
} xlog_color_scheme;

/* Custom color configuration for each log level */
typedef struct xlog_color_config
{
	const char *trace_color;
	const char *debug_color;
	const char *info_color;
	const char *warn_color;
	const char *error_color;
	const char *fatal_color;
	const char *timestamp_color;
	const char *thread_color;
	const char *file_color;
	const char *module_color;
	const char *tag_color;
	const char *reset;
} xlog_color_config;

/* ============================================================================
 * Color API
 * ============================================================================ */

/**
 * Initialize color support (call once at startup).
 * On Windows, this enables ANSI escape sequence processing.
 */
void xlog_color_init(void);

/**
 * Check if the given file descriptor supports colors.
 *
 * @param fd    File descriptor (0=stdin, 1=stdout, 2=stderr)
 * @return      true if colors are supported
 */
bool xlog_color_supported(int fd);

/**
 * Get the ANSI color code for a log level.
 *
 * @param level     Log level
 * @return          ANSI color code string (static, do not free)
 */
const char *xlog_color_for_level(xlog_level level);

/**
 * Get the ANSI reset code.
 *
 * @return          ANSI reset code string
 */
const char *xlog_color_reset(void);

/**
 * Get a predefined color scheme.
 *
 * @param scheme    Color scheme type
 * @return          Pointer to color config (static, do not free)
 */
const xlog_color_config *xlog_color_get_scheme(xlog_color_scheme scheme);

/**
 * Set the global color mode.
 *
 * @param mode      Color mode (AUTO, ALWAYS, NEVER)
 */
void xlog_color_set_mode(xlog_color_mode mode);

/**
 * Get the current global color mode.
 *
 * @return          Current color mode
 */
xlog_color_mode xlog_color_get_mode(void);

/**
 * Set a custom color scheme.
 *
 * @param config    Custom color configuration
 */
void xlog_color_set_custom(const xlog_color_config *config);

/* ============================================================================
 * Color Formatting Helpers
 * ============================================================================ */

/**
 * Format a string with color.
 * Output: {color}{text}{reset}
 *
 * @param output    Output buffer
 * @param out_size  Output buffer size
 * @param color     ANSI color code
 * @param text      Text to colorize
 * @return          Number of bytes written (not including null terminator)
 */
int xlog_color_format(char *output, size_t out_size,
                      const char *color, const char *text);

/**
 * Format a log level with color.
 *
 * @param output    Output buffer
 * @param out_size  Output buffer size
 * @param level     Log level
 * @return          Number of bytes written
 */
int xlog_color_format_level(char *output, size_t out_size, xlog_level level);

/**
 * Format a timestamp with color.
 *
 * @param output    Output buffer
 * @param out_size  Output buffer size
 * @param timestamp Timestamp string
 * @return          Number of bytes written
 */
int xlog_color_format_timestamp(char *output, size_t out_size, const char *timestamp);

/**
 * Strip ANSI color codes from a string.
 *
 * @param output    Output buffer (can be same as input for in-place)
 * @param out_size  Output buffer size
 * @param input     Input string with potential ANSI codes
 * @return          Number of bytes written (not including null terminator)
 */
int xlog_color_strip(char *output, size_t out_size, const char *input);

/**
 * Calculate the display width of a string (excluding ANSI codes).
 *
 * @param str       String potentially containing ANSI codes
 * @return          Display width in characters
 */
size_t xlog_color_display_width(const char *str);

/* ============================================================================
 * Windows Compatibility
 * ============================================================================ */

#ifdef _WIN32
/**
 * Enable ANSI escape sequence processing on Windows console.
 * Called automatically by xlog_color_init().
 *
 * @return          true if ANSI is now enabled
 */
bool xlog_color_enable_windows_ansi(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XLOG_COLOR_H */

