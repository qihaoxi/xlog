/* =====================================================================================
 *       Filename:  syslog_sink.c
 *    Description:  Syslog sink implementation
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#include "syslog_sink.h"

#if XLOG_HAS_SYSLOG

#include <stdlib.h>
#include <string.h>

/*
 * syslog.h defines LOG_DEBUG, LOG_INFO, etc. which will conflict with our macros.
 * We save the syslog priority values BEFORE including syslog.h won't work because
 * the values come from syslog.h itself. Instead, we use the known numeric values
 * directly for syslog priorities.
 *
 * Standard syslog priority values (from RFC 5424):
 *   LOG_EMERG   = 0
 *   LOG_ALERT   = 1
 *   LOG_CRIT    = 2
 *   LOG_ERR     = 3
 *   LOG_WARNING = 4
 *   LOG_NOTICE  = 5
 *   LOG_INFO    = 6
 *   LOG_DEBUG   = 7
 */

/* Define our own constants for syslog priorities to avoid macro conflicts */
#define XLOG_SYSLOG_EMERG   0
#define XLOG_SYSLOG_ALERT   1
#define XLOG_SYSLOG_CRIT    2
#define XLOG_SYSLOG_ERR     3
#define XLOG_SYSLOG_WARNING 4
#define XLOG_SYSLOG_NOTICE  5
#define XLOG_SYSLOG_INFO    6
#define XLOG_SYSLOG_DEBUG   7

#include <syslog.h>


/* ============================================================================
 * Syslog Sink Context
 * ============================================================================ */

typedef struct syslog_sink_ctx
{
	char *ident;         /* Copy of program identifier */
	syslog_facility facility;       /* Syslog facility */
	bool opened;         /* Whether openlog was called */
} syslog_sink_ctx;

/* ============================================================================
 * Level Mapping
 * ============================================================================ */

int syslog_priority_from_level(xlog_level level)
{
	switch (level)
	{
		case XLOG_LEVEL_TRACE:
			return XLOG_SYSLOG_DEBUG;
		case XLOG_LEVEL_DEBUG:
			return XLOG_SYSLOG_DEBUG;
		case XLOG_LEVEL_INFO:
			return XLOG_SYSLOG_INFO;
		case XLOG_LEVEL_WARNING:
			return XLOG_SYSLOG_WARNING;
		case XLOG_LEVEL_ERROR:
			return XLOG_SYSLOG_ERR;
		case XLOG_LEVEL_FATAL:
			return XLOG_SYSLOG_CRIT;
		default:
			return XLOG_SYSLOG_INFO;
	}
}

/* Convert our facility enum to syslog facility */
static int facility_to_syslog(syslog_facility facility)
{
	switch (facility)
	{
		case SYSLOG_FACILITY_KERN:
			return LOG_KERN;
		case SYSLOG_FACILITY_USER:
			return LOG_USER;
		case SYSLOG_FACILITY_MAIL:
			return LOG_MAIL;
		case SYSLOG_FACILITY_DAEMON:
			return LOG_DAEMON;
		case SYSLOG_FACILITY_AUTH:
			return LOG_AUTH;
		case SYSLOG_FACILITY_SYSLOG:
			return LOG_SYSLOG;
		case SYSLOG_FACILITY_LPR:
			return LOG_LPR;
		case SYSLOG_FACILITY_NEWS:
			return LOG_NEWS;
		case SYSLOG_FACILITY_UUCP:
			return LOG_UUCP;
		case SYSLOG_FACILITY_CRON:
			return LOG_CRON;
		case SYSLOG_FACILITY_AUTHPRIV:
			return LOG_AUTHPRIV;
		case SYSLOG_FACILITY_FTP:
			return LOG_FTP;
		case SYSLOG_FACILITY_LOCAL0:
			return LOG_LOCAL0;
		case SYSLOG_FACILITY_LOCAL1:
			return LOG_LOCAL1;
		case SYSLOG_FACILITY_LOCAL2:
			return LOG_LOCAL2;
		case SYSLOG_FACILITY_LOCAL3:
			return LOG_LOCAL3;
		case SYSLOG_FACILITY_LOCAL4:
			return LOG_LOCAL4;
		case SYSLOG_FACILITY_LOCAL5:
			return LOG_LOCAL5;
		case SYSLOG_FACILITY_LOCAL6:
			return LOG_LOCAL6;
		case SYSLOG_FACILITY_LOCAL7:
			return LOG_LOCAL7;
		default:
			return LOG_USER;
	}
}

