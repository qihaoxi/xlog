/* =====================================================================================
 *       Filename:  test_multi_worker.c
 *    Description:  Multi-worker（多消费者）并发测试
 *                  验证 xlog 在“多个 worker 线程并发 drain 环形缓冲”场景下是否线程安全。
 *        Version:  1.0
 *        Created:  2026-09-04
 *       Compiler:  gcc/clang (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 *
 * 背景：ringbuf.h 自述为 MPSC（多生产者-单消费者），显式声明“Only ONE
 * consumer thread may call rb_peek/rb_consume or rb_pop”。外部测试反馈“多
 * worker 场景下不是线程安全的”，本用例把该场景结构化复现：
 *
 *   场景 A（基线对照）: 多线程走公开 API xlog_log()（生产侧 MP 路径）。
 *   场景 B（核心复现）: P 个生产者 + W 个 worker，worker 并发 rb_peek/rb_consume
 *                      或 rb_pop drain 同一个 ring_buffer（消费侧多消费者路径）。
 *
 * 运行方式（四种 sanitizer 各自编译运行）：
 *   -fsanitize=address
 *   -fsanitize=thread
 *   -fsanitize=memory       （clang）
 *   -fsanitize=undefined,bounds-strict （clang）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdint.h>

#include "platform.h"
#include "ringbuf.h"
#include "log_record.h"
#include "xlog.h"
#include "xlog_core.h"

/* ============================================================================
 * 场景 B：多生产 + 多消费者
 * ============================================================================ */

#define B_RING_SIZE           16384
#define B_PRODUCERS           4
#define B_WORKERS             4
#define B_MESSAGES_PER_PRODUCER 20000
#define B_MAX_EMPTY_SPINS     100000   /* 空转预算（配合 deadline 使用） */
#define B_DEFAULT_BUDGET_S    30       /* 场景B 墙钟预算：活锁时判定 FAILED 退出 */

static ring_buffer *b_ring = NULL;
static atomic_int b_total_pushed = ATOMIC_VAR_INIT(0);
static atomic_int b_total_processed = ATOMIC_VAR_INIT(0);
static atomic_int b_producers_done = ATOMIC_VAR_INIT(0);
static int b_expected_producers = 0;
static uint64_t b_deadline_ns = 0;   /* 场景B 超时预算（墙钟） */
static int b_verdict_fail = 0;       /* 场景B 任一 FAILED 分支置 1，main 用它作退出码 */

static xlog_barrier_t b_start_barrier;

/* 每个 worker 记录其处理过的 payload，join 后主线程做去重/合法性核对。
 * 竞态导致的“同一 slot 被多个 worker 重复处理”会直接表现为重复 payload；
 * 撕裂读则表现为非法的 payload 值。 */
#define B_MAX_MESSAGES (B_PRODUCERS * B_MESSAGES_PER_PRODUCER)
typedef struct b_worker_log
{
	uint64_t *payloads;
	size_t count;
	size_t cap;
} b_worker_log_t;

static int b_payload_valid(uint64_t p)
{
	uint32_t id = (uint32_t)(p / 1000000ULL);
	uint64_t seq = p % 1000000ULL;
	return id < (uint32_t) B_PRODUCERS && seq < (uint64_t) B_MESSAGES_PER_PRODUCER;
}

static void b_worker_log_add(b_worker_log_t *log, uint64_t p)
{
	if (log && log->count < log->cap)
	{
		log->payloads[log->count++] = p;
	}
	else if (log)
	{
		/* 超出预算：忽略，不影响结论（仍计入 processed） */
	}
}

static int b_worker_log_init(b_worker_log_t *log)
{
	log->cap = (size_t) B_MAX_MESSAGES + 1024;
	log->count = 0;
	log->payloads = malloc(log->cap * sizeof(uint64_t));
	return log->payloads ? 0 : -1;
}

/* 排序 + 去重统计：返回重复数与非法数 */
static int b_cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *) a, y = *(const uint64_t *) b;
	return (x > y) - (x < y);
}

static void b_verify_payloads(b_worker_log_t *logs, int n,
                              size_t *dup_out, size_t *invalid_out)
{
	size_t total = 0, i, j;
	for (i = 0; i < (size_t) n; i++)
	{
		total += logs[i].count;
	}

	uint64_t *all = malloc((total ? total : 1) * sizeof(uint64_t));
	if (!all)
	{
		*dup_out = 0;
		*invalid_out = 0;
		return;
	}
	size_t k = 0;
	for (i = 0; i < (size_t) n; i++)
	{
		for (j = 0; j < logs[i].count; j++)
		{
			all[k++] = logs[i].payloads[j];
		}
	}
	qsort(all, k, sizeof(uint64_t), b_cmp_u64);

	size_t dups = 0, invalid = 0;
	for (i = 0; i < k; i++)
	{
		if (!b_payload_valid(all[i]))
		{
			invalid++;
		}
		if (i > 0 && all[i] == all[i - 1])
		{
			dups++;
		}
	}
	*dup_out = dups;
	*invalid_out = invalid;
	free(all);
}

