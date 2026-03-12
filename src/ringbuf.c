/* =====================================================================================
 *       Filename:  ringbuf.c
 *    Description:  Lock-free ring buffer for log_record implementation
 *        Version:  2.0
 *        Created:  2026-02-10
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#include "ringbuf.h"
#include "log_record.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Use platform abstraction for CPU relax */
#define CPU_RELAX() XLOG_CPU_PAUSE()

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static inline bool is_power_of_two(size_t x)
{
	return x && ((x & (x - 1)) == 0);
}

static inline uint64_t rb_now_ns(void)
{
	return xlog_get_timestamp_ns();
}

/* ============================================================================
 * Initialization / Destruction
 * ============================================================================ */

bool rb_init(ring_buffer *rb, size_t capacity, rb_full_policy policy,
             uint64_t spin_timeout_ns, uint64_t block_timeout_ns)
{
	if (!rb || !is_power_of_two(capacity))
	{
		return false;
	}

	memset(rb, 0, sizeof(ring_buffer));

	rb->capacity = capacity;
	rb->mask = capacity - 1;
	rb->policy = policy;
	rb->spin_timeout_ns = spin_timeout_ns ? spin_timeout_ns : RB_SPIN_TIMEOUT_NS_DEFAULT;
	rb->block_timeout_ns = block_timeout_ns ? block_timeout_ns : RB_BLOCK_TIMEOUT_NS_DEFAULT;

	/* Allocate cache-line aligned buffer */
	size_t bytes = capacity * sizeof(log_record);
	void *mem = NULL;
	if (xlog_aligned_alloc(&mem, CACHE_LINE_SIZE, bytes) != 0)
	{
		return false;
	}
	memset(mem, 0, bytes);
	rb->buffer = (log_record *) mem;

	/* Initialize each log_record */
	for (size_t i = 0; i < capacity; i++)
	{
		log_record_init(&rb->buffer[i]);
	}

	atomic_init(&rb->read_idx, 0);
	atomic_init(&rb->write_idx, 0);

	/* Initialize sync primitives for BLOCK policy */
	rb->sync_inited = (xlog_mutex_init(&rb->cv_mutex) == 0) &&
	                  (xlog_cond_init(&rb->cv) == 0);

	rb_reset_stats(rb);
	return true;
}

bool rb_init_with_config(ring_buffer *rb, const rb_config *cfg)
{
	if (!cfg)
	{
		return false;
	}
	return rb_init(rb, cfg->capacity, cfg->policy,
	               cfg->spin_timeout_ns, cfg->block_timeout_ns);
}

ring_buffer *rb_create(size_t capacity, rb_full_policy policy)
{
	ring_buffer *rb = (ring_buffer *) malloc(sizeof(ring_buffer));
	if (!rb)
	{
		return NULL;
	}
	if (!rb_init(rb, capacity, policy, 0, 0))
	{
		free(rb);
		return NULL;
	}
	return rb;
}

ring_buffer *rb_create_with_config(const rb_config *cfg)
{
	if (!cfg)
	{
		return NULL;
	}
	ring_buffer *rb = (ring_buffer *) malloc(sizeof(ring_buffer));
	if (!rb)
	{
		return NULL;
	}
	if (!rb_init_with_config(rb, cfg))
	{
		free(rb);
		return NULL;
	}
	return rb;
}

void rb_free(ring_buffer *rb)
{
	if (!rb)
	{
		return;
	}

	if (rb->buffer)
	{
		xlog_aligned_free(rb->buffer);
		rb->buffer = NULL;
	}
	if (rb->sync_inited)
	{
		xlog_mutex_destroy(&rb->cv_mutex);
		xlog_cond_destroy(&rb->cv);
		rb->sync_inited = false;
	}
	atomic_store(&rb->read_idx, 0);
	atomic_store(&rb->write_idx, 0);
	rb->capacity = 0;
	rb->mask = 0;
}

void rb_destroy(ring_buffer *rb)
{
	if (rb)
	{
		rb_free(rb);
		free(rb);
	}
}

/* ============================================================================
 * Producer Operations
 * ============================================================================ */

