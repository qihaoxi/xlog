/* =====================================================================================
 *       Filename:  xlog.c
 *    Description:  Main xlog implementation with backend thread
 *        Version:  2.0
 *        Created:  2026-02-09
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include "platform.h"

/* MSVC compatibility for stdatomic */
#ifdef _MSC_VER
    #if _MSC_VER >= 1930  /* Visual Studio 2022+ */
        #include <stdatomic.h>
    #endif
    /* Older MSVC uses platform.h fallback */
#else
    #include <stdatomic.h>
#endif

#include "xlog_core.h"
#include "formatter.h"
#include "console_sink.h"
#include "ringbuf.h"

/* ============================================================================
 * Adaptive Spin Configuration
 * ============================================================================
 * Design rationale: We use lock-free polling instead of condition variables
 * because:
 * 1. Producer path stays lock-free (only atomic_store for wakeup flag)
 * 2. Under high load, backend never sleeps (queue always has data)
 * 3. Under low load, 100µs latency is acceptable
 * 4. Avoids mutex contention with multiple producer threads
 *
 * The adaptive back-off has 3 phases:
 * - Phase 1: Busy spin with CPU pause (lowest latency, ~1µs total)
 * - Phase 2: Yield to other threads (~10-100µs total)
 * - Phase 3: Sleep to save CPU (100µs per iteration)
 * ============================================================================ */
#define BACKEND_SPIN_COUNT      64    /* Busy-spin iterations with CPU_PAUSE */
#define BACKEND_YIELD_COUNT     32    /* Yield iterations before sleeping */
#define BACKEND_SLEEP_US        100   /* Microseconds to sleep when idle */

/* ============================================================================
 * Internal Logger State
 * ============================================================================ */
