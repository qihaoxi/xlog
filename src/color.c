/* color.c - ANSI color output implementation */
#include "color.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Thread-safe color mode using atomic */
static atomic_int g_color_mode = XLOG_COLOR_AUTO;
static atomic_int g_color_initialized = 0;

/* Predefined color schemes (const, read-only, thread-safe) */
static const xlog_color_config g_scheme_default =
		{
				.trace_color     = XLOG_COLOR_TRACE,
				.debug_color     = XLOG_COLOR_DEBUG,
				.info_color      = XLOG_COLOR_INFO,
				.warn_color      = XLOG_COLOR_WARN,
				.error_color     = XLOG_COLOR_ERROR,
				.fatal_color     = XLOG_COLOR_FATAL,
				.timestamp_color = XLOG_COLOR_TIMESTAMP,
				.thread_color    = XLOG_COLOR_THREAD,
				.file_color      = XLOG_COLOR_FILE,
				.module_color    = XLOG_COLOR_MODULE,
				.tag_color       = XLOG_COLOR_TAG,
				.reset           = XLOG_ANSI_RESET
		};
/* Initialize g_custom_colors with default scheme */
static xlog_color_config g_custom_colors =
		{
				.trace_color     = XLOG_COLOR_TRACE,
				.debug_color     = XLOG_COLOR_DEBUG,
				.info_color      = XLOG_COLOR_INFO,
				.warn_color      = XLOG_COLOR_WARN,
				.error_color     = XLOG_COLOR_ERROR,
				.fatal_color     = XLOG_COLOR_FATAL,
				.timestamp_color = XLOG_COLOR_TIMESTAMP,
				.thread_color    = XLOG_COLOR_THREAD,
				.file_color      = XLOG_COLOR_FILE,
				.module_color    = XLOG_COLOR_MODULE,
				.tag_color       = XLOG_COLOR_TAG,
				.reset           = XLOG_ANSI_RESET
		};
static const xlog_color_config g_scheme_vivid =
		{
				.trace_color     = XLOG_ANSI_BRIGHT_BLACK,
				.debug_color     = XLOG_ANSI_BRIGHT_CYAN,
				.info_color      = XLOG_ANSI_BRIGHT_GREEN,
				.warn_color      = XLOG_ANSI_BOLD XLOG_ANSI_BRIGHT_YELLOW,
				.error_color     = XLOG_ANSI_BOLD XLOG_ANSI_BRIGHT_RED,
				.fatal_color     = XLOG_ANSI_BOLD XLOG_ANSI_BG_BRIGHT_RED XLOG_ANSI_WHITE,
				.timestamp_color = XLOG_ANSI_BRIGHT_BLUE,
				.thread_color    = XLOG_ANSI_BRIGHT_MAGENTA,
				.file_color      = XLOG_ANSI_BRIGHT_BLACK,
				.module_color    = XLOG_ANSI_BRIGHT_MAGENTA,
				.tag_color       = XLOG_ANSI_BRIGHT_CYAN,
				.reset           = XLOG_ANSI_RESET
		};
static const xlog_color_config g_scheme_pastel =
		{
				.trace_color     = XLOG_ANSI_DIM XLOG_ANSI_WHITE,
				.debug_color     = XLOG_ANSI_BLUE,
				.info_color      = XLOG_ANSI_GREEN,
				.warn_color      = XLOG_ANSI_YELLOW,
				.error_color     = XLOG_ANSI_RED,
				.fatal_color     = XLOG_ANSI_BG_RED XLOG_ANSI_WHITE,
				.timestamp_color = XLOG_ANSI_DIM XLOG_ANSI_WHITE,
				.thread_color    = XLOG_ANSI_BLUE,
				.file_color      = XLOG_ANSI_DIM XLOG_ANSI_WHITE,
				.module_color    = XLOG_ANSI_MAGENTA,
				.tag_color       = XLOG_ANSI_CYAN,
				.reset           = XLOG_ANSI_RESET
		};
static const xlog_color_config g_scheme_mono =
		{
				.trace_color     = XLOG_ANSI_DIM,
				.debug_color     = XLOG_ANSI_DIM,
				.info_color      = "",
				.warn_color      = XLOG_ANSI_BOLD,
				.error_color     = XLOG_ANSI_BOLD XLOG_ANSI_UNDERLINE,
				.fatal_color     = XLOG_ANSI_BOLD XLOG_ANSI_REVERSE,
				.timestamp_color = XLOG_ANSI_DIM,
				.thread_color    = XLOG_ANSI_DIM,
				.file_color      = XLOG_ANSI_DIM,
				.module_color    = "",
				.tag_color       = "",
				.reset           = XLOG_ANSI_RESET
		};
static const char *const g_level_names[] =
		{
				"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"
		};
#ifdef XLOG_PLATFORM_WINDOWS
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
bool xlog_color_enable_windows_ansi(void)
{
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE)
		return false;
	DWORD dwMode = 0;
	if (!GetConsoleMode(hOut, &dwMode))
		return false;
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(hOut, dwMode))
		return false;
	HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
	if (hErr != INVALID_HANDLE_VALUE)
	{
		GetConsoleMode(hErr, &dwMode);
		dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		SetConsoleMode(hErr, dwMode);
	}
	return true;
}
#endif

void xlog_color_init(void)
{
	if (atomic_load(&g_color_initialized))
	{
		return;
	}
#ifdef XLOG_PLATFORM_WINDOWS
	xlog_color_enable_windows_ansi();
#endif
	g_custom_colors = g_scheme_default;
	atomic_store(&g_color_initialized, 1);
}