static int b_budget_left_ns(void)
{
	uint64_t now = xlog_get_timestamp_ns();
	if (b_deadline_ns != 0 && now >= b_deadline_ns)
	{
		return 0;
	}
	return 1;
}

/* Producer: push unique payload into arg_values[0] */
static void *b_producer_thread(void *arg)
{
	int id = *(int *)arg;
	int retry_count = 0;
	const int max_retries = 100000;

	xlog_barrier_wait(&b_start_barrier);

	for (int i = 0; i < B_MESSAGES_PER_PRODUCER; i++)
	{
		/* 预算耗尽：放弃剩余消息（用于在活锁下让测试可终止） */
		if (!b_budget_left_ns())
		{
			break;
		}
		log_record rec;
		log_record_init(&rec);
		rec.level = XLOG_LEVEL_INFO;
		rec.arg_count = 1;
		rec.arg_types[0] = (uint8_t) LOG_ARG_U64;
		/* 每个消息唯一的载荷：(producer * 大跨度 + seq)，用于事后核对重/丢 */
		rec.arg_values[0] = (uint64_t) id * 1000000ULL + (uint64_t) i;
		rec.thread_id = (uint32_t) id;

		retry_count = 0;
		while (!rb_push(b_ring, &rec))
		{
			retry_count++;
			if (retry_count > max_retries)
			{
				break;  /* 防止死循环（DROP 策略下几乎不会走到） */
			}
			XLOG_CPU_PAUSE();
		}
		if (retry_count <= max_retries)
		{
			atomic_fetch_add(&b_total_pushed, 1);
		}
	}

	atomic_fetch_add(&b_producers_done, 1);
	return NULL;
}

/* Worker：zero-copy 模式 rb_peek + rb_consume */
static void *b_worker_peek_consume(void *arg)
{
	b_worker_log_t *log = (b_worker_log_t *) arg;
	int empty_count = 0;

	xlog_barrier_wait(&b_start_barrier);

	while (1)
	{
		log_record *rec = rb_peek(b_ring);
		if (rec)
		{
			/* “处理”= 读走载荷并记录，供事后去重核对 */
			uint64_t p = rec->arg_values[0];
			(void) rec->level;
			rb_consume(b_ring);
			atomic_fetch_add(&b_total_processed, 1);
			b_worker_log_add(log, p);
			empty_count = 0;
		}
		else
		{
			empty_count++;
			if (!b_budget_left_ns())
			{
				break;  /* 活锁：预算耗尽，退出并报告 */
			}
			if (empty_count > B_MAX_EMPTY_SPINS)
			{
				if (atomic_load(&b_producers_done) >= b_expected_producers)
				{
					/* 收尾：尽力 drain 剩余 */
					while ((rec = rb_peek(b_ring)) != NULL)
					{
						uint64_t p2 = rec->arg_values[0];
						rb_consume(b_ring);
						atomic_fetch_add(&b_total_processed, 1);
						b_worker_log_add(log, p2);
					}
					break;
				}
				empty_count = 0;
			}
			XLOG_CPU_PAUSE();
		}
	}
	return NULL;
}

/* Worker：copy-out 模式 rb_pop */
static void *b_worker_pop(void *arg)
{
	b_worker_log_t *log = (b_worker_log_t *) arg;
	int empty_count = 0;

	xlog_barrier_wait(&b_start_barrier);

	while (1)
	{
		log_record out;
		char ibuf[LOG_INLINE_BUF_SIZE];
		log_record_init_with_buf(&out, ibuf, LOG_INLINE_BUF_SIZE);

		if (rb_pop(b_ring, &out))
		{
			uint64_t p = out.arg_values[0];
			(void) out.level;
			atomic_fetch_add(&b_total_processed, 1);
			b_worker_log_add(log, p);
			empty_count = 0;
		}
		else
		{
			empty_count++;
			if (!b_budget_left_ns())
			{
				break;  /* 活锁：预算耗尽，退出并报告 */
			}
			if (empty_count > B_MAX_EMPTY_SPINS)
			{
				if (atomic_load(&b_producers_done) >= b_expected_producers)
				{
					while (rb_pop(b_ring, &out))
					{
						uint64_t p2 = out.arg_values[0];
						atomic_fetch_add(&b_total_processed, 1);
						b_worker_log_add(log, p2);
					}
					break;
				}
				empty_count = 0;
			}
			XLOG_CPU_PAUSE();
		}
	}
	return NULL;
}