typedef struct xlog_state
{
	xlog_config config;
	ring_buffer *queue;              /* Lock-free MPSC ring buffer */
	xlog_thread_t backend_thread;
	atomic_bool running;

	/* Lightweight producer->backend notification (no mutex needed) */
	atomic_bool wakeup;              /* Producers set this to wake backend */

	/* Flush handshake: caller sets flush_requested, backend processes
	 * and sets flush_done, then signals flush_cond */
	atomic_bool flush_requested;
	atomic_bool flush_done;
	xlog_mutex_t flush_mutex;       /* Only for xlog_flush() callers to block */
	xlog_cond_t flush_cond;

	sink_manager_t *sinks;
	char *format_buffer;
	char *format_buffer_plain;       /* Buffer for non-colored output */
	xlog_mutex_t format_mutex;       /* Only needed in sync mode */
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

static xlog_state g_logger;

/* Thread-local inline buffer for sync-mode log records.
 * In sync mode, records are created on the stack and processed immediately,
 * so a per-thread buffer avoids heap allocation. */
static XLOG_THREAD_LOCAL char g_sync_inline_buf[LOG_INLINE_BUF_SIZE];

/* ============================================================================
 * Helper Functions
 * ============================================================================ */
static inline uint32_t get_thread_id(void)
{
	/* Use platform abstraction for cross-platform support */
	return (uint32_t)xlog_get_tid();
}

static inline uint64_t get_timestamp_ns(void)
{
	/* Use platform abstraction for cross-platform support */
	return xlog_get_timestamp_ns();
}

/**
 * Parse printf-style format string and extract arguments into log record.
 * Supports: %d, %i, %u, %x, %X, %o, %s, %f, %e, %E, %g, %G, %a, %A, %p, %c
 * Length modifiers: %zu, %zd, %ld, %lu, %lld, %llu, %hd, %hu, %hhd, %hhu, etc.
 * Also supports width, precision, and flags (-, +, space, #, 0)
 */
static void parse_format_args(log_record *record, const char *fmt, va_list args)
{
	const log_fmt_plan *plan;
	uint8_t i;

	if (!fmt)
	{
		return;
	}
	if (strchr(fmt, '%') == NULL && strchr(fmt, '{') == NULL)
	{
		return;
	}

	plan = log_format_get_plan(fmt);
	if (plan && plan->has_placeholders)
	{
		for (i = 0; i < plan->token_count && record->arg_count < LOG_MAX_ARGS; ++i)
		{
			switch ((log_fmt_arg_kind) plan->tokens[i].kind)
			{
				case LOG_FMT_ARG_KIND_BRACE_I64:
					log_record_add_arg(record, LOG_ARG_I64, (uint64_t) va_arg(args, int64_t));
					break;

				case LOG_FMT_ARG_KIND_I32:
				case LOG_FMT_ARG_KIND_CHAR:
					log_record_add_arg(record, LOG_ARG_I32, (uint64_t) va_arg(args, int));
					break;

				case LOG_FMT_ARG_KIND_U32:
					log_record_add_arg(record, LOG_ARG_U32, (uint64_t) va_arg(args, unsigned int));
					break;

				case LOG_FMT_ARG_KIND_LONG:
					log_record_add_arg(record, LOG_ARG_I64, (uint64_t) va_arg(args, long));
					break;

				case LOG_FMT_ARG_KIND_ULONG:
					log_record_add_arg(record, LOG_ARG_U64, (uint64_t) va_arg(args, unsigned long));
					break;

				case LOG_FMT_ARG_KIND_LLONG:
					log_record_add_arg(record, LOG_ARG_I64, (uint64_t) va_arg(args, long long));
					break;

				case LOG_FMT_ARG_KIND_ULLONG:
					log_record_add_arg(record, LOG_ARG_U64, (uint64_t) va_arg(args, unsigned long long));
					break;

				case LOG_FMT_ARG_KIND_SIZE:
					log_record_add_arg(record, LOG_ARG_U64, (uint64_t) va_arg(args, size_t));
					break;

				case LOG_FMT_ARG_KIND_STRING:
					log_record_add_string_safe(record, va_arg(args, const char *), false);
					break;

				case LOG_FMT_ARG_KIND_F64:
				{
					double d = va_arg(args, double);
					uint64_t raw;
					memcpy(&raw, &d, sizeof(raw));
					log_record_add_arg(record, LOG_ARG_F64, raw);
					break;
				}

				case LOG_FMT_ARG_KIND_LONG_DOUBLE:
				{
					long double ld = va_arg(args, long double);
					double d = (double) ld;
					uint64_t raw;
					memcpy(&raw, &d, sizeof(raw));
					log_record_add_arg(record, LOG_ARG_F64, raw);
					break;
				}

				case LOG_FMT_ARG_KIND_PTR:
					log_record_add_arg(record, LOG_ARG_PTR,
					                   (uint64_t) (uintptr_t) va_arg(args, void *));
					break;
			}
		}
		return;
	}

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
			const char *spec_start = p + 1;
			/* Skip flags: -, +, space, #, 0 */
			while (*spec_start == '-' || *spec_start == '+' || *spec_start == ' ' ||
			       *spec_start == '#' || *spec_start == '0')
			{
				spec_start++;
			}
			/* Skip width */
			while (*spec_start >= '0' && *spec_start <= '9')
			{
				spec_start++;
			}
			/* Skip precision */
			if (*spec_start == '.')
			{
				spec_start++;
				while (*spec_start >= '0' && *spec_start <= '9')
				{
					spec_start++;
				}
			}
			/* Check length modifier and conversion specifier */
			char c1 = *spec_start;
			char c2 = (c1 != '\0') ? *(spec_start + 1) : '\0';

			if (c1 == 'z' && (c2 == 'u' || c2 == 'd' || c2 == 'i' || c2 == 'x' || c2 == 'X' || c2 == 'o'))
			{
				/* %zu, %zd, %zi, %zx, %zX, %zo - size_t */
				log_record_add_arg(record, LOG_ARG_U64, (uint64_t) va_arg(args, size_t));
				p = spec_start + 2;
			}
			else if (c1 == 'l' && c2 == 'l')
			{
				/* %lld, %llu, %llx, etc. - long long */
				char c3 = *(spec_start + 2);
				if (c3 == 'd' || c3 == 'i')
				{
					log_record_add_arg(record, LOG_ARG_I64, (uint64_t) va_arg(args, long long));
				}
				else
				{
					log_record_add_arg(record, LOG_ARG_U64, (uint64_t) va_arg(args, unsigned long long));
				}
				p = spec_start + 3;
			}
			else if (c1 == 'l' && (c2 == 'd' || c2 == 'i'))
			{
				/* %ld, %li - long */
				log_record_add_arg(record, LOG_ARG_I64, (uint64_t) va_arg(args, long));
				p = spec_start + 2;
			}
			else if (c1 == 'l' && (c2 == 'u' || c2 == 'x' || c2 == 'X' || c2 == 'o'))
			{
				/* %lu, %lx, %lX, %lo - unsigned long */
				log_record_add_arg(record, LOG_ARG_U64, (uint64_t) va_arg(args, unsigned long));
				p = spec_start + 2;
			}
			else if (c1 == 'h' && c2 == 'h')
			{
				/* %hhd, %hhu - char */
				char c3 = *(spec_start + 2);
				if (c3 == 'd' || c3 == 'i')
				{
					log_record_add_arg(record, LOG_ARG_I32, (uint64_t) (int) va_arg(args, int));
				}
				else
				{
					log_record_add_arg(record, LOG_ARG_U32, (uint64_t) (unsigned int) va_arg(args, unsigned int));
				}
				p = spec_start + 3;
			}
			else if (c1 == 'h' && (c2 == 'd' || c2 == 'i' || c2 == 'u' || c2 == 'x' || c2 == 'X' || c2 == 'o'))
			{
				/* %hd, %hi, %hu, etc. - short */
				if (c2 == 'd' || c2 == 'i')
				{
					log_record_add_arg(record, LOG_ARG_I32, (uint64_t) (int) va_arg(args, int));
				}
				else
				{
					log_record_add_arg(record, LOG_ARG_U32, (uint64_t) (unsigned int) va_arg(args, unsigned int));
				}
				p = spec_start + 2;
			}
			else if (c1 == 'd' || c1 == 'i')
			{
				log_record_add_arg(record, LOG_ARG_I32, (uint64_t) va_arg(args, int));
				p = spec_start + 1;
			}
			else if (c1 == 'u' || c1 == 'x' || c1 == 'X' || c1 == 'o')
			{
				log_record_add_arg(record, LOG_ARG_U32, (uint64_t) va_arg(args, unsigned int));
				p = spec_start + 1;
			}
			else if (c1 == 's')
			{
				/* Use safe version to deep copy or truncate, avoiding use-after-free in async mode */
				log_record_add_string_safe(record, va_arg(args, const char *), false);
				p = spec_start + 1;
			}
			else if (c1 == 'f' || c1 == 'e' || c1 == 'E' || c1 == 'g' || c1 == 'G' || c1 == 'a' || c1 == 'A')
			{
				double d = va_arg(args, double);
				uint64_t raw;
				memcpy(&raw, &d, sizeof(raw));
				log_record_add_arg(record, LOG_ARG_F64, raw);
				p = spec_start + 1;
			}
			else if (c1 == 'L' && (c2 == 'f' || c2 == 'e' || c2 == 'E' || c2 == 'g' || c2 == 'G'))
			{
				/* %Lf - long double (treated as double for now) */
				long double ld = va_arg(args, long double);
				double d = (double) ld;
				uint64_t raw;
				memcpy(&raw, &d, sizeof(raw));
				log_record_add_arg(record, LOG_ARG_F64, raw);
				p = spec_start + 2;
			}
			else if (c1 == 'p')
			{
				log_record_add_arg(record, LOG_ARG_PTR, (uint64_t) (uintptr_t) va_arg(args, void *));
				p = spec_start + 1;
			}
			else if (c1 == 'c')
			{
				log_record_add_arg(record, LOG_ARG_I32, (uint64_t) va_arg(args, int));
				p = spec_start + 1;
			}
			else if (c1 == 'n')
			{
				/* %n is not supported, skip but consume argument */
				(void) va_arg(args, int *);
				p = spec_start + 1;
			}
			else
			{
				/* Unknown specifier, just advance */
				p++;
			}
		}
		else
		{
			p++;
		}
	}
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
		xlog_mutex_lock(&g_logger.format_mutex);
	}

	int len = 0;
	bool use_colors = g_logger.console_use_colors;

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
			/* Keep colored path unchanged; use faster inline default formatter
			 * for the common non-colored path. */
			if (use_colors)
			{
				len = log_record_format_colored(record, g_logger.format_buffer,
				                                g_logger.config.format_buffer_size,
				                                true);
			}
			else
			{
				len = log_record_format_default_inline(record, g_logger.format_buffer,
				                                      g_logger.config.format_buffer_size);
			}
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
		else if (use_colors && g_logger.has_file_sink && g_logger.format_buffer_plain)
		{
			int plain_len = log_record_format_default_inline(record, g_logger.format_buffer_plain,
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
		xlog_mutex_unlock(&g_logger.format_mutex);
	}
}

