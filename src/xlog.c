/* =====================================================================================
 *       Filename:  xlog.c
 *    Description:  Main xlog implementation with backend thread
 *        Version:  2.0
 *        Created:  2026-02-09
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <errno.h>
#include "platform.h"
#include "xlog_core.h"
#include "formatter.h"
#include "console_sink.h"
#include "ringbuf.h"

/* ============================================================================
 * Internal Logger State
 * ============================================================================ */
typedef struct xlog_state
{
	xlog_config config;
	ring_buffer *queue;              /* Ring buffer for log records */
	pthread_t backend_thread;
	atomic_bool running;
	pthread_mutex_t queue_mutex;     /* Mutex for queue condition */
	pthread_cond_t queue_not_empty;  /* Signal when queue has data */
	pthread_cond_t queue_empty;      /* Signal when queue is drained */
	pthread_mutex_t flush_mutex;
	pthread_cond_t flush_cond;
	sink_manager_t *sinks;
	char *format_buffer;
	char *format_buffer_plain;       /* Buffer for non-colored output */
	pthread_mutex_t format_mutex;
	atomic_uint_fast64_t logged;
	atomic_uint_fast64_t dropped;
	atomic_uint_fast64_t processed;
	atomic_uint_fast64_t flushed;
	atomic_uint_fast64_t format_errors;
	atomic_int min_level;
	atomic_bool initialized;
	bool console_use_colors;         /* Flag for console color support */
	bool has_file_sink;              /* Flag for file sink presence */
} xlog_state;

static xlog_state g_logger = {0};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */
static inline pid_t get_thread_id(void)
{
	/* Use platform abstraction for cross-platform support */
	return (pid_t) xlog_get_tid();
}

static inline uint64_t get_timestamp_ns(void)
{
	/* Use platform abstraction for cross-platform support */
	return xlog_get_timestamp_ns();
}

/* ============================================================================
 * Backend Thread
 * ============================================================================ */
static void process_record(log_record *record)
{
	if (!record || !atomic_load(&record->ready))
	{
		return;
	}

	/* Only lock in sync mode where multiple producer threads may call this.
	 * In async mode, only the single backend thread calls this function. */
	bool need_lock = !g_logger.config.async;
	if (need_lock)
	{
		pthread_mutex_lock(&g_logger.format_mutex);
	}

	int len = 0;

	/* Format based on configured style */
	switch (g_logger.config.format_style)
	{
		case XLOG_OUTPUT_JSON:
			len = log_record_format_json_inline(record, g_logger.format_buffer,
			                                    g_logger.config.format_buffer_size);
			break;

		case XLOG_OUTPUT_RAW:
			/* Just format the message part without metadata */
			len = log_record_format(record, g_logger.format_buffer,
			                        g_logger.config.format_buffer_size);
			break;

		case XLOG_OUTPUT_SIMPLE:
		case XLOG_OUTPUT_DETAILED:
		case XLOG_OUTPUT_DEFAULT:
		default:
			/* Format with colors for console output */
			len = log_record_format_colored(record, g_logger.format_buffer,
			                                g_logger.config.format_buffer_size,
			                                g_logger.console_use_colors);
			break;
	}

	if (len > 0)
	{
		/* For JSON/RAW formats, no need for split formatting */
		if (g_logger.config.format_style == XLOG_OUTPUT_JSON ||
		    g_logger.config.format_style == XLOG_OUTPUT_RAW)
		{
			sink_manager_write(g_logger.sinks, record->level,
			                   g_logger.format_buffer, (size_t) len);
		}
		/* If we have both console and file sinks and colors are enabled,
		 * we need to format separately for file (no colors) */
		else if (g_logger.console_use_colors && g_logger.has_file_sink && g_logger.format_buffer_plain)
		{
			int plain_len = log_record_format(record, g_logger.format_buffer_plain,
			                                  g_logger.config.format_buffer_size);
			/* Write colored to console, plain to files */
			sink_manager_write_split(g_logger.sinks, record->level,
			                         g_logger.format_buffer, (size_t) len,
			                         g_logger.format_buffer_plain,
			                         plain_len > 0 ? (size_t) plain_len : 0);
		}
		else
		{
			sink_manager_write(g_logger.sinks, record->level,
			                   g_logger.format_buffer, (size_t) len);
		}
		atomic_fetch_add(&g_logger.processed, 1);
	}
	else
	{
		atomic_fetch_add(&g_logger.format_errors, 1);
	}

	if (need_lock)
	{
		pthread_mutex_unlock(&g_logger.format_mutex);
	}
}

