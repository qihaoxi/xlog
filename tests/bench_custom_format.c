/* bench_custom_format.c - overhead measurement for XLOG_OUTPUT_CUSTOM
 *
 * Answers: does routing the backend through a user format function slow
 * logging down, and by how much? Three configurations, same workload:
 *
 *   default      : built-in XLOG_OUTPUT_DEFAULT (static inline formatter)
 *   passthrough  : XLOG_OUTPUT_CUSTOM whose fn just calls the built-in
 *                  default inline formatter -> isolates the pure
 *                  function-pointer indirection cost
 *   cds-style    : XLOG_OUTPUT_CUSTOM with a compound_logger-style renderer
 *                  (TLS-cached timestamp + snprintf segments + raw message)
 *                  -> a realistic embedder formatter
 *
 * Output: producer-side ns/log and end-to-end logs/s per configuration.
 * Sink writes go to /dev/null to keep disk noise out of the measurement.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "xlog_core.h"
#include "xlog_builder.h"
#include "log_record.h"
#include "formatter.h"
#include "platform.h"

#if defined(_WIN32)
#define BENCH_DIR "."
#elif defined(__linux__)
#define BENCH_DIR "/dev/shm"
#else /* macOS/BSD: no /dev/shm, fall back to cwd */
#define BENCH_DIR "."
#endif

#ifndef _WIN32
static double now_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}
#else
static double now_sec(void)
{
	return (double) clock() / CLOCKS_PER_SEC;
}
#endif

/* ---- passthrough: built-in work behind a function pointer ---- */

static size_t passthrough_fn(const log_record *rec, void *ctx,
                             char *buf, size_t size)
{
	(void) ctx;
	return (size_t) log_record_format_default_inline(rec, buf, size);
}

/* ---- compound_logger-style renderer (realistic embedder formatter) ----
 * Layout: MM-DD HH:MM:SS.uuuuuu|LEVEL|module:tag|file:line[|trc:.. s:.. p:..]|msg\n
 */