static void *backend_thread_func(void *arg)
{
	(void) arg;
	uint64_t last_flush_time = get_timestamp_ns();
	uint32_t batch_count = 0;
	uint32_t idle_count = 0;

	while (atomic_load(&g_logger.running))
	{
		log_record *record = rb_peek(g_logger.queue);

		if (record)
		{
			/* Process the record (zero-copy from ring buffer) */
			process_record(record);
			rb_consume(g_logger.queue);
			batch_count++;
			idle_count = 0;

			/* Batch flush */
			if (g_logger.config.auto_flush && batch_count >= g_logger.config.batch_size)
			{
				sink_manager_flush(g_logger.sinks);
				atomic_fetch_add(&g_logger.flushed, 1);
				last_flush_time = get_timestamp_ns();
				batch_count = 0;
			}
		}
		else
		{
			/* Queue is empty - adaptive back-off */
			idle_count++;

			if (idle_count <= BACKEND_SPIN_COUNT)
			{
				/* Phase 1: Busy spin with CPU pause */
				XLOG_CPU_PAUSE();
			}
			else if (idle_count <= BACKEND_SPIN_COUNT + BACKEND_YIELD_COUNT)
			{
				/* Phase 2: Yield to other threads */
				xlog_thread_yield();
			}
			else
			{
				/* Phase 3: Short sleep to save CPU */
				xlog_sleep_us(BACKEND_SLEEP_US);
			}

			/* Periodic flush during idle */
			uint64_t now = get_timestamp_ns();
			if (now - last_flush_time >= g_logger.config.flush_interval_ms * 1000000ULL)
			{
				if (batch_count > 0)
				{
					sink_manager_flush(g_logger.sinks);
					atomic_fetch_add(&g_logger.flushed, 1);
					batch_count = 0;
				}
				last_flush_time = now;
			}
		}

		/* Check wakeup flag from producers */
		if (atomic_load(&g_logger.wakeup))
		{
			atomic_store(&g_logger.wakeup, false);
			idle_count = 0;  /* Reset back-off on wakeup */
		}

		/* Handle flush request from xlog_flush() */
		if (atomic_load(&g_logger.flush_requested))
		{
			/* Drain all pending records */
			log_record *rec;
			while ((rec = rb_peek(g_logger.queue)) != NULL)
			{
				process_record(rec);
				rb_consume(g_logger.queue);
			}
			sink_manager_flush(g_logger.sinks);
			atomic_fetch_add(&g_logger.flushed, 1);
			last_flush_time = get_timestamp_ns();
			batch_count = 0;

			/* Signal flush completion */
			atomic_store(&g_logger.flush_requested, false);
			xlog_mutex_lock(&g_logger.flush_mutex);
			atomic_store(&g_logger.flush_done, true);
			xlog_cond_signal(&g_logger.flush_cond);
			xlog_mutex_unlock(&g_logger.flush_mutex);
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

	/* Signal flush completion for any waiting xlog_flush() callers */
	xlog_mutex_lock(&g_logger.flush_mutex);
	atomic_store(&g_logger.flush_done, true);
	xlog_cond_broadcast(&g_logger.flush_cond);
	xlog_mutex_unlock(&g_logger.flush_mutex);

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
					.min_level = XLOG_LEVEL_DEBUG,
					.async = true,
					.queue_full_policy = RB_POLICY_DROP,
					.queue_spin_timeout_ns = 0,
					.queue_block_timeout_ns = 0,
					.auto_flush = true,
					.batch_size = XLOG_DEFAULT_BATCH_SIZE,
					.flush_interval_ms = XLOG_DEFAULT_FLUSH_INTERVAL_MS,
					.format_style = XLOG_OUTPUT_DEFAULT
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

	/* Create ring buffer with configured full-queue policy. Sync mode still
	 * allocates the queue for API compatibility, but uses a non-dropping policy. */
	rb_config rb_cfg = {
		.capacity = config->queue_capacity,
		.policy = config->async ? config->queue_full_policy : RB_POLICY_SPIN,
		.spin_timeout_ns = config->queue_spin_timeout_ns,
		.block_timeout_ns = config->queue_block_timeout_ns
	};
	g_logger.queue = rb_create_with_config(&rb_cfg);
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

	xlog_mutex_init(&g_logger.format_mutex);
	xlog_mutex_init(&g_logger.flush_mutex);
	xlog_cond_init(&g_logger.flush_cond);
	atomic_store(&g_logger.min_level, config->min_level);
	atomic_store(&g_logger.wakeup, false);
	atomic_store(&g_logger.flush_requested, false);
	atomic_store(&g_logger.flush_done, false);

	if (config->async)
	{
		atomic_store(&g_logger.running, true);
		if (xlog_thread_create(&g_logger.backend_thread,
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
		/* Wake up backend thread if it's sleeping in adaptive wait */
		atomic_store(&g_logger.wakeup, true);
		xlog_thread_join(g_logger.backend_thread, NULL);
	}

	sink_manager_destroy(g_logger.sinks);
	free(g_logger.format_buffer);
	free(g_logger.format_buffer_plain);
	rb_destroy(g_logger.queue);
	xlog_mutex_destroy(&g_logger.format_mutex);
	xlog_mutex_destroy(&g_logger.flush_mutex);
	xlog_cond_destroy(&g_logger.flush_cond);
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
		/* Request flush from backend thread via atomic handshake */
		atomic_store(&g_logger.flush_done, false);
		atomic_store(&g_logger.flush_requested, true);
		atomic_store(&g_logger.wakeup, true);  /* Wake backend if sleeping */

		/* Wait for backend to complete the flush */
		xlog_mutex_lock(&g_logger.flush_mutex);
		while (!atomic_load(&g_logger.flush_done))
		{
			xlog_cond_timedwait(&g_logger.flush_cond, &g_logger.flush_mutex, 100);
		}
		xlog_mutex_unlock(&g_logger.flush_mutex);
	}
	else
	{
		sink_manager_flush(g_logger.sinks);
		atomic_fetch_add(&g_logger.flushed, 1);
	}
}

void xlog_set_level(xlog_level level)
{
	atomic_store(&g_logger.min_level, level);
}

xlog_level xlog_get_level(void)
{
	return (xlog_level) atomic_load(&g_logger.min_level);
}

bool xlog_level_enabled(xlog_level level)
{
	return level >= (xlog_level) atomic_load(&g_logger.min_level);
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
void xlog_log(xlog_level level, const char *file, uint32_t line,
              const char *func, const char *fmt, ...)
{
	log_record sync_record;
	log_record *record;
	bool async_mode;

	if (!atomic_load(&g_logger.initialized))
	{
		return;
	}
	if (level < (xlog_level) atomic_load(&g_logger.min_level))
	{
		return;
	}

	async_mode = g_logger.config.async;
	if (async_mode)
	{
		/* Reserve a slot in the ring buffer */
		record = rb_reserve(g_logger.queue);
		if (!record)
		{
			atomic_fetch_add(&g_logger.dropped, 1);
			return;
		}
	}
	else
	{
		log_record_init_with_buf(&sync_record, g_sync_inline_buf, LOG_INLINE_BUF_SIZE);
		record = &sync_record;
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
	parse_format_args(record, fmt, args);
	va_end(args);

	/* Commit the record */
	if (async_mode)
	{
		rb_commit(g_logger.queue, record);
	}
	else
	{
		log_record_commit(record);
	}
	atomic_fetch_add(&g_logger.logged, 1);

	/* Signal backend thread that data is available */
	if (async_mode)
	{
		atomic_store(&g_logger.wakeup, true);
	}
	else
	{
		process_record(record);
	}
}

void xlog_log_ctx(xlog_level level, const log_context *ctx,
                  const char *file, uint32_t line, const char *func,
                  const char *fmt, ...)
{
	log_record sync_record;
	log_record *record;
	bool async_mode;

	if (!atomic_load(&g_logger.initialized))
	{
		return;
	}
	if (level < (xlog_level) atomic_load(&g_logger.min_level))
	{
		return;
	}

	async_mode = g_logger.config.async;
	if (async_mode)
	{
		/* Reserve a slot in the ring buffer */
		record = rb_reserve(g_logger.queue);
		if (!record)
		{
			atomic_fetch_add(&g_logger.dropped, 1);
			return;
		}
	}
	else
	{
		log_record_init_with_buf(&sync_record, g_sync_inline_buf, LOG_INLINE_BUF_SIZE);
		record = &sync_record;
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
	parse_format_args(record, fmt, args);
	va_end(args);

	/* Commit the record */
	if (async_mode)
	{
		rb_commit(g_logger.queue, record);
	}
	else
	{
		log_record_commit(record);
	}
	atomic_fetch_add(&g_logger.logged, 1);

	/* Signal backend thread that data is available */
	if (async_mode)
	{
		atomic_store(&g_logger.wakeup, true);
	}
	else
	{
		process_record(record);
	}
}

bool xlog_submit(log_record *record)
{
	bool async_mode;

	if (!atomic_load(&g_logger.initialized) || !record)
	{
		return false;
	}
	if (record->level < (xlog_level) atomic_load(&g_logger.min_level))
	{
		return false;
	}

	async_mode = g_logger.config.async;
	if (!async_mode)
	{
		if (!atomic_load(&record->ready))
		{
			log_record_commit(record);
		}
		atomic_fetch_add(&g_logger.logged, 1);
		process_record(record);
		return true;
	}

	/* Reserve a slot and copy record data into it */
	log_record *slot = rb_reserve(g_logger.queue);
	if (!slot)
	{
		atomic_fetch_add(&g_logger.dropped, 1);
		return false;
	}

	/* Copy record fields (rb_reserve already reset the slot) */
	slot->level = record->level;
	slot->timestamp_ns = record->timestamp_ns;
	slot->thread_id = record->thread_id;
	slot->loc = record->loc;
	slot->fmt = record->fmt;
	slot->ctx = record->ctx;
	slot->arg_count = record->arg_count;
	slot->field_count = record->field_count;
	memcpy(slot->arg_types, record->arg_types, record->arg_count * sizeof(record->arg_types[0]));
	memcpy(slot->arg_values, record->arg_values, record->arg_count * sizeof(record->arg_values[0]));
	if (record->inline_buf_used > 0 && record->inline_buf && slot->inline_buf)
	{
		const char *src_buf = record->inline_buf;
		uint16_t copy_len = record->inline_buf_used;
		if (copy_len > slot->inline_buf_capacity)
		{
			copy_len = slot->inline_buf_capacity;
		}
		memcpy(slot->inline_buf, src_buf, copy_len);
		slot->inline_buf_used = copy_len;
		/* Fix up STR_INLINE arg pointers from source buffer to slot buffer */
		log_record_fixup_inline_ptrs(slot, src_buf);
	}

	rb_commit(g_logger.queue, slot);
	atomic_fetch_add(&g_logger.logged, 1);

	/* Signal backend thread that data is available */
	if (async_mode)
	{
		atomic_store(&g_logger.wakeup, true);
	}
	else
	{
		/* In sync mode, process immediately */
		log_record *peek = rb_peek(g_logger.queue);
		if (peek)
		{
			process_record(peek);
			rb_consume(g_logger.queue);
		}
	}
	return true;
}