static void *backend_thread_func(void *arg)
{
	(void) arg;
	uint64_t last_flush_time = get_timestamp_ns();
	uint32_t batch_count = 0;

	while (atomic_load(&g_logger.running))
	{
		log_record *record = NULL;

		/* Wait for data with timeout for periodic flush */
		pthread_mutex_lock(&g_logger.queue_mutex);
		while ((record = rb_peek(g_logger.queue)) == NULL && atomic_load(&g_logger.running))
		{
			/* Wait with timeout for flush interval */
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_nsec += g_logger.config.flush_interval_ms * 1000000L;
			if (ts.tv_nsec >= 1000000000L)
			{
				ts.tv_sec += ts.tv_nsec / 1000000000L;
				ts.tv_nsec %= 1000000000L;
			}

			int ret = pthread_cond_timedwait(&g_logger.queue_not_empty, &g_logger.queue_mutex, &ts);
			if (ret == ETIMEDOUT)
			{
				/* Timeout - do periodic flush */
				uint64_t now = get_timestamp_ns();
				if (now - last_flush_time >= g_logger.config.flush_interval_ms * 1000000ULL)
				{
					sink_manager_flush(g_logger.sinks);
					atomic_fetch_add(&g_logger.flushed, 1);
					last_flush_time = now;
				}
			}
		}
		pthread_mutex_unlock(&g_logger.queue_mutex);

		if (!record)
		{
			continue;
		}

		/* Process the record */
		process_record(record);
		rb_consume(g_logger.queue);
		batch_count++;

		/* Signal that queue might be empty (for flush waiters) */
		if (rb_is_empty(g_logger.queue))
		{
			pthread_mutex_lock(&g_logger.queue_mutex);
			pthread_cond_broadcast(&g_logger.queue_empty);
			pthread_mutex_unlock(&g_logger.queue_mutex);
		}

		/* Batch flush */
		if (g_logger.config.auto_flush && batch_count >= g_logger.config.batch_size)
		{
			sink_manager_flush(g_logger.sinks);
			atomic_fetch_add(&g_logger.flushed, 1);
			last_flush_time = get_timestamp_ns();
			batch_count = 0;
		}
	}

	/* Drain remaining records on shutdown */
	log_record *record;
	while ((record = rb_peek(g_logger.queue)) != NULL)
	{
		process_record(record);
		rb_consume(g_logger.queue);
	}
	sink_manager_flush(g_logger.sinks);

	/* Signal queue is empty for any flush waiters */
	pthread_mutex_lock(&g_logger.queue_mutex);
	pthread_cond_broadcast(&g_logger.queue_empty);
	pthread_mutex_unlock(&g_logger.queue_mutex);

	return NULL;
}

/* ============================================================================
 * Core Logger API Implementation
 * ============================================================================ */
bool xlog_init(void)
{
	xlog_config config =
			{
					.queue_capacity = XLOG_DEFAULT_QUEUE_CAPACITY,
					.format_buffer_size = XLOG_DEFAULT_FORMAT_BUF_SIZE,
					.min_level = LOG_LEVEL_DEBUG,
					.async = true,
					.auto_flush = true,
					.batch_size = XLOG_DEFAULT_BATCH_SIZE,
					.flush_interval_ms = XLOG_DEFAULT_FLUSH_INTERVAL_MS
			};
	return xlog_init_with_config(&config);
}