bool xlog_color_supported(int fd)
{
	int mode = atomic_load(&g_color_mode);
	if (mode == XLOG_COLOR_ALWAYS)
	{
		return true;
	}
	if (mode == XLOG_COLOR_NEVER)
	{
		return false;
	}
#ifdef XLOG_PLATFORM_WINDOWS
	HANDLE h;
	switch (fd)
	{
		case 0: h = GetStdHandle(STD_INPUT_HANDLE); break;
		case 1: h = GetStdHandle(STD_OUTPUT_HANDLE); break;
		case 2: h = GetStdHandle(STD_ERROR_HANDLE); break;
		default: return false;
	}
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD dwmode;
	if (!GetConsoleMode(h, &dwmode)) return false;
	return (dwmode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
	return xlog_is_tty(fd);
#endif
}

const char *xlog_color_for_level(log_level level)
{
	/* Check color mode first */
	if (atomic_load(&g_color_mode) == XLOG_COLOR_NEVER)
	{
		return "";
	}

	switch (level)
	{
		case LOG_LEVEL_TRACE:
			return g_custom_colors.trace_color;
		case LOG_LEVEL_DEBUG:
			return g_custom_colors.debug_color;
		case LOG_LEVEL_INFO:
			return g_custom_colors.info_color;
		case LOG_LEVEL_WARNING:
			return g_custom_colors.warn_color;
		case LOG_LEVEL_ERROR:
			return g_custom_colors.error_color;
		case LOG_LEVEL_FATAL:
			return g_custom_colors.fatal_color;
		default:
			return "";
	}
}

const char *xlog_color_reset(void)
{
	if (atomic_load(&g_color_mode) == XLOG_COLOR_NEVER)
	{
		return "";
	}
	return XLOG_ANSI_RESET;
}

const xlog_color_config *xlog_color_get_scheme(xlog_color_scheme scheme)
{
	switch (scheme)
	{
		case XLOG_SCHEME_DEFAULT:
			return &g_scheme_default;
		case XLOG_SCHEME_VIVID:
			return &g_scheme_vivid;
		case XLOG_SCHEME_PASTEL:
			return &g_scheme_pastel;
		case XLOG_SCHEME_MONOCHROME:
			return &g_scheme_mono;
		case XLOG_SCHEME_CUSTOM:
			return &g_custom_colors;
		default:
			return &g_scheme_default;
	}
}

void xlog_color_set_mode(xlog_color_mode mode)
{
	atomic_store(&g_color_mode, (int)mode);
}

xlog_color_mode xlog_color_get_mode(void)
{
	return (xlog_color_mode)atomic_load(&g_color_mode);
}

void xlog_color_set_custom(const xlog_color_config *config)
{
	if (config)
	{
		g_custom_colors = *config;
	}
}

int xlog_color_format(char *output, size_t out_size, const char *color, const char *text)
{
	if (!output || out_size == 0)
	{
		return -1;
	}
	if (!color || !text || color[0] == '\0')
	{
		size_t len = text ? strlen(text) : 0;
		if (len >= out_size)
		{
			len = out_size - 1;
		}
		if (text)
		{
			memcpy(output, text, len);
		}
		output[len] = '\0';
		return (int) len;
	}
	return snprintf(output, out_size, "%s%s%s", color, text, XLOG_ANSI_RESET);
}

int xlog_color_format_level(char *output, size_t out_size, log_level level)
{
	if (!output || out_size == 0)
	{
		return -1;
	}
	const char *color = xlog_color_for_level(level);
	const char *name = (level >= 0 && level <= LOG_LEVEL_FATAL) ? g_level_names[level] : "?????";
	if (color && color[0] != '\0')
	{
		return snprintf(output, out_size, "%s%s%s", color, name, xlog_color_reset());
	}
	return snprintf(output, out_size, "%s", name);
}

int xlog_color_format_timestamp(char *output, size_t out_size, const char *timestamp)
{
	if (!output || out_size == 0 || !timestamp)
	{
		return -1;
	}
	if (atomic_load(&g_color_mode) == XLOG_COLOR_NEVER)
	{
		return snprintf(output, out_size, "%s", timestamp);
	}
	const char *color = g_custom_colors.timestamp_color;
	if (color && color[0] != '\0')
	{
		return snprintf(output, out_size, "%s%s%s", color, timestamp, XLOG_ANSI_RESET);
	}
	return snprintf(output, out_size, "%s", timestamp);
}

int xlog_color_strip(char *output, size_t out_size, const char *input)
{
	if (!output || out_size == 0 || !input)
	{
		return -1;
	}
	char *dst = output, *dst_end = output + out_size - 1;
	const char *src = input;
	while (*src && dst < dst_end)
	{
		if (*src == '\033')
		{
			src++;
			if (*src == '[')
			{
				src++;
				while (*src && ((*src >= '0' && *src <= '9') || *src == ';')) src++;
				if (*src)
				{
					src++;
				}
			}
		}
		else
		{
			*dst++ = *src++;
		}
	}
	*dst = '\0';
	return (int) (dst - output);
}

size_t xlog_color_display_width(const char *str)
{
	if (!str)
	{
		return 0;
	}
	size_t width = 0;
	const char *p = str;
	while (*p)
	{
		if (*p == '\033')
		{
			p++;
			if (*p == '[')
			{
				p++;
				while (*p && ((*p >= '0' && *p <= '9') || *p == ';')) p++;
				if (*p)
				{
					p++;
				}
			}
		}
		else
		{
			width++;
			p++;
		}
	}
	return width;
}
