/* =====================================================================================
 *       Filename:  test_backend_flush.c
 *    Description:  Backend idle-path regression tests
 *
 *  Guards the 2026-11-10 backend-loop fixes (139/xen CPU burn):
 *   1. The periodic partial-batch flush must still happen while the backend is
 *      idle. The flush check used to live inside the spin loop; it now runs
 *      once per condvar wait cycle. If that ever regresses (check removed, or
 *      moved somewhere unreachable), a partial batch would never be flushed
 *      without an explicit xlog_flush().
 *   2. A sustained burst must still be fully drained with the shorter spin
 *      phase (BACKEND_SPIN_COUNT 800 -> 64): no drops, no stuck records.
 * ===================================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
#include <pthread.h>
#include "xlog.h"
#include "xlog_core.h"
#include "sink.h"

typedef struct
{
	atomic_ullong bytes;
	atomic_ullong flushes;
	atomic_ullong writes;
} capture_counters;

static void cap_write(sink_t *sink, const char *data, size_t len)
{
	capture_counters *c = (capture_counters *) sink->ctx;
	(void) data;
	atomic_fetch_add(&c->bytes, (unsigned long long) len);
	atomic_fetch_add(&c->writes, 1);
}

static void cap_flush(sink_t *sink)
{
	capture_counters *c = (capture_counters *) sink->ctx;
	atomic_fetch_add(&c->flushes, 1);
}

/* 1. Partial batch (5 records, batch_size=1000) must be flushed by the idle
 *    periodic-flush path, without any explicit xlog_flush() call. */
static int test_idle_partial_flush(void)
{
	printf("\n=== Test 1: idle periodic flush of a partial batch ===\n");

	xlog_config config = {
		.queue_capacity = 1024,
		.format_buffer_size = 4096,
		.min_level = XLOG_LEVEL_DEBUG,
		.async = true,
		.auto_flush = true,
		.batch_size = 1000,          /* never reached: only the idle path can flush */
		.flush_interval_ms = 200
	};
	if (!xlog_init_with_config(&config))
	{
		printf("FAILED: xlog_init_with_config()\n");
		return 1;
	}

	capture_counters cap = {0};
	sink_t *sink = sink_create(&cap, cap_write, cap_flush, NULL,
	                           XLOG_LEVEL_DEBUG, SINK_TYPE_CUSTOM);
	if (!sink || !xlog_add_sink(sink))
	{
		printf("FAILED: add sink\n");
		xlog_shutdown();
		return 1;
	}

	for (int i = 0; i < 5; i++)
	{
		XLOG_INFO("partial batch record %d", i);
	}

	/* Wait up to 3s (15x the flush interval) for the idle flush. */
	int waited_ms = 0;
	while (atomic_load(&cap.flushes) == 0 && waited_ms < 3000)
	{
		usleep(10 * 1000);
		waited_ms += 10;
	}

	xlog_stats st;
	xlog_get_stats(&st);
	unsigned long long flushes = atomic_load(&cap.flushes);
	unsigned long long bytes = atomic_load(&cap.bytes);

	printf("waited=%dms flushes=%llu bytes=%llu processed=%lu logged=%lu\n",
	       waited_ms, flushes, bytes, st.processed, st.logged);

	int failed = 0;
	if (st.logged != 5 || st.processed != 5)
	{
		printf("FAILED: expected logged=processed=5, got %lu/%lu\n", st.logged, st.processed);
		failed = 1;
	}
	if (bytes == 0)
	{
		printf("FAILED: sink received no data\n");
		failed = 1;
	}
	if (flushes == 0)
	{
		printf("FAILED: partial batch was never flushed while idle\n");
		failed = 1;
	}
	if (waited_ms >= 3000)
	{
		printf("FAILED: idle flush did not happen within 3s\n");
		failed = 1;
	}

	xlog_shutdown();
	if (!failed) printf("OK\n");
	return failed;
}

/* 2. Sustained burst: after shrinking the spin phase, records must still be
 *    fully drained (no drops, no lost tail). */
static void *burst_producer(void *arg)
{
	(void) arg;
	for (int i = 0; i < 5000; i++)
	{
		XLOG_INFO("burst record %d", i);
	}
	return NULL;
}