bool xlog_init_with_config(const xlog_config *config)
{
	if (!config)
	{
		return false;
	}
	if (atomic_load(&g_logger.initialized))
	{
		return false;
	}

	memset(&g_logger, 0, sizeof(g_logger));
	memcpy(&g_logger.config, config, sizeof(xlog_config));

	/* Create ring buffer with DROP policy for async mode */
	rb_full_policy policy = config->async ? RB_POLICY_DROP : RB_POLICY_SPIN;
	g_logger.queue = rb_create(config->queue_capacity, policy);
	if (!g_logger.queue)
	{
		return false;
	}

	g_logger.format_buffer = malloc(config->format_buffer_size);
	if (!g_logger.format_buffer)
	{
		rb_destroy(g_logger.queue);
		return false;
	}

	/* Allocate plain buffer for file sinks (when using colors for console) */
	g_logger.format_buffer_plain = malloc(config->format_buffer_size);
	if (!g_logger.format_buffer_plain)
	{
		free(g_logger.format_buffer);
		rb_destroy(g_logger.queue);
		return false;
	}

	g_logger.sinks = sink_manager_create();
	if (!g_logger.sinks)
	{
		free(g_logger.format_buffer_plain);
		free(g_logger.format_buffer);
		rb_destroy(g_logger.queue);
		return false;
	}

	pthread_mutex_init(&g_logger.format_mutex, NULL);
	pthread_mutex_init(&g_logger.flush_mutex, NULL);
	pthread_mutex_init(&g_logger.queue_mutex, NULL);
	pthread_cond_init(&g_logger.flush_cond, NULL);
	pthread_cond_init(&g_logger.queue_not_empty, NULL);
	pthread_cond_init(&g_logger.queue_empty, NULL);
	atomic_store(&g_logger.min_level, config->min_level);

	if (config->async)
	{
		atomic_store(&g_logger.running, true);
		if (pthread_create(&g_logger.backend_thread, NULL,
		                   backend_thread_func, NULL) != 0)
		{
			sink_manager_destroy(g_logger.sinks);
			free(g_logger.format_buffer_plain);
			free(g_logger.format_buffer);
			rb_destroy(g_logger.queue);
			return false;
		}
	}

	atomic_store(&g_logger.initialized, true);
	return true;
}

void xlog_shutdown(void)
{
	if (!atomic_load(&g_logger.initialized))
	{
		return;
	}

	if (g_logger.config.async)
	{
		atomic_store(&g_logger.running, false);
		/* Wake up backend thread if it's waiting */
		pthread_mutex_lock(&g_logger.queue_mutex);
		pthread_cond_signal(&g_logger.queue_not_empty);
		pthread_mutex_unlock(&g_logger.queue_mutex);
		pthread_join(g_logger.backend_thread, NULL);
	}

	sink_manager_destroy(g_logger.sinks);
	free(g_logger.format_buffer);
	free(g_logger.format_buffer_plain);
	rb_destroy(g_logger.queue);
	pthread_mutex_destroy(&g_logger.format_mutex);
	pthread_mutex_destroy(&g_logger.flush_mutex);
	pthread_mutex_destroy(&g_logger.queue_mutex);
	pthread_cond_destroy(&g_logger.flush_cond);
	pthread_cond_destroy(&g_logger.queue_not_empty);
	pthread_cond_destroy(&g_logger.queue_empty);
	atomic_store(&g_logger.initialized, false);
}

bool xlog_is_initialized(void)
{
	return atomic_load(&g_logger.initialized);
}

void xlog_set_console_colors(bool enable)
{
	g_logger.console_use_colors = enable;
}

void xlog_set_has_file_sink(bool has_file)
{
	g_logger.has_file_sink = has_file;
}

void xlog_set_format_style(xlog_output_format style)
{
	g_logger.config.format_style = style;
}

void xlog_flush(void)
{
	if (!atomic_load(&g_logger.initialized))
	{
		return;
	}

	if (g_logger.config.async)
	{
		/* Wait for queue to drain using condition variable */
		pthread_mutex_lock(&g_logger.queue_mutex);
		while (!rb_is_empty(g_logger.queue))
		{
			pthread_cond_wait(&g_logger.queue_empty, &g_logger.queue_mutex);
		}
		pthread_mutex_unlock(&g_logger.queue_mutex);
	}
	sink_manager_flush(g_logger.sinks);
	atomic_fetch_add(&g_logger.flushed, 1);
}

void xlog_set_level(log_level level)
{
	atomic_store(&g_logger.min_level, level);
}

log_level xlog_get_level(void)
{
	return (log_level) atomic_load(&g_logger.min_level);
}

bool xlog_level_enabled(log_level level)
{
	return level >= (log_level) atomic_load(&g_logger.min_level);
}

void xlog_get_stats(xlog_stats *stats)
{
	if (!stats)
	{
		return;
	}
	stats->logged = atomic_load(&g_logger.logged);
	stats->dropped = atomic_load(&g_logger.dropped);
	stats->processed = atomic_load(&g_logger.processed);
	stats->flushed = atomic_load(&g_logger.flushed);
	stats->format_errors = atomic_load(&g_logger.format_errors);

	/* Also include ringbuf stats */
	if (g_logger.queue)
	{
		rb_stats rb = rb_get_stats(g_logger.queue);
		stats->dropped += rb.dropped;
	}
}