log_record *rb_reserve(ring_buffer *rb)
{
	if (!rb || !rb->buffer)
	{
		return NULL;
	}

	uint64_t spin_deadline = 0;
	uint64_t block_deadline = 0;

	for (;;)
	{
		size_t wr = atomic_load_explicit(&rb->write_idx, memory_order_relaxed);
		size_t rd = atomic_load_explicit(&rb->read_idx, memory_order_acquire);

		/* Check if queue is full */
		if (wr - rd >= rb->capacity)
		{
			switch (rb->policy)
			{
				case RB_POLICY_DROP:
					atomic_fetch_add_explicit(&rb->stats.dropped, 1, memory_order_relaxed);
					return NULL;

				case RB_POLICY_DROP_OLDEST:
				{
					/* Try to advance read index to make room */
					size_t drop_idx = rd;
					if (atomic_compare_exchange_weak_explicit(&rb->read_idx, &drop_idx, drop_idx + 1,
					                                          memory_order_acq_rel, memory_order_relaxed))
					{
						log_record *drop_slot = &rb->buffer[drop_idx & rb->mask];
						log_record_reset(drop_slot);
						atomic_fetch_add_explicit(&rb->stats.dropped, 1, memory_order_relaxed);
					}
					CPU_RELAX();
					continue;
				}

				case RB_POLICY_SPIN:
					if (!spin_deadline)
					{
						spin_deadline = rb_now_ns() + rb->spin_timeout_ns;
					}
					if (rb_now_ns() >= spin_deadline)
					{
						atomic_fetch_add_explicit(&rb->stats.dropped, 1, memory_order_relaxed);
						return NULL;
					}
					CPU_RELAX();
					continue;

				case RB_POLICY_BLOCK:
					if (!rb->sync_inited)
					{
						atomic_fetch_add_explicit(&rb->stats.dropped, 1, memory_order_relaxed);
						return NULL;
					}
					if (!block_deadline)
					{
						block_deadline = rb_now_ns() + rb->block_timeout_ns;
					}

					xlog_mutex_lock(&rb->cv_mutex);
					while (atomic_load_explicit(&rb->write_idx, memory_order_relaxed) -
					       atomic_load_explicit(&rb->read_idx, memory_order_acquire) >= rb->capacity)
					{
						if (rb_now_ns() >= block_deadline)
						{
							xlog_mutex_unlock(&rb->cv_mutex);
							atomic_fetch_add_explicit(&rb->stats.dropped, 1, memory_order_relaxed);
							return NULL;
						}
						/* Use simple wait - platform handles timeout internally */
						xlog_cond_wait(&rb->cv, &rb->cv_mutex);
					}
					xlog_mutex_unlock(&rb->cv_mutex);
					continue;

				default:
					CPU_RELAX();
					continue;
			}
		}

		/* Try to reserve a slot */
		if (atomic_compare_exchange_weak_explicit(&rb->write_idx, &wr, wr + 1,
		                                          memory_order_acq_rel, memory_order_relaxed))
		{
			size_t slot_idx = wr & rb->mask;
			log_record *rec = &rb->buffer[slot_idx];

			/* Wait for slot to be available (in case consumer hasn't finished) */
			int spin_count = 0;
			while (atomic_load_explicit(&rec->ready, memory_order_acquire) && spin_count < 1000)
			{
				spin_count++;
				CPU_RELAX();
			}

			/* Reset the record for reuse */
			log_record_reset(rec);
			return rec;
		}

		CPU_RELAX();
	}
}

void rb_commit(ring_buffer *rb, log_record *rec)
{
	if (!rb || !rec)
	{
		return;
	}
	log_record_commit(rec);
	atomic_fetch_add_explicit(&rb->stats.pushed, 1, memory_order_relaxed);
}

bool rb_push(ring_buffer *rb, const log_record *rec)
{
	if (!rb || !rec)
	{
		return false;
	}

	log_record *slot = rb_reserve(rb);
	if (!slot)
	{
		return false;
	}

	/* Copy record data */
	memcpy(slot, rec, sizeof(log_record));
	rb_commit(rb, slot);
	return true;
}

/* ============================================================================
 * Consumer Operations
 * ============================================================================ */