static int test_burst_drain(void)
{
	printf("\n=== Test 2: sustained burst drain (short spin) ===\n");

	xlog_config config = {
		.queue_capacity = 32768,        /* > 4*5000 so DROP policy never engages */
		.format_buffer_size = 4096,
		.min_level = XLOG_LEVEL_DEBUG,
		.async = true,
		.auto_flush = true,
		.batch_size = 64,
		.flush_interval_ms = 1000
	};
	if (!xlog_init_with_config(&config))
	{
		printf("FAILED: xlog_init_with_config()\n");
		return 1;
	}

	capture_counters cap = {0};
	sink_t *sink = sink_create(&cap, cap_write, cap_flush, NULL,
	                           XLOG_LEVEL_DEBUG, SINK_TYPE_CUSTOM);
	if (!sink || !xlog_add_sink(sink))
	{
		printf("FAILED: add sink\n");
		xlog_shutdown();
		return 1;
	}

	enum { NPROD = 4 };
	pthread_t th[NPROD];
	for (int i = 0; i < NPROD; i++)
	{
		if (pthread_create(&th[i], NULL, burst_producer, NULL) != 0)
		{
			printf("FAILED: pthread_create\n");
			xlog_shutdown();
			return 1;
		}
	}
	for (int i = 0; i < NPROD; i++)
	{
		pthread_join(th[i], NULL);
	}

	/* Explicit flush is the correctness barrier here. */
	xlog_flush();

	xlog_stats st;
	xlog_get_stats(&st);
	printf("logged=%lu processed=%lu dropped=%lu writes=%llu\n",
	       st.logged, st.processed, st.dropped,
	       atomic_load(&cap.writes));

	int failed = 0;
	if (st.logged != NPROD * 5000)
	{
		printf("FAILED: expected logged=%d, got %lu\n", NPROD * 5000, st.logged);
		failed = 1;
	}
	if (st.processed != st.logged)
	{
		printf("FAILED: processed (%lu) != logged (%lu)\n", st.processed, st.logged);
		failed = 1;
	}
	if (st.dropped != 0)
	{
		printf("FAILED: queue dropped %lu records under a 32768-slot queue\n", st.dropped);
		failed = 1;
	}

	xlog_shutdown();
	if (!failed) printf("OK\n");
	return failed;
}

/* 3. Producer->backend wakeup handshake: a slow trickle must never lose a
 *    wakeup. The backend parks between records; if a signal is dropped in the
 *    window between its wakeup check and the futex wait, the record waits for
 *    the flush timeout, the small queue fills and records are dropped. */
static void *trickle_producer(void *arg)
{
	(void) arg;
	for (int i = 0; i < 3000; i++)
	{
		XLOG_INFO("trickle %d", i);
		usleep(1000);
	}
	return NULL;
}

static int test_wakeup_handshake(void)
{
	printf("\n=== Test 3: producer->backend wakeup handshake ===\n");

	xlog_config config = {
		.queue_capacity = 64,          /* overflow within ~64ms if a wakeup is lost */
		.format_buffer_size = 4096,
		.min_level = XLOG_LEVEL_DEBUG,
		.async = true,
		.auto_flush = true,
		.batch_size = 64,
		.flush_interval_ms = 1000
	};
	if (!xlog_init_with_config(&config))
	{
		printf("FAILED: xlog_init_with_config()\n");
		return 1;
	}

	capture_counters cap = {0};
	sink_t *sink = sink_create(&cap, cap_write, cap_flush, NULL,
	                           XLOG_LEVEL_DEBUG, SINK_TYPE_CUSTOM);
	if (!sink || !xlog_add_sink(sink))
	{
		printf("FAILED: add sink\n");
		xlog_shutdown();
		return 1;
	}

	pthread_t th;
	if (pthread_create(&th, NULL, trickle_producer, NULL) != 0)
	{
		printf("FAILED: pthread_create\n");
		xlog_shutdown();
		return 1;
	}
	pthread_join(th, NULL);
	xlog_flush();

	xlog_stats st;
	xlog_get_stats(&st);
	printf("logged=%lu processed=%lu dropped=%lu\n", st.logged, st.processed, st.dropped);

	int failed = 0;
	if (st.logged != 3000 || st.processed != 3000)
	{
		printf("FAILED: expected logged=processed=3000, got %lu/%lu\n", st.logged, st.processed);
		failed = 1;
	}
	if (st.dropped != 0)
	{
		printf("FAILED: dropped %lu records -> a wakeup was lost\n", st.dropped);
		failed = 1;
	}

	xlog_shutdown();
	if (!failed) printf("OK\n");
	return failed;
}

int main(void)
{
	int failed = 0;
	failed |= test_idle_partial_flush();
	failed |= test_burst_drain();
	failed |= test_wakeup_handshake();
	printf("\n%s\n", failed ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
	return failed ? 1 : 0;
}