void xlog_reset_stats(void)
{
	atomic_store(&g_logger.logged, 0);
	atomic_store(&g_logger.dropped, 0);
	atomic_store(&g_logger.processed, 0);
	atomic_store(&g_logger.flushed, 0);
	atomic_store(&g_logger.format_errors, 0);
	if (g_logger.queue)
	{
		rb_reset_stats(g_logger.queue);
	}
}

/* ============================================================================
 * Sink Management API Implementation
 * ============================================================================ */
bool xlog_add_sink(sink_t *sink)
{
	if (!atomic_load(&g_logger.initialized) || !sink)
	{
		return false;
	}
	return sink_manager_add(g_logger.sinks, sink);
}

bool xlog_remove_sink(sink_t *sink)
{
	if (!atomic_load(&g_logger.initialized) || !sink)
	{
		return false;
	}
	return sink_manager_remove(g_logger.sinks, sink);
}

size_t xlog_sink_count(void)
{
	if (!atomic_load(&g_logger.initialized))
	{
		return 0;
	}
	return sink_manager_count(g_logger.sinks);
}

/* ============================================================================
 * Logging API Implementation
 * ============================================================================ */
void xlog_log(log_level level, const char *file, uint32_t line,
              const char *func, const char *fmt, ...)
{
	if (!atomic_load(&g_logger.initialized))
	{
		return;
	}
	if (level < (log_level) atomic_load(&g_logger.min_level))
	{
		return;
	}

	/* Reserve a slot in the ring buffer */
	log_record *record = rb_reserve(g_logger.queue);
	if (!record)
	{
		atomic_fetch_add(&g_logger.dropped, 1);
		return;
	}

	/* Fill in the record */
	record->level = level;
	record->timestamp_ns = get_timestamp_ns();
	record->thread_id = get_thread_id();
	record->loc.file = file;
	record->loc.func = func;
	record->loc.line = line;
	record->fmt = fmt;

	/* Parse format string and add arguments */
	va_list args;
	va_start(args, fmt);
	if (fmt)
	{
		const char *p = fmt;
		while (*p && record->arg_count < LOG_MAX_ARGS)
		{
			if (*p == '{' && *(p + 1) == '}')
			{
				int64_t val = va_arg(args, int64_t);
				log_record_add_arg(record, LOG_ARG_I64, (uint64_t) val);
				p += 2;
			}
			else if (*p == '%' && *(p + 1) != '%' && *(p + 1) != '\0')
			{
				char spec = *(p + 1);
				if (spec == 'd' || spec == 'i')
				{
					log_record_add_arg(record, LOG_ARG_I32, (uint64_t) va_arg(args, int));
				}
				else if (spec == 'l')
				{
					log_record_add_arg(record, LOG_ARG_I64, (uint64_t) va_arg(args, int64_t));
				}
				else if (spec == 'u')
				{
					log_record_add_arg(record, LOG_ARG_U32, (uint64_t) va_arg(args, unsigned int));
				}
				else if (spec == 's')
				{
					/* 使用安全版本，会深拷贝或截断，避免异步模式下的 use-after-free */
					log_record_add_string_safe(record, va_arg(args, const char *), false);
				}
				else if (spec == 'f')
				{
					double d = va_arg(args, double);
					uint64_t raw;
					memcpy(&raw, &d, sizeof(raw));
					log_record_add_arg(record, LOG_ARG_F64, raw);
				}
				else if (spec == 'p')
				{
					log_record_add_arg(record, LOG_ARG_PTR, (uint64_t) (uintptr_t) va_arg(args, void *));
				}
				p += 2;
			}
			else
			{
				p++;
			}
		}
	}
	va_end(args);

	/* Commit the record */
	rb_commit(g_logger.queue, record);
	atomic_fetch_add(&g_logger.logged, 1);

	/* Signal backend thread that data is available */
	if (g_logger.config.async)
	{
		pthread_mutex_lock(&g_logger.queue_mutex);
		pthread_cond_signal(&g_logger.queue_not_empty);
		pthread_mutex_unlock(&g_logger.queue_mutex);
	}
	else
	{
		/* In sync mode, process immediately */
		process_record(record);
		rb_consume(g_logger.queue);
	}
}