bool rb_pop(ring_buffer *rb, log_record *out_rec)
{
	if (!rb || !out_rec || !rb->buffer)
	{
		return false;
	}

	size_t rd = atomic_load_explicit(&rb->read_idx, memory_order_relaxed);
	size_t slot_idx = rd & rb->mask;
	log_record *rec = &rb->buffer[slot_idx];

	if (!atomic_load_explicit(&rec->ready, memory_order_acquire))
	{
		return false;  /* Queue empty or record not ready */
	}

	/* Copy out the record */
	memcpy(out_rec, rec, sizeof(log_record));

	/* Reset the slot */
	log_record_reset(rec);

	/* Advance read index */
	atomic_fetch_add_explicit(&rb->read_idx, 1, memory_order_release);
	atomic_fetch_add_explicit(&rb->stats.popped, 1, memory_order_relaxed);

	/* Wake up blocked producers */
	if (rb->sync_inited)
	{
		xlog_mutex_lock(&rb->cv_mutex);
		xlog_cond_broadcast(&rb->cv);
		xlog_mutex_unlock(&rb->cv_mutex);
	}

	return true;
}

log_record *rb_peek(ring_buffer *rb)
{
	if (!rb || !rb->buffer)
	{
		return NULL;
	}

	/* Safety: rb_peek returns a raw pointer into the ring buffer.
	 * With DROP_OLDEST, a producer can CAS-advance read_idx and overwrite
	 * this slot while the consumer is reading it. Use rb_pop instead. */
	assert(rb->policy != RB_POLICY_DROP_OLDEST &&
	       "rb_peek is unsafe with DROP_OLDEST policy - use rb_pop instead");

	size_t rd = atomic_load_explicit(&rb->read_idx, memory_order_relaxed);
	size_t slot_idx = rd & rb->mask;
	log_record *rec = &rb->buffer[slot_idx];

	if (!atomic_load_explicit(&rec->ready, memory_order_acquire))
	{
		return NULL;
	}

	return rec;
}

void rb_consume(ring_buffer *rb)
{
	if (!rb || !rb->buffer)
	{
		return;
	}

	size_t rd = atomic_load_explicit(&rb->read_idx, memory_order_relaxed);
	size_t slot_idx = rd & rb->mask;
	log_record *rec = &rb->buffer[slot_idx];

	/* Reset the slot */
	log_record_reset(rec);

	/* Advance read index */
	atomic_fetch_add_explicit(&rb->read_idx, 1, memory_order_release);
	atomic_fetch_add_explicit(&rb->stats.popped, 1, memory_order_relaxed);

	/* Wake up blocked producers */
	if (rb->sync_inited)
	{
		xlog_mutex_lock(&rb->cv_mutex);
		xlog_cond_broadcast(&rb->cv);
		xlog_mutex_unlock(&rb->cv_mutex);
	}
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

bool rb_is_empty(ring_buffer *rb)
{
	if (!rb)
	{
		return true;
	}
	size_t rd = atomic_load_explicit(&rb->read_idx, memory_order_acquire);
	size_t wr = atomic_load_explicit(&rb->write_idx, memory_order_relaxed);
	return rd == wr;
}

bool rb_is_full(ring_buffer *rb)
{
	if (!rb)
	{
		return false;
	}
	size_t rd = atomic_load_explicit(&rb->read_idx, memory_order_acquire);
	size_t wr = atomic_load_explicit(&rb->write_idx, memory_order_relaxed);
	return (wr - rd) >= rb->capacity;
}

size_t rb_size(ring_buffer *rb)
{
	if (!rb)
	{
		return 0;
	}
	size_t rd = atomic_load_explicit(&rb->read_idx, memory_order_acquire);
	size_t wr = atomic_load_explicit(&rb->write_idx, memory_order_relaxed);
	return wr - rd;
}

rb_stats rb_get_stats(ring_buffer *rb)
{
	rb_stats s = {0};
	if (!rb)
	{
		return s;
	}
	s.pushed = atomic_load_explicit(&rb->stats.pushed, memory_order_relaxed);
	s.popped = atomic_load_explicit(&rb->stats.popped, memory_order_relaxed);
	s.dropped = atomic_load_explicit(&rb->stats.dropped, memory_order_relaxed);
	return s;
}

void rb_reset_stats(ring_buffer *rb)
{
	if (!rb)
	{
		return;
	}
	atomic_store_explicit(&rb->stats.pushed, 0, memory_order_relaxed);
	atomic_store_explicit(&rb->stats.popped, 0, memory_order_relaxed);
	atomic_store_explicit(&rb->stats.dropped, 0, memory_order_relaxed);
}

