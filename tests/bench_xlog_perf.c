/* =====================================================================================
 *       Filename:  bench_xlog_perf.c
 *    Description:  Micro-benchmark for xlog formatter and core logging path
 *        Version:  1.0
 *        Created:  2026-04-13
 *       Compiler:  gcc/clang/msvc (C11)
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "platform.h"
#include "xlog_core.h"
#include "sink.h"
#include "log_record.h"

#ifndef XLOG_BENCH_DEFAULT_ITERS
#define XLOG_BENCH_DEFAULT_ITERS 1000000UL
#endif

static volatile uint64_t g_sink_bytes = 0;
static volatile uint64_t g_sink_calls = 0;
static volatile uint64_t g_checksum = 0;

static uint64_t bench_now_ns(void)
{
	return xlog_get_timestamp_ns();
}

static void noop_sink_write(struct sink_t *sink, const char *data, size_t len)
{
	(void)sink;
	if (data && len > 0)
	{
		g_sink_bytes += (uint64_t)len;
		g_sink_calls++;
		g_checksum += (unsigned char)data[0];
	}
}

static void noop_sink_flush(struct sink_t *sink)
{
	(void)sink;
}

static void noop_sink_close(struct sink_t *sink)
{
	(void)sink;
}

static sink_t *create_noop_sink(void)
{
	return sink_create(NULL, noop_sink_write, noop_sink_flush, noop_sink_close,
	                   XLOG_LEVEL_TRACE, SINK_TYPE_FILE);
}

static void create_bench_record(log_record *rec)
{
	log_record_init(rec);
	rec->level = XLOG_LEVEL_INFO;
	rec->thread_id = (uint32_t)xlog_get_tid();
	rec->timestamp_ns = bench_now_ns();
	rec->fmt = "user=%s id=%d latency=%llu";
	rec->loc.file = "bench_xlog_perf.c";
	rec->loc.func = "bench";
	rec->loc.line = 88;
	rec->arg_types[0] = LOG_ARG_STR_STATIC;
	rec->arg_values[0] = (uint64_t)(uintptr_t)"alice";
	rec->arg_types[1] = LOG_ARG_I32;
	rec->arg_values[1] = (uint64_t)(int64_t)42;
	rec->arg_types[2] = LOG_ARG_U64;
	rec->arg_values[2] = (uint64_t)123456789ULL;
	rec->arg_count = 3;
	atomic_store(&rec->ready, true);
}

static void bench_formatter(size_t iterations)
{
	log_record rec;
	char buffer[1024];
	create_bench_record(&rec);

	printf("\n=== Formatter benchmark (%zu iterations) ===\n", iterations);

	uint64_t start = bench_now_ns();
	for (size_t i = 0; i < iterations; ++i)
	{
		int len = log_record_format(&rec, buffer, sizeof(buffer));
		g_checksum += (uint64_t)len;
	}
	uint64_t generic_ns = bench_now_ns() - start;

	start = bench_now_ns();
	for (size_t i = 0; i < iterations; ++i)
	{
		int len = log_record_format_default_inline(&rec, buffer, sizeof(buffer));
		g_checksum += (uint64_t)len;
	}
	uint64_t inline_ns = bench_now_ns() - start;

	printf("  log_record_format:                %8.2f ns/call\n",
	       (double)generic_ns / (double)iterations);
	printf("  log_record_format_default_inline: %8.2f ns/call\n",
	       (double)inline_ns / (double)iterations);
	if (inline_ns > 0)
	{
		printf("  Inline speedup: %.2fx\n", (double)generic_ns / (double)inline_ns);
	}
}

static void bench_init_path(size_t iterations)
{
	xlog_config cfg = {
		.queue_capacity = 8192,
		.format_buffer_size = 4096,
		.min_level = XLOG_LEVEL_DEBUG,
		.async = true,
		.queue_full_policy = RB_POLICY_DROP,
		.queue_spin_timeout_ns = 0,
		.queue_block_timeout_ns = 0,
		.auto_flush = true,
		.batch_size = 64,
		.flush_interval_ms = 1000,
		.format_style = XLOG_OUTPUT_DEFAULT
	};
	size_t init_iters = iterations < 200 ? iterations : 200;

	printf("\n=== Init benchmark (%zu iterations) ===\n", init_iters);

	uint64_t start = bench_now_ns();
	for (size_t i = 0; i < init_iters; ++i)
	{
		if (!xlog_init_with_config(&cfg))
		{
			printf("  FAILED: xlog_init_with_config during init benchmark\n");
			return;
		}
		xlog_shutdown();
	}
	uint64_t elapsed = bench_now_ns() - start;
	printf("  xlog_init_with_config + shutdown: %8.2f us/cycle\n",
	       (double) elapsed / (double) init_iters / 1000.0);
}

static void bench_xlog_mode(size_t iterations, bool async_mode, bool with_args,
	                        rb_full_policy queue_policy, uint64_t spin_timeout_ns)
{
	xlog_config cfg = {
		.queue_capacity = 1u << 16,
		.format_buffer_size = 4096,
		.min_level = XLOG_LEVEL_DEBUG,
		.async = async_mode,
		.queue_full_policy = queue_policy,
		.queue_spin_timeout_ns = spin_timeout_ns,
		.queue_block_timeout_ns = 1000000ULL,
		.auto_flush = false,
		.batch_size = 1024,
		.flush_interval_ms = 1000,
		.format_style = XLOG_OUTPUT_DEFAULT
	};

	if (!xlog_init_with_config(&cfg))
	{
		printf("  FAILED: xlog_init_with_config(async=%d)\n", async_mode ? 1 : 0);
		return;
	}

	xlog_set_console_colors(false);
	xlog_set_has_file_sink(false);

	sink_t *sink = create_noop_sink();
	if (!sink || !xlog_add_sink(sink))
	{
		printf("  FAILED: create/add noop sink\n");
		if (sink)
		{
			sink_destroy(sink);
		}
		xlog_shutdown();
		return;
	}

	uint64_t start = bench_now_ns();
	if (with_args)
	{
		for (size_t i = 0; i < iterations; ++i)
		{
			xlog_log(XLOG_LEVEL_INFO, __FILE__, __LINE__, __func__,
			         "user=%s id=%d latency=%llu",
			         "alice", 42, (unsigned long long)(123456789ULL + i));
		}
	}
	else
	{
		for (size_t i = 0; i < iterations; ++i)
		{
			xlog_log(XLOG_LEVEL_INFO, __FILE__, __LINE__, __func__,
			         "plain literal benchmark message");
		}
	}
	if (async_mode)
	{
		xlog_flush();
	}
	uint64_t elapsed = bench_now_ns() - start;

	xlog_stats stats;
	xlog_get_stats(&stats);

	printf("  xlog %-5s %-7s %-5s: %8.2f ns/call, %.2f M msg/s, processed=%" PRIu64 ", dropped=%" PRIu64 "\n",
	       async_mode ? "async" : "sync",
	       with_args ? "args" : "literal",
	       queue_policy == RB_POLICY_SPIN ? "spin" : "drop",
	       (double)elapsed / (double)iterations,
	       (double)iterations * 1000.0 / (double)elapsed,
	       stats.processed,
	       stats.dropped);

	xlog_shutdown();
}

int main(int argc, char **argv)
{
	size_t iterations = XLOG_BENCH_DEFAULT_ITERS;
	if (argc > 1)
	{
		char *end = NULL;
		unsigned long long parsed = strtoull(argv[1], &end, 10);
		if (end && *end == '\0' && parsed > 0)
		{
			iterations = (size_t)parsed;
		}
	}

	printf("============================================\n");
	printf("  xlog Performance Benchmark\n");
	printf("============================================\n");
	printf("Iterations: %zu\n", iterations);

	bench_init_path(iterations);
	bench_formatter(iterations);

	printf("\n=== xlog benchmark (%zu iterations) ===\n", iterations);
	bench_xlog_mode(iterations, false, false, RB_POLICY_SPIN, 0);
	bench_xlog_mode(iterations, false, true, RB_POLICY_SPIN, 0);
	bench_xlog_mode(iterations, true, false, RB_POLICY_DROP, 0);
	bench_xlog_mode(iterations, true, true, RB_POLICY_DROP, 0);
	bench_xlog_mode(iterations, true, false, RB_POLICY_SPIN, 50000ULL);
	bench_xlog_mode(iterations, true, true, RB_POLICY_SPIN, 50000ULL);

	printf("\nchecksum=%" PRIu64 ", sink_bytes=%" PRIu64 ", sink_calls=%" PRIu64 "\n",
	       g_checksum, g_sink_bytes, g_sink_calls);
	return 0;
}