void xlog_log_ctx(log_level level, const log_context *ctx,
                  const char *file, uint32_t line, const char *func,
                  const char *fmt, ...)
{
	if (!atomic_load(&g_logger.initialized))
	{
		return;
	}
	if (level < (log_level) atomic_load(&g_logger.min_level))
	{
		return;
	}

	/* Reserve a slot in the ring buffer */
	log_record *record = rb_reserve(g_logger.queue);
	if (!record)
	{
		atomic_fetch_add(&g_logger.dropped, 1);
		return;
	}

	/* Fill in the record */
	record->level = level;
	record->timestamp_ns = get_timestamp_ns();
	record->thread_id = get_thread_id();
	record->loc.file = file;
	record->loc.func = func;
	record->loc.line = line;
	record->fmt = fmt;

	/* Copy context if provided */
	if (ctx)
	{
		record->ctx = *ctx;
	}

	/* Parse format string and add arguments */
	va_list args;
	va_start(args, fmt);
	if (fmt)
	{
		const char *p = fmt;
		while (*p && record->arg_count < LOG_MAX_ARGS)
		{
			if (*p == '{' && *(p + 1) == '}')
			{
				int64_t val = va_arg(args, int64_t);
				log_record_add_arg(record, LOG_ARG_I64, (uint64_t) val);
				p += 2;
			}
			else if (*p == '%' && *(p + 1) != '%' && *(p + 1) != '\0')
			{
				char spec = *(p + 1);
				if (spec == 'd' || spec == 'i')
				{
					log_record_add_arg(record, LOG_ARG_I32, (uint64_t) va_arg(args, int));
				}
				else if (spec == 'l')
				{
					log_record_add_arg(record, LOG_ARG_I64, (uint64_t) va_arg(args, int64_t));
				}
				else if (spec == 'u')
				{
					log_record_add_arg(record, LOG_ARG_U32, (uint64_t) va_arg(args, unsigned int));
				}
				else if (spec == 's')
				{
					/* 使用安全版本，会深拷贝或截断，避免异步模式下的 use-after-free */
					log_record_add_string_safe(record, va_arg(args, const char *), false);
				}
				else if (spec == 'f')
				{
					double d = va_arg(args, double);
					uint64_t raw;
					memcpy(&raw, &d, sizeof(raw));
					log_record_add_arg(record, LOG_ARG_F64, raw);
				}
				else if (spec == 'p')
				{
					log_record_add_arg(record, LOG_ARG_PTR, (uint64_t) (uintptr_t) va_arg(args, void *));
				}
				p += 2;
			}
			else
			{
				p++;
			}
		}
	}
	va_end(args);

	/* Commit the record */
	rb_commit(g_logger.queue, record);
	atomic_fetch_add(&g_logger.logged, 1);

	/* Signal backend thread that data is available */
	if (g_logger.config.async)
	{
		pthread_mutex_lock(&g_logger.queue_mutex);
		pthread_cond_signal(&g_logger.queue_not_empty);
		pthread_mutex_unlock(&g_logger.queue_mutex);
	}
	else
	{
		/* In sync mode, process immediately */
		process_record(record);
		rb_consume(g_logger.queue);
	}
}

bool xlog_submit(log_record *record)
{
	if (!atomic_load(&g_logger.initialized) || !record)
	{
		return false;
	}
	if (record->level < (log_level) atomic_load(&g_logger.min_level))
	{
		return false;
	}

	/* Push the record to ring buffer */
	if (!rb_push(g_logger.queue, record))
	{
		atomic_fetch_add(&g_logger.dropped, 1);
		return false;
	}

	atomic_fetch_add(&g_logger.logged, 1);

	/* Signal backend thread that data is available */
	if (g_logger.config.async)
	{
		pthread_mutex_lock(&g_logger.queue_mutex);
		pthread_cond_signal(&g_logger.queue_not_empty);
		pthread_mutex_unlock(&g_logger.queue_mutex);
	}
	else
	{
		/* In sync mode, process immediately */
		log_record *slot = rb_peek(g_logger.queue);
		if (slot)
		{
			process_record(slot);
			rb_consume(g_logger.queue);
		}
	}
	return true;
}