/* ============================================================================
 * Sink Interface Implementation
 * ============================================================================ */

static void syslog_sink_write(sink_t *sink, const char *data, size_t len)
{
	if (!sink || !sink->ctx || !data || len == 0)
	{
		return;
	}

	syslog_sink_ctx *ctx = (syslog_sink_ctx *) sink->ctx;
	(void) ctx;  /* Context info already set via openlog */

	/* syslog expects null-terminated string, but we might have newline */
	/* Create a temporary buffer without trailing newline */
	char *msg = NULL;
	size_t msg_len = len;

	/* Strip trailing newline if present */
	while (msg_len > 0 && (data[msg_len - 1] == '\n' || data[msg_len - 1] == '\r'))
	{
		msg_len--;
	}

	if (msg_len == 0)
	{
		return;
	}

	/* If the string is already null-terminated and no modification needed */
	if (data[msg_len] == '\0')
	{
		/* Use directly, extract priority from log level in the message */
		/* For now, use INFO as default since we don't have level info here */
		syslog(XLOG_SYSLOG_INFO, "%.*s", (int) msg_len, data);
	}
	else
	{
		/* Need to create a copy */
		msg = malloc(msg_len + 1);
		if (msg)
		{
			memcpy(msg, data, msg_len);
			msg[msg_len] = '\0';
			syslog(XLOG_SYSLOG_INFO, "%s", msg);
			free(msg);
		}
	}
}

static void syslog_sink_flush(sink_t *sink)
{
	/* Syslog doesn't need explicit flush */
	(void) sink;
}

static void syslog_sink_close(sink_t *sink)
{
	if (!sink || !sink->ctx)
	{
		return;
	}

	syslog_sink_ctx *ctx = (syslog_sink_ctx *) sink->ctx;

	if (ctx->opened)
	{
		closelog();
		ctx->opened = false;
	}

	if (ctx->ident)
	{
		free(ctx->ident);
		ctx->ident = NULL;
	}

	free(ctx);
	sink->ctx = NULL;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

sink_t *syslog_sink_create(const syslog_sink_config *config, xlog_level level)
{
	syslog_sink_config default_config = SYSLOG_SINK_CONFIG_DEFAULT;
	if (!config)
	{
		config = &default_config;
	}

	/* Allocate context */
	syslog_sink_ctx *ctx = calloc(1, sizeof(syslog_sink_ctx));
	if (!ctx)
	{
		return NULL;
	}

	/* Copy ident string */
	if (config->ident)
	{
		ctx->ident = strdup(config->ident);
		if (!ctx->ident)
		{
			free(ctx);
			return NULL;
		}
	}

	ctx->facility = config->facility;

	/* Build syslog options */
	int options = LOG_NDELAY;  /* Open connection immediately */
	if (config->include_pid)
	{
		options |= LOG_PID;
	}
	if (config->log_perror)
	{
		options |= LOG_PERROR;
	}

	/* Open syslog connection */
	openlog(ctx->ident, options, facility_to_syslog(config->facility));
	ctx->opened = true;

	/* Create sink */
	sink_t *sink = sink_create(ctx, syslog_sink_write, syslog_sink_flush,
	                           syslog_sink_close, level, SINK_TYPE_SYSLOG);
	if (!sink)
	{
		closelog();
		if (ctx->ident)
		{
			free(ctx->ident);
		}
		free(ctx);
		return NULL;
	}

	return sink;
}

sink_t *syslog_sink_create_default(const char *ident, xlog_level level)
{
	syslog_sink_config config =
			{
					.ident = ident,
					.facility = SYSLOG_FACILITY_USER,
					.include_pid = true,
					.log_to_stderr = false,
					.log_perror = false
			};
	return syslog_sink_create(&config, level);
}

sink_t *syslog_sink_create_daemon(const char *ident, xlog_level level)
{
	syslog_sink_config config =
			{
					.ident = ident,
					.facility = SYSLOG_FACILITY_DAEMON,
					.include_pid = true,
					.log_to_stderr = false,
					.log_perror = false
			};
	return syslog_sink_create(&config, level);
}

sink_t *syslog_sink_create_with_facility(const char *ident,
                                         syslog_facility facility,
                                         xlog_level level)
{
	syslog_sink_config config =
			{
					.ident = ident,
					.facility = facility,
					.include_pid = true,
					.log_to_stderr = false,
					.log_perror = false
			};
	return syslog_sink_create(&config, level);
}

#endif /* XLOG_HAS_SYSLOG */