static void b_run_scenario(void)
{
	int budget_s = B_DEFAULT_BUDGET_S;
	const char *env = getenv("MW_BUDGET");
	if (env)
	{
		int v = atoi(env);
		if (v > 0 && v <= 3600)
		{
			budget_s = v;
		}
	}

	atomic_store(&b_total_pushed, 0);
	atomic_store(&b_total_processed, 0);
	atomic_store(&b_producers_done, 0);
	b_expected_producers = B_PRODUCERS;
	b_deadline_ns = xlog_get_timestamp_ns() + (uint64_t) budget_s * 1000000000ULL;

	b_ring = rb_create(B_RING_SIZE, RB_POLICY_DROP);
	if (!b_ring)
	{
		printf("  FAILED: create ring buffer\n");
		return;
	}

	if (xlog_barrier_init(&b_start_barrier, B_PRODUCERS + B_WORKERS) != 0)
	{
		printf("  FAILED: init barrier\n");
		rb_destroy(b_ring);
		return;
	}
	fprintf(stderr, "[B] ring created; spawning %d producers + %d workers\n",
	        B_PRODUCERS, B_WORKERS);

	xlog_thread_t *producers = malloc(B_PRODUCERS * sizeof(xlog_thread_t));
	xlog_thread_t *workers = malloc(B_WORKERS * sizeof(xlog_thread_t));
	int *ids = malloc(B_PRODUCERS * sizeof(int));
	b_worker_log_t *logs = calloc(B_WORKERS, sizeof(b_worker_log_t));
	if (!producers || !workers || !ids || !logs)
	{
		printf("  FAILED: alloc threads\n");
		free(producers); free(workers); free(ids); free(logs);
		rb_destroy(b_ring);
		return;
	}

	for (int i = 0; i < B_PRODUCERS; i++)
	{
		ids[i] = i;
		xlog_thread_create(&producers[i], b_producer_thread, &ids[i]);
	}
	for (int i = 0; i < B_WORKERS; i++)
	{
		if (b_worker_log_init(&logs[i]) != 0)
		{
			printf("  FAILED: init worker log\n");
			rb_destroy(b_ring);
			free(producers); free(workers); free(ids); free(logs);
			return;
		}
		if (i % 2 == 0)
			xlog_thread_create(&workers[i], b_worker_peek_consume, &logs[i]);
		else
			xlog_thread_create(&workers[i], b_worker_pop, &logs[i]);
	}

	for (int i = 0; i < B_PRODUCERS; i++)
		xlog_thread_join(producers[i], NULL);
	fprintf(stderr, "[B] all producers joined; waiting workers\n");
	for (int i = 0; i < B_WORKERS; i++)
		xlog_thread_join(workers[i], NULL);
	fprintf(stderr, "[B] all workers joined\n");

	int pushed = atomic_load(&b_total_pushed);
	int processed = atomic_load(&b_total_processed);
	int producers_done = atomic_load(&b_producers_done);
	int budget_exhausted = !b_budget_left_ns();
	int expected = B_PRODUCERS * B_MESSAGES_PER_PRODUCER;

	size_t dups = 0, invalid = 0;
	b_verify_payloads(logs, B_WORKERS, &dups, &invalid);

	printf("  场景B: %d producers x %d msgs(expected=%d) -> pushed=%d, W%d workers processed=%d, producers_done=%d/%d, 重复=%zu 非法=%zu\n",
	       B_PRODUCERS, B_MESSAGES_PER_PRODUCER, expected, pushed,
	       B_WORKERS, processed, producers_done, B_PRODUCERS,
	       dups, invalid);

	/* 证伪/证实：多消费者场景下管线是否能在预算内正常走完 */
	if (budget_exhausted && pushed < expected)
	{
		b_verdict_fail = 1;
		printf("  FAILED(LIVELOCK): 预算内仅 pushed=%d / expected=%d —— "
		       "多 worker 并发消费轮流把同一 slot 当本轮消费，read_idx 一次性前进 ≥2 步而 "
		       "write_idx 只进 1 步，且“先 peek 后 consume”使 rd 越过 wr 后 wr-rd 无符号下溢，"
		       "队列对生产者“永远满”对消费者“永远空”→ 生产者饿死/管线活锁。\n",
		       pushed, expected);
	}
	else if (budget_exhausted)
	{
		b_verdict_fail = 1;
		printf("  FAILED(TIMEOUT): 生产完成但 worker 未在预算内结束 drain\n");
	}
	else if (dups > 0 || invalid > 0)
	{
		b_verdict_fail = 1;
		printf("  FAILED: 载荷校验失败 —— 重复=%zu(同一记录被多个 worker 处理/rd 越界跳槽) 非法=%zu(撕裂读或读到未初始化槽)\n",
		       dups, invalid);
	}
	else if (processed > pushed)
	{
		b_verdict_fail = 1;
		printf("  FAILED: processed(%d) > pushed(%d)\n", processed, pushed);
	}
	else if (processed == pushed && pushed == expected)
	{
		printf("  PASSED（本轮未观察异常；是否竞态看 sanitizer 报告）\n");
	}
	else
	{
		printf("  processed(%d) < pushed(%d) —— 存在丢失（DROP 策略下可能正常，也可能是竞态丢失）；载荷判定为主\n",
		       processed, pushed);
	}

	xlog_barrier_destroy(&b_start_barrier);
	for (int i = 0; i < B_WORKERS; i++)
	{
		free(logs[i].payloads);
	}
	free(producers); free(workers); free(ids); free(logs);
	rb_destroy(b_ring);
	b_deadline_ns = 0;
}