static size_t cds_fn(const log_record *rec, void *ctx, char *buf, size_t size)
{
	static _Thread_local char sec_str[16];
	static _Thread_local time_t cached_sec;
	char msg[1024];
	char seg[80];
	const char *level;
	const char *base;
	const char *slash;
	const char *bslash;
	time_t sec;
	uint64_t us;
	size_t n;

	(void) ctx;

	sec = (time_t) (rec->timestamp_ns / 1000000000ULL);
	us = (rec->timestamp_ns / 1000ULL) % 1000000ULL;
	if (sec != cached_sec)
	{
		struct tm tmv;
		xlog_get_localtime(sec, &tmv);
		snprintf(sec_str, sizeof(sec_str), "%02d-%02d %02d:%02d:%02d",
		         tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
		cached_sec = sec;
	}

	switch ((xlog_level) rec->level)
	{
		case XLOG_LEVEL_TRACE: level = "TRACE"; break;
		case XLOG_LEVEL_DEBUG: level = "DEBUG"; break;
		case XLOG_LEVEL_INFO: level = "INFO"; break;
		case XLOG_LEVEL_WARNING: level = "WARN"; break;
		case XLOG_LEVEL_ERROR: level = "ERROR"; break;
		default: level = "ERROR"; break;
	}

	base = rec->loc.file ? rec->loc.file : "-";
	slash = strrchr(base, '/');
	bslash = strrchr(base, '\\');
	if (slash || bslash)
	{
		base = (bslash && (!slash || bslash > slash)) ? bslash + 1 : slash + 1;
	}

	seg[0] = '\0';
	if (rec->ctx.flags & (LOG_CTX_HAS_TRACE_ID | LOG_CTX_HAS_TRACE_ID_HI))
	{
		snprintf(seg, sizeof(seg), "|trc:%016llx%016llx",
		         (unsigned long long) rec->ctx.trace_id_hi,
		         (unsigned long long) rec->ctx.trace_id);
	}
	if (rec->ctx.flags & LOG_CTX_HAS_SPAN_ID)
	{
		size_t off = strlen(seg);
		snprintf(seg + off, sizeof(seg) - off, "%ss:%016llx",
		         off ? " " : "|",
		         (unsigned long long) rec->ctx.span_id);
	}

	{
		int mlen = log_record_format_raw_inline(rec, msg, sizeof(msg));
		if (mlen < 0)
		{
			msg[0] = '\0';
		}
	}

	n = (size_t) snprintf(buf, size,
	                      "%s.%06llu|%s|%s:%s|%s:%u%s|%s\n",
	                      sec_str, (unsigned long long) us,
	                      level,
	                      rec->ctx.module ? rec->ctx.module : "",
	                      rec->ctx.tag ? rec->ctx.tag : "",
	                      base, rec->loc.line, seg, msg);
	return n;
}

/* ---- harness ---- */

/* Phase 1: DROP policy, loop kept below queue capacity -> no blocking, no
 * drops: measures the pure producer-side enqueue cost. Formatting style is
 * irrelevant on this path (rendering happens on the backend), so all three
 * configs should land on the same number - that is the point. */
static double producer_only_ns(const char *label, xlog_custom_format_fn fn)
{
	xlog_builder *cfg;
	double t0, t1, result;
	xlog_stats st;
	const int N1 = 60000; /* < 65536 queue capacity */

	cfg = xlog_builder_new();
	xlog_builder_set_level(cfg, XLOG_LEVEL_INFO);
	xlog_builder_set_mode(cfg, XLOG_MODE_ASYNC);
	xlog_builder_set_buffer_size(cfg, 65536);
	xlog_builder_set_queue_policy(cfg, XLOG_QUEUE_DROP);
	if (fn)
	{
		xlog_builder_set_custom_format(cfg, fn, NULL);
	}
	xlog_builder_enable_console(cfg, false);
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_directory(cfg, BENCH_DIR);
	xlog_builder_file_name(cfg, "xlog_bench");
	xlog_builder_file_rotate_on_start(cfg, false);
	if (!xlog_builder_apply(cfg) || xlog_sink_count() == 0)
	{
		fprintf(stderr, "apply/sink failed for %s (phase 1)\n", label);
		xlog_builder_free(cfg);
		return -1;
	}
	xlog_builder_free(cfg);

	xlog_trace_set(0x1122334455667788ULL, 0x99aabbccddeeff00ULL,
	               0x1234567890abcdefULL, 0);

	t0 = now_sec();
	for (int i = 0; i < N1; i++)
	{
		XLOG_INFO("req id=%d name=proc-%d size=%u", i, i & 0xff, 4096u);
	}
	t1 = now_sec();
	result = (t1 - t0) * 1e9 / N1;

	xlog_flush();
	xlog_get_stats(&st);
	if (st.dropped != 0)
	{
		fprintf(stderr, "note: %s phase1 dropped=%llu (timing tainted)\n",
		        label, (unsigned long long) st.dropped);
	}
	xlog_shutdown();
	return result;
}

/* Phase 2: BLOCK policy - producers wait for the backend instead of
 * dropping, so the end-to-end number is the true pipeline throughput. */
static void run_config(const char *label, xlog_custom_format_fn fn)
{
	xlog_builder *cfg;
	double t0, t1, t_produce, t_total, p_ns;
	xlog_stats st;
	const int N = 200000;

	remove(BENCH_DIR "/xlog_bench.log");
	cfg = xlog_builder_new();
	xlog_builder_set_level(cfg, XLOG_LEVEL_INFO);
	xlog_builder_set_mode(cfg, XLOG_MODE_ASYNC);
	xlog_builder_set_buffer_size(cfg, 65536);
	xlog_builder_set_queue_policy(cfg, XLOG_QUEUE_BLOCK);
	if (fn)
	{
		xlog_builder_set_custom_format(cfg, fn, NULL);
	}
	xlog_builder_enable_console(cfg, false);
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_directory(cfg, BENCH_DIR);
	xlog_builder_file_name(cfg, "xlog_bench");
	xlog_builder_file_rotate_on_start(cfg, false);
	if (!xlog_builder_apply(cfg) || xlog_sink_count() == 0)
	{
		fprintf(stderr, "apply/sink failed for %s\n", label);
		xlog_builder_free(cfg);
		return;
	}
	xlog_builder_free(cfg);

	/* Trace context on so the custom renderer exercises its full path. */
	xlog_trace_set(0x1122334455667788ULL, 0x99aabbccddeeff00ULL,
	               0x1234567890abcdefULL, 0);

	t0 = now_sec();
	for (int i = 0; i < N; i++)
	{
		XLOG_INFO("req id=%d name=proc-%d size=%u", i, i & 0xff, 4096u);
	}
	t1 = now_sec();
	t_produce = t1 - t0;

	xlog_flush();
	t_total = now_sec() - t0;

	xlog_get_stats(&st);
	xlog_shutdown();

	p_ns = producer_only_ns(label, fn);
	printf("%-14s producer %8.1f ns/log (enqueue-only %6.1f ns)"
	       "   end-to-end %9.0f logs/s   (processed=%llu dropped=%llu)\n",
	       label, t_produce * 1e9 / N, p_ns, (double) N / t_total,
	       (unsigned long long) st.processed,
	       (unsigned long long) st.dropped);
}

int main(void)
{
	printf("bench_custom_format: %d logs per config, BLOCK policy,"
	       " file sink -> /dev/shm (tmpfs)\n\n", 200000);

	run_config("default", NULL);
	run_config("default", NULL);
	run_config("passthrough", passthrough_fn);
	run_config("passthrough", passthrough_fn);
	run_config("cds-style", cds_fn);
	run_config("cds-style", cds_fn);

	return 0;
}
