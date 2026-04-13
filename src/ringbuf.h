/* =====================================================================================
 *       Filename:  ringbuf.h
 *    Description:  Lock-free ring buffer for log_record
 *        Version:  2.0
 *        Created:  2026-02-10
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#ifndef XLOG_RINGBUF_H
#define XLOG_RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include "platform.h"

/* MSVC compatibility for stdatomic and stdalign */
#ifdef _MSC_VER
    #if _MSC_VER >= 1930  /* Visual Studio 2022+ */
        #include <stdatomic.h>
        #include <stdalign.h>
    #else
        /* Fallback handled in platform.h, just need stdalign */
        #ifndef alignas
            #define alignas(x) __declspec(align(x))
        #endif
    #endif
#else
    #include <stdatomic.h>
    #include <stdalign.h>
#endif

/* Forward declaration - log_record is defined in log_record.h */
struct log_record;
typedef struct log_record log_record;

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

#define RB_DEFAULT_CAPACITY         65536
#define RB_SPIN_TIMEOUT_NS_DEFAULT  100000000ULL   /* 100ms */
#define RB_BLOCK_TIMEOUT_NS_DEFAULT 100000000ULL   /* 100ms */

/* ============================================================================
 * Full Queue Policy
 * ============================================================================ */
typedef enum rb_full_policy
{
	RB_POLICY_DROP = 0,  /* Drop newest and count */
	RB_POLICY_SPIN = 1,  /* Spin wait with timeout */
	RB_POLICY_DROP_OLDEST = 2,  /* Overwrite oldest entry */
	RB_POLICY_BLOCK = 3   /* Block wait for space */
} rb_full_policy;

/* ============================================================================
 * Configuration Structure
 * ============================================================================ */
typedef struct rb_config
{
	size_t capacity;           /* Must be power of 2 */
	rb_full_policy policy;
	uint64_t spin_timeout_ns;
	uint64_t block_timeout_ns;
} rb_config;

/* ============================================================================
 * Statistics
 * ============================================================================ */
typedef struct rb_stats
{
	atomic_uint_fast64_t pushed;
	atomic_uint_fast64_t popped;
	atomic_uint_fast64_t dropped;
} rb_stats;

/* ============================================================================
 * Ring Buffer Structure
 * ============================================================================
 * Multi-producer single-consumer lock-free ring buffer.
 * Uses cache-line padding to avoid false sharing.
 */
typedef struct ring_buffer
{
	/* Consumer area (cold) - cache line aligned */
	alignas(CACHE_LINE_SIZE) atomic_size_t read_idx;
	char _pad1[CACHE_LINE_SIZE - sizeof(atomic_size_t)];

	/* Producer area (hot) - cache line aligned */
	alignas(CACHE_LINE_SIZE) atomic_size_t write_idx;
	char _pad2[CACHE_LINE_SIZE - sizeof(atomic_size_t)];

	/* Configuration and statistics */
	size_t capacity;
	size_t mask;
	rb_full_policy policy;
	rb_stats stats;

	/* Timeout configuration */
	uint64_t spin_timeout_ns;
	uint64_t block_timeout_ns;

	/* Synchronization primitives (for BLOCK policy) */
	xlog_mutex_t cv_mutex;
	xlog_cond_t cv;
	bool sync_inited;

	/* Data storage - array of log_record */
	log_record *buffer;

	/* Separate pool for inline string buffers (one per slot) */
	char *inline_pool;
} ring_buffer;

/* ============================================================================
 * Initialization / Destruction
 * ============================================================================ */

bool rb_init(ring_buffer *rb, size_t capacity, rb_full_policy policy,
             uint64_t spin_timeout_ns, uint64_t block_timeout_ns);

bool rb_init_with_config(ring_buffer *rb, const rb_config *cfg);

ring_buffer *rb_create(size_t capacity, rb_full_policy policy);

ring_buffer *rb_create_with_config(const rb_config *cfg);

void rb_free(ring_buffer *rb);

void rb_destroy(ring_buffer *rb);

/* ============================================================================
 * Producer Operations
 * ============================================================================ */

log_record *rb_reserve(ring_buffer *rb);

void rb_commit(ring_buffer *rb, log_record *rec);

bool rb_push(ring_buffer *rb, const log_record *rec);

/* ============================================================================
 * Consumer Operations
 * ============================================================================
 *
 * Two consumption patterns are available:
 *
 * 1) Zero-copy (preferred for single-consumer backend thread):
 *    log_record *rec = rb_peek(rb);   // Get pointer to slot, no copy
 *    if (rec) {
 *        process(rec);                // Read directly from ring buffer slot
 *        rb_consume(rb);             // Release slot and advance read index
 *    }
 *
 * 2) Copy-out (convenience API, for tests or simple use cases):
 *    log_record out;
 *    if (rb_pop(rb, &out)) {         // Copies record out + releases slot
 *        process(&out);
 *    }
 *
 * These are NOT runtime switchable modes. They are two API patterns that
 * the caller chooses at code-writing time. The xlog backend uses zero-copy.
 *
 * THREAD SAFETY:
 *   This is a Multi-Producer Single-Consumer (MPSC) ring buffer.
 *   - Multiple producers can call rb_reserve/rb_commit/rb_push concurrently.
 *   - Only ONE consumer thread may call rb_peek/rb_consume or rb_pop.
 *
 * POLICY SAFETY with zero-copy (rb_peek + rb_consume):
 *   Between rb_peek() and rb_consume(), the consumer holds a raw pointer
 *   into the ring buffer slot. The pointer is safe as long as no producer
 *   can overwrite that slot:
 *
 *   - DROP:         SAFE - producers return NULL when full, never touch
 *                   the consumer's slot.
 *   - SPIN:         SAFE - producers busy-wait until space is available,
 *                   which requires the consumer to call rb_consume() first.
 *   - BLOCK:        SAFE - producers block-wait, same reasoning as SPIN.
 *   - DROP_OLDEST:  UNSAFE - producer CAS-advances read_idx, can overwrite
 *                   the slot the consumer is currently reading.
 *                   Use rb_pop (copy-out) with DROP_OLDEST instead.
 */

bool rb_pop(ring_buffer *rb, log_record *out_rec);

log_record *rb_peek(ring_buffer *rb);

void rb_consume(ring_buffer *rb);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

bool rb_is_empty(ring_buffer *rb);

bool rb_is_full(ring_buffer *rb);

size_t rb_size(ring_buffer *rb);

rb_stats rb_get_stats(ring_buffer *rb);

void rb_reset_stats(ring_buffer *rb);

#ifdef __cplusplus
}
#endif

#endif /* XLOG_RINGBUF_H */