/* ============================================================================
 * 场景 A（基线对照）：多线程公开 API xlog_log —— MP 生产路径
 * ============================================================================ */
#define A_THREADS     8
#define A_LOGS_PER    5000

static int a_failures = 0;

/* 只计数、不写盘/屏的 sink：避免 4 万行 console 输出拖慢 sanitizer 跑并淹没告警 */
static void a_sink_write(sink_t *sink, const char *data, size_t len)
{
	(void) sink; (void) data; (void) len;
}

static void a_sink_flush(sink_t *sink)
{
	(void) sink;
}

static void a_sink_close(sink_t *sink)
{
	(void) sink;
}

static void *a_producer_thread(void *arg)
{
	int id = *(int *)arg;
	xlog_barrier_wait(&b_start_barrier);
	for (int i = 0; i < A_LOGS_PER; i++)
	{
		LOG_INFO("worker=%d seq=%d payload=%d", id, i, i);
	}
	return NULL;
}

static void a_run(void)
{
	fprintf(stderr, "[A] start\n");
	printf("  场景A: %d threads x %d xlog_log() concurrently (async mode)\n",
	       A_THREADS, A_LOGS_PER);

	if (!xlog_init())
	{
		a_failures = 1;
		fprintf(stderr, "[A] xlog_init failed\n");
		return;
	}
	sink_t *csink = sink_create(NULL, a_sink_write, a_sink_flush, a_sink_close,
	                           XLOG_LEVEL_INFO, SINK_TYPE_CUSTOM);
	if (!csink || !xlog_add_sink(csink))
	{
		a_failures = 1;
		fprintf(stderr, "[A] add sink failed\n");
		return;
	}
	fprintf(stderr, "[A] inited\n");

	xlog_thread_t *threads = malloc(A_THREADS * sizeof(xlog_thread_t));
	int *ids = malloc(A_THREADS * sizeof(int));

	if (xlog_barrier_init(&b_start_barrier, A_THREADS) != 0)
	{
		printf("  FAILED: init barrier\n");
		a_failures = 1;
		free(threads); free(ids);
		return;
	}
	fprintf(stderr, "[A] spawning %d producers\n", A_THREADS);
	for (int i = 0; i < A_THREADS; i++)
	{
		ids[i] = i;
		xlog_thread_create(&threads[i], a_producer_thread, &ids[i]);
	}
	for (int i = 0; i < A_THREADS; i++)
		xlog_thread_join(threads[i], NULL);
	fprintf(stderr, "[A] all producers joined\n");

	xlog_flush();
	fprintf(stderr, "[A] flushed\n");
	xlog_shutdown();
	fprintf(stderr, "[A] shutdown done\n");
	xlog_barrier_destroy(&b_start_barrier);
	free(threads); free(ids);

	printf("  场景A 完成（多线程生产路径）；是否竞态看 sanitizer 报告\n");
}

int main(void)
{
	printf("=========================================\n");
	printf("  Multi-Worker 并发测试\n");
	printf("=========================================\n\n");

	printf("[场景A] 公开 API 多线程生产（基线对照）\n");
	a_run();

	printf("\n[场景B] 多 worker（多消费者）并发 drain 环形缓冲\n");
	b_run_scenario();

	printf("\n=========================================\n");
	printf("  完成（返回值：0=全部通过，非0=场景B失败/未完成）\n");
	printf("=========================================\n");
	return a_failures || b_verdict_fail;
}
