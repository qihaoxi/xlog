/* =====================================================================================
 *       Filename:  console_sink.c
 *    Description:  Console sink with color support implementation
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include <stdlib.h>
#include <stdio.h>
#include "platform.h"  /* includes unistd.h on POSIX, provides compat on Windows */
#include "console_sink.h"

/* ============================================================================
 * Console Sink Context
 * ============================================================================ */

typedef struct console_sink_ctx
{
	FILE *stream;        /* Output stream (stdout/stderr) */
	console_target target;         /* Output target */
	bool use_colors;     /* Enable colors */
	bool is_tty;         /* Output is a TTY */
	bool flush_on_write; /* Flush after each write */
} console_sink_ctx;

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

static bool detect_tty(console_target target)
{
	int fd = (target == CONSOLE_STDOUT) ? STDOUT_FILENO : STDERR_FILENO;
	return isatty(fd) != 0;
}

/* ============================================================================
 * Sink Interface Implementation
 * ============================================================================ */

static void console_sink_write(sink_t *sink, const char *data, size_t len)
{
	if (!sink || !sink->ctx || !data || len == 0)
	{
		return;
	}

	console_sink_ctx *ctx = (console_sink_ctx *) sink->ctx;
	if (!ctx->stream)
	{
		return;
	}

	fwrite(data, 1, len, ctx->stream);

	if (ctx->flush_on_write)
	{
		fflush(ctx->stream);
	}
}

static void console_sink_flush(sink_t *sink)
{
	if (!sink || !sink->ctx)
	{
		return;
	}

	console_sink_ctx *ctx = (console_sink_ctx *) sink->ctx;
	if (ctx->stream)
	{
		fflush(ctx->stream);
	}
}

static void console_sink_close(sink_t *sink)
{
	if (!sink || !sink->ctx)
	{
		return;
	}

	console_sink_ctx *ctx = (console_sink_ctx *) sink->ctx;

	/* Don't close stdout/stderr, just flush */
	if (ctx->stream)
	{
		fflush(ctx->stream);
	}

	free(ctx);
	sink->ctx = NULL;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

sink_t *console_sink_create(const console_sink_config *config, log_level level)
{
	if (!config)
	{
		return NULL;
	}

	/* Allocate context */
	console_sink_ctx *ctx = calloc(1, sizeof(console_sink_ctx));
	if (!ctx)
	{
		return NULL;
	}

	ctx->target = config->target;
	ctx->stream = (config->target == CONSOLE_STDOUT) ? stdout : stderr;
	ctx->is_tty = detect_tty(config->target);
	ctx->use_colors = config->use_colors && ctx->is_tty;
	ctx->flush_on_write = config->flush_on_write;

	/* Create sink */
	sink_t *sink = sink_create(ctx, console_sink_write, console_sink_flush,
	                           console_sink_close, level, SINK_TYPE_CONSOLE);
	if (!sink)
	{
		free(ctx);
		return NULL;
	}

	return sink;
}

sink_t *console_sink_create_stdout(log_level level)
{
	console_sink_config config =
			{
					.target = CONSOLE_STDOUT,
					.use_colors = true,
					.flush_on_write = true
			};
	return console_sink_create(&config, level);
}

sink_t *console_sink_create_stderr(log_level level)
{
	console_sink_config config =
			{
					.target = CONSOLE_STDERR,
					.use_colors = true,
					.flush_on_write = true
			};
	return console_sink_create(&config, level);
}

bool console_sink_is_tty(sink_t *sink)
{
	if (!sink || !sink->ctx)
	{
		return false;
	}

	console_sink_ctx *ctx = (console_sink_ctx *) sink->ctx;
	return ctx->is_tty;
}

void console_sink_set_colors(sink_t *sink, bool enable)
{
	if (!sink || !sink->ctx)
	{
		return;
	}

	console_sink_ctx *ctx = (console_sink_ctx *) sink->ctx;
	ctx->use_colors = enable && ctx->is_tty;
}

const char *log_level_color(log_level level)
{
	switch (level)
	{
		case LOG_LEVEL_TRACE:
			return LOG_COLOR_TRACE;
		case LOG_LEVEL_DEBUG:
			return LOG_COLOR_DEBUG;
		case LOG_LEVEL_INFO:
			return LOG_COLOR_INFO;
		case LOG_LEVEL_WARNING:
			return LOG_COLOR_WARN;
		case LOG_LEVEL_ERROR:
			return LOG_COLOR_ERROR;
		case LOG_LEVEL_FATAL:
			return LOG_COLOR_FATAL;
		default:
			return ANSI_RESET;
	}
}

