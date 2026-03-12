/* =====================================================================================
 *       Filename:  syslog_sink.h
 *    Description:  Syslog sink for system logging (Linux/macOS/BSD)
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#ifndef XLOG_SYSLOG_SINK_H
#define XLOG_SYSLOG_SINK_H

#include <stdbool.h>
#include "sink.h"
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Syslog is available on POSIX systems (Linux, macOS, BSD) */
#if defined(XLOG_PLATFORM_LINUX) || defined(XLOG_PLATFORM_MACOS) || defined(XLOG_PLATFORM_BSD)
#define XLOG_HAS_SYSLOG 1
#else
#define XLOG_HAS_SYSLOG 0
#endif

#if XLOG_HAS_SYSLOG

/* ============================================================================
 * Syslog Facility Codes
 * ============================================================================
 * Standard syslog facilities as defined in RFC 5424
 */
typedef enum syslog_facility
{
	SYSLOG_FACILITY_KERN = 0,   /* Kernel messages */
	SYSLOG_FACILITY_USER = 1,   /* User-level messages (default) */
	SYSLOG_FACILITY_MAIL = 2,   /* Mail system */
	SYSLOG_FACILITY_DAEMON = 3,   /* System daemons */
	SYSLOG_FACILITY_AUTH = 4,   /* Security/authorization */
	SYSLOG_FACILITY_SYSLOG = 5,   /* Syslogd internal messages */
	SYSLOG_FACILITY_LPR = 6,   /* Line printer subsystem */
	SYSLOG_FACILITY_NEWS = 7,   /* Network news subsystem */
	SYSLOG_FACILITY_UUCP = 8,   /* UUCP subsystem */
	SYSLOG_FACILITY_CRON = 9,   /* Clock daemon */
	SYSLOG_FACILITY_AUTHPRIV = 10,  /* Security/authorization (private) */
	SYSLOG_FACILITY_FTP = 11,  /* FTP daemon */
	SYSLOG_FACILITY_LOCAL0 = 16,  /* Local use 0 */
	SYSLOG_FACILITY_LOCAL1 = 17,  /* Local use 1 */
	SYSLOG_FACILITY_LOCAL2 = 18,  /* Local use 2 */
	SYSLOG_FACILITY_LOCAL3 = 19,  /* Local use 3 */
	SYSLOG_FACILITY_LOCAL4 = 20,  /* Local use 4 */
	SYSLOG_FACILITY_LOCAL5 = 21,  /* Local use 5 */
	SYSLOG_FACILITY_LOCAL6 = 22,  /* Local use 6 */
	SYSLOG_FACILITY_LOCAL7 = 23,  /* Local use 7 */
} syslog_facility;

/* ============================================================================
 * Syslog Sink Configuration
 * ============================================================================ */
typedef struct syslog_sink_config
{
	const char *ident;         /* Program identifier (appears in syslog) */
	syslog_facility facility;       /* Syslog facility */
	bool include_pid;    /* Include PID in syslog messages */
	bool log_to_stderr;  /* Also log to stderr (for debugging) */
	bool log_perror;     /* Use LOG_PERROR option */
} syslog_sink_config;

/* Default configuration */
#define SYSLOG_SINK_CONFIG_DEFAULT { \
    .ident = NULL,                   \
    .facility = SYSLOG_FACILITY_USER, \
    .include_pid = true,             \
    .log_to_stderr = false,          \
    .log_perror = false              \
}

/* ============================================================================
 * Syslog Sink API
 * ============================================================================ */

/**
 * Create a syslog sink with the specified configuration.
 *
 * @param config    Configuration (NULL for defaults)
 * @param level     Minimum log level for this sink
 * @return          Pointer to the created sink, or NULL on failure
 */
sink_t *syslog_sink_create(const syslog_sink_config *config, xlog_level level);

/**
 * Create a syslog sink with default settings.
 * Uses USER facility and includes PID.
 *
 * @param ident     Program identifier (usually program name)
 * @param level     Minimum log level
 * @return          Pointer to the created sink, or NULL on failure
 */
sink_t *syslog_sink_create_default(const char *ident, xlog_level level);

/**
 * Create a syslog sink for daemon processes.
 * Uses DAEMON facility with PID.
 *
 * @param ident     Program identifier
 * @param level     Minimum log level
 * @return          Pointer to the created sink, or NULL on failure
 */
sink_t *syslog_sink_create_daemon(const char *ident, xlog_level level);

/**
 * Create a syslog sink with a specific facility.
 *
 * @param ident     Program identifier
 * @param facility  Syslog facility
 * @param level     Minimum log level
 * @return          Pointer to the created sink, or NULL on failure
 */
sink_t *syslog_sink_create_with_facility(const char *ident,
                                         syslog_facility facility,
                                         xlog_level level);

/**
 * Get the syslog priority for a log level.
 * Maps xlog levels to syslog priorities.
 *
 * @param level     Log level
 * @return          Syslog priority
 */
int syslog_priority_from_level(xlog_level level);

#else /* !XLOG_HAS_SYSLOG */

/* Stub for non-POSIX platforms */
static inline sink_t *syslog_sink_create(const void *config, xlog_level level)
{
	(void)config; (void)level;
	return NULL;
}

static inline sink_t *syslog_sink_create_default(const char *ident, xlog_level level)
{
	(void)ident; (void)level;
	return NULL;
}

#endif /* XLOG_HAS_SYSLOG */

#ifdef __cplusplus
}
#endif

#endif /* XLOG_SYSLOG_SINK_H */

