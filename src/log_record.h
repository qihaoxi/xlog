/* =====================================================================================
 *       Filename:  log_record.h
 *    Description:  High-performance binary log record protocol (Type-Length-Value based)
 *                  Inspired by Quill's deferred formatting approach
 *        Version:  1.0
 *        Created:  2026-02-07
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#ifndef XLOG_LOG_RECORD_H
#define XLOG_LOG_RECORD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "level.h"

/* ============================================================================
 * MSVC Compatibility
 * ============================================================================ */
#ifdef _MSC_VER
    /* MSVC doesn't support C11 stdatomic until VS2022 */
    #if _MSC_VER >= 1930
        #include <stdatomic.h>
        #include <stdalign.h>
    #else
        /* Fallback for older MSVC - types defined in compress.sh header */
        #ifndef XLOG_NO_STDATOMIC
            /* If not already defined by single header preamble */
            #include <windows.h>
            #ifndef atomic_bool
                typedef volatile LONG atomic_bool;
            #endif
            #ifndef atomic_size_t
                typedef volatile size_t atomic_size_t;
            #endif
            #ifndef atomic_uint_fast64_t
                typedef volatile LONGLONG atomic_uint_fast64_t;
            #endif
            #ifndef ATOMIC_VAR_INIT
                #define ATOMIC_VAR_INIT(val) (val)
            #endif
            #ifndef atomic_init
                #define atomic_init(ptr, val) (*(ptr) = (val))
            #endif
            #ifndef atomic_store
                #define atomic_store(ptr, val) (*(ptr) = (val))
            #endif
            #ifndef atomic_store_explicit
                #define atomic_store_explicit(ptr, val, order) (*(ptr) = (val))
            #endif
            #ifndef atomic_load
                #define atomic_load(ptr) (*(ptr))
            #endif
            #ifndef atomic_load_explicit
                #define atomic_load_explicit(ptr, order) (*(ptr))
            #endif
            #ifndef memory_order_relaxed
                #define memory_order_relaxed 0
                #define memory_order_acquire 2
                #define memory_order_release 3
            #endif
        #endif
        /* stdalign fallback */
        #ifndef alignas
            #define alignas(x) __declspec(align(x))
        #endif
    #endif

    /* MSVC uses __declspec(align) instead of __attribute__((aligned)) */
    #define XLOG_ALIGNED_STRUCT(name, alignment) __declspec(align(alignment)) struct name

    /* MSVC static_assert */
    #ifndef _Static_assert
        #define _Static_assert static_assert
    #endif

    /* MSVC doesn't support _Generic in C mode, disable type-safe macros */
    #define XLOG_NO_GENERIC 1

#else
    /* GCC/Clang */
    #include <stdatomic.h>
    #include <stdalign.h>
    #define XLOG_ALIGNED_STRUCT(name, alignment) struct name __attribute__((aligned(alignment)))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 配置常量
 * ============================================================================ */
#define CACHE_LINE_SIZE         64      /* cache line size */
#define LOG_MAX_ARGS            8       /* max args per log record */
#define LOG_INLINE_BUF_SIZE     64      /* inline buffer size (for dynamic string deep copy) */
#define LOG_MAX_MSG_SIZE        256     /* max pre-formatted message size */
#define LOG_MAX_CUSTOM_FIELDS   2       /* max custom fields count */
#define LOG_TAG_MAX_LEN         32      /* max tag length */
#define LOG_MODULE_MAX_LEN      32      /* module name max length */

/* ============================================================================
 * TLV argument type definition (Type-Length-Value)
 * ============================================================================
 * Compact type encoding for efficient serialization
 */
typedef enum log_arg_type
{
	LOG_ARG_NONE = 0x00,     /* empty, marks end */
	LOG_ARG_I8 = 0x01,     /* int8_t */
	LOG_ARG_I16 = 0x02,     /* int16_t */
	LOG_ARG_I32 = 0x03,     /* int32_t */
	LOG_ARG_I64 = 0x04,     /* int64_t */
	LOG_ARG_U8 = 0x05,     /* uint8_t */
	LOG_ARG_U16 = 0x06,     /* uint16_t */
	LOG_ARG_U32 = 0x07,     /* uint32_t */
	LOG_ARG_U64 = 0x08,     /* uint64_t */
	LOG_ARG_F32 = 0x09,     /* float */
	LOG_ARG_F64 = 0x0A,     /* double */
	LOG_ARG_F64_PREC = 0x0E,     /* double with precision (precision stored in len field) */
	LOG_ARG_CHAR = 0x0B,     /* char */
	LOG_ARG_BOOL = 0x0C,     /* bool */
	LOG_ARG_PTR = 0x0D,     /* void* (printed as hex address) */
	LOG_ARG_STR_STATIC = 0x10,     /* static string, store pointer only */
	LOG_ARG_STR_INLINE = 0x11,     /* dynamic string, deep copy to inline_buf */
	LOG_ARG_STR_EXTERN = 0x12,     /* external string, caller must ensure lifetime */
	LOG_ARG_BINARY = 0x20,     /* binary data (len + data) */
} log_arg_type;

/* ============================================================================
 * 自定义field type定义 (Custom Field Types)
 * ============================================================================
 * 用于支持可扩展的元数据字段（如组件标签、module name、追踪 ID 等）
 */
typedef enum log_field_type
{
	LOG_FIELD_NONE = 0x00,     /* invalid field */
	LOG_FIELD_TAG = 0x01,     /* component/module tag (string) */
	LOG_FIELD_MODULE = 0x02,     /* module name */
	LOG_FIELD_COMPONENT = 0x03,     /* component name */
	LOG_FIELD_TRACE_ID = 0x04,     /* trace ID (64-bit) */
	LOG_FIELD_SPAN_ID = 0x05,     /* Span ID (64-bit) */
	LOG_FIELD_REQUEST_ID = 0x06,     /* request ID (string) */
	LOG_FIELD_USER_ID = 0x07,     /* user ID */
	LOG_FIELD_SESSION_ID = 0x08,     /* session ID */
	LOG_FIELD_CORRELATION_ID = 0x09,    /* correlation ID */
	LOG_FIELD_CUSTOM_INT = 0x10,     /* custom integer field */
	LOG_FIELD_CUSTOM_STR = 0x11,     /* custom string field */
	LOG_FIELD_CUSTOM_FLOAT = 0x12,     /* custom float field */
	LOG_FIELD_CUSTOM_BINARY = 0x13,     /* custom binary field */
} log_field_type;

/* ============================================================================
 * 日志argument value联合体
 * ============================================================================
 * 64-bit storage for all basic types, ensures alignment and fast access
 */
typedef union log_arg_value
{
	int8_t i8;
	int16_t i16;
	int32_t i32;
	int64_t i64;
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	float f32;
	double f64;
	char c;
	bool b;
	void *ptr;
	const char *str;       /* string pointer (static or inline_buf)*/
	uint64_t raw;        /* raw 64-bit value */
} log_arg_value;

/* ============================================================================
 * Log Argument Structure
 * ============================================================================ */
typedef struct log_arg
{
	log_arg_type type;   /* argument type */
	uint16_t len;    /* length (for string and binary)*/
	log_arg_value val;    /* argument value */
} log_arg;

/* ============================================================================
 * Custom Field Structure
 * ============================================================================
 * Flexible Key-Value custom metadata
 */
typedef struct log_custom_field
{
	log_field_type type;       /* field type */
	uint8_t key_len;    /* key length*/
	uint16_t val_len;    /* value length */
	union
	{
		int64_t i64;        /* integer value */
		uint64_t u64;        /* 无符号integer value */
		double f64;        /* float value */
		const char *str;       /* string pointer */
		void *ptr;       /* generic pointer */
	} value;
	const char *key;       /* field name (for CUSTOM type)*/
} log_custom_field;

/* ============================================================================
 * Source Location Info
 * ============================================================================ */
typedef struct log_source_loc
{
	const char *file;      /* filename (__FILE__) */
	const char *func;      /* function name (__func__) */
	uint32_t line;         /* line number (__LINE__) */
} log_source_loc;

/* ============================================================================
 * Log Context Info
 * ============================================================================
 * Provides optional context info like module name, component tag, trace info
 */
typedef struct log_context
{
	const char *module;        /* module name */
	const char *component;     /* component name */
	const char *tag;           /* tag */
	uint64_t trace_id;         /* distributed trace ID */
	uint64_t span_id;          /* Span ID */
	uint32_t flags;            /* context flags */
} log_context;

/* Context flag definitions */
#define LOG_CTX_HAS_MODULE      (1U << 0)
#define LOG_CTX_HAS_COMPONENT   (1U << 1)
#define LOG_CTX_HAS_TAG         (1U << 2)
#define LOG_CTX_HAS_TRACE_ID    (1U << 3)
#define LOG_CTX_HAS_SPAN_ID     (1U << 4)

/* ============================================================================
 * High Performance Format Pattern
 * ============================================================================
 * Design: define once, compile to fixed format sequence, no runtime branching
 *
 * Use pre-compiled format pattern array, each element is a format step
 * Traverse step array directly, no conditional checks
 */

/* Format Step Type */
typedef enum log_fmt_step_type
{
	LOG_STEP_END = 0,    /* end marker */
	LOG_STEP_LITERAL = 1,    /* literal string */
	LOG_STEP_TIMESTAMP = 2,    /* timestamp */
	LOG_STEP_LEVEL = 3,    /* log level */
	LOG_STEP_MODULE = 4,    /* module name (output if set) */
	LOG_STEP_COMPONENT = 5,    /* component name (output if set) */
	LOG_STEP_TAG = 6,    /* tag (output if set) */
	LOG_STEP_THREAD_ID = 7,    /* thread ID */
	LOG_STEP_TRACE_ID = 8,    /* trace ID (output if set) */
	LOG_STEP_SPAN_ID = 9,    /* span ID (output if set) */
	LOG_STEP_FILE = 10,   /* filename */
	LOG_STEP_LINE = 11,   /* line number */
	LOG_STEP_FUNC = 12,   /* function name */
	LOG_STEP_MESSAGE = 13,   /* message content */
	LOG_STEP_FIELDS = 14,   /* custom fields */
	LOG_STEP_NEWLINE = 15,   /* newline */
	/* Combined steps - reduce branch count */
	LOG_STEP_MODULE_TAG = 20,   /* [module#tag] combined */
	LOG_STEP_LOCATION = 21,   /* [file:line@func] combined */
	LOG_STEP_FILE_LINE = 22,   /* [file:line] combined */
	/* Unified meta block - all metadata in one [] */
	LOG_STEP_META_BLOCK = 30,   /* [time  level  T:thread  module#tag  trace:xxx  file:line] */
} log_fmt_step_type;

/* Format step */
typedef struct log_fmt_step
{
	uint8_t type;           /* step type */
	const char *literal;       /* literal (LOG_STEP_LITERAL only)*/
} log_fmt_step;

/* max steps */
#define LOG_FMT_MAX_STEPS   16

/* Pre-compiled format pattern */
typedef struct log_fmt_pattern
{
	log_fmt_step steps[LOG_FMT_MAX_STEPS];
	uint8_t step_count;
} log_fmt_pattern;

/* ============================================================================
 * Pre-defined format patterns (compile-time constants, zero runtime overhead)
 * ============================================================================ */

/* Note: Designated initializers (.field=value) are C99/C11 features.
 * MSVC in C mode may not support them fully. These macros work with GCC/Clang.
 * For MSVC, use the runtime initialization functions or compile as C++.
 */
#ifndef _MSC_VER

/* Default format (unified meta block): [time  level  T:thread  module#tag  trace:xxx  file:line] message {fields} */
#define LOG_PATTERN_DEFAULT { \
    .steps = { \
        { LOG_STEP_META_BLOCK, NULL }, \
        { LOG_STEP_MESSAGE,    NULL }, \
        { LOG_STEP_FIELDS,     NULL }, \
        { LOG_STEP_NEWLINE,    NULL }, \
        { LOG_STEP_END,        NULL } \
    }, \
    .step_count = 4 \
}

/* Simple format */
#define LOG_PATTERN_SIMPLE { \
    .steps = { \
        { LOG_STEP_LITERAL,    "[" }, \
        { LOG_STEP_TIMESTAMP,  NULL }, \
        { LOG_STEP_LITERAL,    "  " }, \
        { LOG_STEP_LEVEL,      NULL }, \
        { LOG_STEP_LITERAL,    "] " }, \
        { LOG_STEP_MESSAGE,    NULL }, \
        { LOG_STEP_NEWLINE,    NULL }, \
        { LOG_STEP_END,        NULL } \
    }, \
    .step_count = 7 \
}

/* With module format */
#define LOG_PATTERN_WITH_MODULE { \
    .steps = { \
        { LOG_STEP_LITERAL,    "[" }, \
        { LOG_STEP_TIMESTAMP,  NULL }, \
        { LOG_STEP_LITERAL,    "  " }, \
        { LOG_STEP_LEVEL,      NULL }, \
        { LOG_STEP_LITERAL,    "  " }, \
        { LOG_STEP_MODULE_TAG, NULL }, \
        { LOG_STEP_LITERAL,    "] " }, \
        { LOG_STEP_MESSAGE,    NULL }, \
        { LOG_STEP_NEWLINE,    NULL }, \
        { LOG_STEP_END,        NULL } \
    }, \
    .step_count = 9 \
}

/* With tag format */
#define LOG_PATTERN_WITH_TAG { \
    .steps = { \
        { LOG_STEP_LITERAL,    "[" }, \
        { LOG_STEP_TIMESTAMP,  NULL }, \
        { LOG_STEP_LITERAL,    "  " }, \
        { LOG_STEP_LEVEL,      NULL }, \
        { LOG_STEP_LITERAL,    "  " }, \
        { LOG_STEP_MODULE_TAG, NULL }, \
        { LOG_STEP_LITERAL,    "] " }, \
        { LOG_STEP_MESSAGE,    NULL }, \
        { LOG_STEP_NEWLINE,    NULL }, \
        { LOG_STEP_END,        NULL } \
    }, \
    .step_count = 9 \
}

/* Production format (unified meta block, no location) */
#define LOG_PATTERN_PROD { \
    .steps = { \
        { LOG_STEP_LITERAL,    "[" }, \
        { LOG_STEP_TIMESTAMP,  NULL }, \
        { LOG_STEP_LITERAL,    "  " }, \
        { LOG_STEP_LEVEL,      NULL }, \
        { LOG_STEP_LITERAL,    "  T:" }, \
        { LOG_STEP_THREAD_ID,  NULL }, \
        { LOG_STEP_LITERAL,    "  " }, \
        { LOG_STEP_MODULE_TAG, NULL }, \
        { LOG_STEP_LITERAL,    "] " }, \
        { LOG_STEP_MESSAGE,    NULL }, \
        { LOG_STEP_FIELDS,     NULL }, \
        { LOG_STEP_NEWLINE,    NULL }, \
        { LOG_STEP_END,        NULL } \
    }, \
    .step_count = 12 \
}

/* Debug format: full info */
#define LOG_PATTERN_DEBUG { \
    .steps = { \
        { LOG_STEP_META_BLOCK, NULL }, \
        { LOG_STEP_MESSAGE,    NULL }, \
        { LOG_STEP_FIELDS,     NULL }, \
        { LOG_STEP_NEWLINE,    NULL }, \
        { LOG_STEP_END,        NULL } \
    }, \
    .step_count = 4 \
}

/* No location format */
#define LOG_PATTERN_NO_LOCATION { \
    .steps = { \
        { LOG_STEP_LITERAL,    "[" }, \
        { LOG_STEP_TIMESTAMP,  NULL }, \
        { LOG_STEP_LITERAL,    "  " }, \
        { LOG_STEP_LEVEL,      NULL }, \
        { LOG_STEP_LITERAL,    "  T:" }, \
        { LOG_STEP_THREAD_ID,  NULL }, \
        { LOG_STEP_LITERAL,    "] " }, \
        { LOG_STEP_MESSAGE,    NULL }, \
        { LOG_STEP_NEWLINE,    NULL }, \
        { LOG_STEP_END,        NULL } \
    }, \
    .step_count = 9 \
}

/* File:line only format */
#define LOG_PATTERN_FILE_LINE { \
    .steps = { \
        { LOG_STEP_LITERAL,    "[" }, \
        { LOG_STEP_TIMESTAMP,  NULL }, \
        { LOG_STEP_LITERAL,    "  " }, \
        { LOG_STEP_LEVEL,      NULL }, \
        { LOG_STEP_LITERAL,    "  " }, \
        { LOG_STEP_FILE_LINE,  NULL }, \
        { LOG_STEP_LITERAL,    "] " }, \
        { LOG_STEP_MESSAGE,    NULL }, \
        { LOG_STEP_NEWLINE,    NULL }, \
        { LOG_STEP_END,        NULL } \
    }, \
    .step_count = 9 \
}

#endif /* !_MSC_VER */

/* ============================================================================
 * Log Record Structure
 * ============================================================================
 * Core data structure stored in ring_buffer.
 * Design principles:
 *   1. Cache-line aligned to avoid false sharing
 *   2. Hot data (ready, timestamp) at the front
 *   3. Fixed size for ring_buffer management
 *   4. Supports custom field extension (TLV protocol)
 */

/* Internal layout struct for size calculation */
typedef struct log_record_layout
{
	/* === Hot Data === */
	atomic_bool ready;
	uint8_t level;
	uint8_t arg_count;
	uint8_t field_count;
	uint32_t thread_id;
	uint64_t timestamp_ns;

	/* === Format Info === */
	const char *fmt;
	log_source_loc loc;

	/* === Context (Optional) === */
	log_context ctx;

	/* === Arguments === */
	uint8_t arg_types[LOG_MAX_ARGS];
	uint64_t arg_values[LOG_MAX_ARGS];

	/* === Custom Fields (TLV) === */
	log_custom_field custom_fields[LOG_MAX_CUSTOM_FIELDS];

	/* === Inline Buffer === */
	char inline_buf[LOG_INLINE_BUF_SIZE];
	uint16_t inline_buf_used;
} log_record_layout;

/* Calculate padding size for cache line alignment */
enum
{
	LOG_RECORD_PAD_SIZE = (CACHE_LINE_SIZE - (sizeof(log_record_layout) % CACHE_LINE_SIZE)) % CACHE_LINE_SIZE
};

#ifdef _MSC_VER
/* MSVC: alignment must be specified before struct keyword */
typedef struct __declspec(align(64)) log_record
#else
/* GCC/Clang: use attribute after struct */
typedef struct log_record
#endif
{
	/* === Hot Data === */
	atomic_bool ready;
	uint8_t level;
	uint8_t arg_count;
	uint8_t field_count;
	uint32_t thread_id;
	uint64_t timestamp_ns;

	/* === Format Info === */
	const char *fmt;
	log_source_loc loc;

	/* === Context (Optional) === */
	log_context ctx;

	/* === Arguments === */
	uint8_t arg_types[LOG_MAX_ARGS];
	uint64_t arg_values[LOG_MAX_ARGS];

	/* === Custom Fields (TLV) === */
	log_custom_field custom_fields[LOG_MAX_CUSTOM_FIELDS];

	/* === Inline Buffer === */
	char inline_buf[LOG_INLINE_BUF_SIZE];
	uint16_t inline_buf_used;

	/* === Padding for cache line alignment === */
	char _padding[LOG_RECORD_PAD_SIZE > 0 ? LOG_RECORD_PAD_SIZE : 1];
}
#ifndef _MSC_VER
__attribute__((aligned(CACHE_LINE_SIZE)))
#endif
log_record;

/* Size check macro */
#define LOG_RECORD_SIZE_CHECK ((sizeof(log_record) + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE * CACHE_LINE_SIZE)

/* Static assert for cache line alignment - disabled on MSVC */
#ifndef _MSC_VER
_Static_assert(sizeof(log_record) % CACHE_LINE_SIZE == 0,
               "log_record must be cache-line aligned");
#endif

/* ============================================================================
 * C11 _Generic type deduction macros
 * ============================================================================
 * 自动推导argument type，简化 API 使用
 * Note: MSVC in C mode doesn't support _Generic, so we disable these macros
 */
#ifndef XLOG_NO_GENERIC

#define LOG_ARG_TYPE_OF(x) _Generic((x),                \
    int8_t:         LOG_ARG_I8,                         \
    int16_t:        LOG_ARG_I16,                        \
    int32_t:        LOG_ARG_I32,                        \
    int:            LOG_ARG_I32,                        \
    int64_t:        LOG_ARG_I64,                        \
    long:           LOG_ARG_I64,                        \
    long long:      LOG_ARG_I64,                        \
    uint8_t:        LOG_ARG_U8,                         \
    uint16_t:       LOG_ARG_U16,                        \
    uint32_t:       LOG_ARG_U32,                        \
    unsigned int:   LOG_ARG_U32,                        \
    uint64_t:       LOG_ARG_U64,                        \
    unsigned long:  LOG_ARG_U64,                        \
    unsigned long long: LOG_ARG_U64,                    \
    float:          LOG_ARG_F32,                        \
    double:         LOG_ARG_F64,                        \
    char:           LOG_ARG_CHAR,                       \
    bool:           LOG_ARG_BOOL,                       \
    char*:          LOG_ARG_STR_STATIC,                 \
    const char*:    LOG_ARG_STR_STATIC,                 \
    void*:          LOG_ARG_PTR,                        \
    default:        LOG_ARG_I64)

/* ============================================================================
 * argument value转换宏
 * ============================================================================
 * Safely convert arguments to 64-bit storage
 */
#define LOG_ARG_TO_U64(x) _Generic((x),                 \
    int8_t:         (uint64_t)(int64_t)(x),             \
    int16_t:        (uint64_t)(int64_t)(x),             \
    int32_t:        (uint64_t)(int64_t)(x),             \
    int:            (uint64_t)(int64_t)(x),             \
    int64_t:        (uint64_t)(x),                      \
    long:           (uint64_t)(x),                      \
    long long:      (uint64_t)(x),                      \
    uint8_t:        (uint64_t)(x),                      \
    uint16_t:       (uint64_t)(x),                      \
    uint32_t:       (uint64_t)(x),                      \
    unsigned int:   (uint64_t)(x),                      \
    uint64_t:       (uint64_t)(x),                      \
    unsigned long:  (uint64_t)(x),                      \
    unsigned long long: (uint64_t)(x),                  \
    float:          log_f32_to_u64(x),                  \
    double:         log_f64_to_u64(x),                  \
    char:           (uint64_t)(unsigned char)(x),       \
    bool:           (uint64_t)(x),                      \
    char*:          (uint64_t)(uintptr_t)(x),           \
    const char*:    (uint64_t)(uintptr_t)(x),           \
    void*:          (uint64_t)(uintptr_t)(x),           \
    default:        (uint64_t)(x))

#endif /* XLOG_NO_GENERIC */

/* ============================================================================
 * Helper: float <-> uint64_t conversion
 * ============================================================================ */
static inline uint64_t log_f32_to_u64(float f)
{
	uint64_t u = 0;
	memcpy(&u, &f, sizeof(float));
	return u;
}

static inline uint64_t log_f64_to_u64(double d)
{
	uint64_t u;
	memcpy(&u, &d, sizeof(double));
	return u;
}

static inline float log_u64_to_f32(uint64_t u)
{
	float f;
	memcpy(&f, &u, sizeof(float));
	return f;
}

static inline double log_u64_to_f64(uint64_t u)
{
	double d;
	memcpy(&d, &u, sizeof(double));
	return d;
}

/* ============================================================================
 * Log record initialization
 * ============================================================================ */
static inline void log_record_init(log_record *rec)
{
	memset(rec, 0, sizeof(log_record));
	atomic_init(&rec->ready, false);
}

static inline void log_record_reset(log_record *rec)
{
	atomic_store_explicit(&rec->ready, false, memory_order_release);
	rec->fmt = NULL;
	rec->arg_count = 0;
	rec->field_count = 0;
	rec->inline_buf_used = 0;
	rec->loc.file = NULL;
	rec->loc.func = NULL;
	rec->loc.line = 0;
	/* Reset context */
	rec->ctx.module = NULL;
	rec->ctx.component = NULL;
	rec->ctx.tag = NULL;
	rec->ctx.trace_id = 0;
	rec->ctx.span_id = 0;
	rec->ctx.flags = 0;
}

/* ============================================================================
 * Set log record basic info
 * ============================================================================ */
static inline void log_record_set_meta(
		log_record *rec,
		xlog_level level,
		const char *fmt,
		const char *file,
		const char *func,
		uint32_t line,
		uint32_t thread_id,
		uint64_t timestamp_ns)
{
	rec->level = (uint8_t) level;
	rec->fmt = fmt;
	rec->loc.file = file;
	rec->loc.func = func;
	rec->loc.line = line;
	rec->thread_id = thread_id;
	rec->timestamp_ns = timestamp_ns;
}

/* ============================================================================
 * Add argument to log record
 * ============================================================================ */
static inline bool log_record_add_arg(
		log_record *rec,
		log_arg_type type,
		uint64_t value)
{
	if (rec->arg_count >= LOG_MAX_ARGS)
	{
		return false;
	}
	rec->arg_types[rec->arg_count] = (uint8_t) type;
	rec->arg_values[rec->arg_count] = value;
	rec->arg_count++;
	return true;
}

/* ============================================================================
 * Add dynamic string (deep copy to inline buffer)
 * ============================================================================
 * Note: If string exceeds inline_buf space, two options:
 *   1. log_record_add_string: mark as EXTERN (caller ensures lifetime until consumed)
 *   2. log_record_add_string_safe: truncate string for safety (recommended for async)
 */
static inline bool log_record_add_string(
		log_record *rec,
		const char *str,
		bool is_static)
{
	if (rec->arg_count >= LOG_MAX_ARGS)
	{
		return false;
	}

	if (is_static || str == NULL)
	{
		/* Static string or NULL, store pointer directly */
		rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_STATIC;
		rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) str;
	}
	else
	{
		/* Dynamic string, try deep copy */
		size_t len = strlen(str);
		size_t need = len + 1;  /* including '\0' */

		if (rec->inline_buf_used + need <= LOG_INLINE_BUF_SIZE)
		{
			/* Enough space, deep copy */
			char *dest = rec->inline_buf + rec->inline_buf_used;
			memcpy(dest, str, need);
			rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_INLINE;
			rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) dest;
			rec->inline_buf_used += (uint16_t) need;
		}
		else
		{
			/* Not enough space, mark as external (caller ensures lifetime)*/
			/* Warning: in async logging, this may cause dangling pointer!*/
			/* Recommend using log_record_add_string_safe instead */
			rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_EXTERN;
			rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) str;
		}
	}

	rec->arg_count++;
	return true;
}

/**
 * Safely add dynamic string (recommended for async logging)
 * When inline_buf space insufficient, truncate instead of external reference
 *
 * @param rec       log record
 * @param str       string (can be dynamically allocated)
 * @param is_static is static string (literal)
 * @return          true=success, false=failed (args full)
 *
 * Truncation: if string too long, truncate and add "..." suffix
 */
static inline bool log_record_add_string_safe(
		log_record *rec,
		const char *str,
		bool is_static)
{
	if (rec->arg_count >= LOG_MAX_ARGS)
	{
		return false;
	}

	if (is_static || str == NULL)
	{
		/* Static string or NULL, store pointer directly */
		rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_STATIC;
		rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) str;
	}
	else
	{
		/* Dynamic string, try deep copy */
		size_t len = strlen(str);
		size_t avail = LOG_INLINE_BUF_SIZE - rec->inline_buf_used;

		if (avail == 0)
		{
			/* No space at all, use static placeholder */
			rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_STATIC;
			rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) "<truncated>";
		}
		else if (len + 1 <= avail)
		{
			/* Enough space, full deep copy */
			char *dest = rec->inline_buf + rec->inline_buf_used;
			memcpy(dest, str, len + 1);
			rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_INLINE;
			rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) dest;
			rec->inline_buf_used += (uint16_t) (len + 1);
		}
		else
		{
			/* Not enough space, truncate string */
			char *dest = rec->inline_buf + rec->inline_buf_used;
			size_t copy_len = avail - 4;  /* reserve for "...\0" */
			if (copy_len > 0)
			{
				memcpy(dest, str, copy_len);
				dest[copy_len] = '.';
				dest[copy_len + 1] = '.';
				dest[copy_len + 2] = '.';
				dest[copy_len + 3] = '\0';
				rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_INLINE;
				rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) dest;
				rec->inline_buf_used += (uint16_t) avail;
			}
			else
			{
				/* Cannot even fit "..." */
				rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_STATIC;
				rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) "...";
			}
		}
	}

	rec->arg_count++;
	return true;
}

/* ============================================================================
 * Set log context info
 * ============================================================================ */
static inline void log_record_set_context(
		log_record *rec,
		const log_context *ctx)
{
	if (!rec || !ctx)
	{
		return;
	}
	rec->ctx = *ctx;
}

static inline void log_record_set_module(log_record *rec, const char *module)
{
	if (!rec)
	{
		return;
	}
	rec->ctx.module = module;
	rec->ctx.flags |= LOG_CTX_HAS_MODULE;
}

static inline void log_record_set_component(log_record *rec, const char *component)
{
	if (!rec)
	{
		return;
	}
	rec->ctx.component = component;
	rec->ctx.flags |= LOG_CTX_HAS_COMPONENT;
}

static inline void log_record_set_tag(log_record *rec, const char *tag)
{
	if (!rec)
	{
		return;
	}
	rec->ctx.tag = tag;
	rec->ctx.flags |= LOG_CTX_HAS_TAG;
}

static inline void log_record_set_trace(log_record *rec, uint64_t trace_id, uint64_t span_id)
{
	if (!rec)
	{
		return;
	}
	rec->ctx.trace_id = trace_id;
	rec->ctx.span_id = span_id;
	rec->ctx.flags |= (LOG_CTX_HAS_TRACE_ID | LOG_CTX_HAS_SPAN_ID);
}

/* ============================================================================
 * 添加custom fields (Custom Fields)
 * ============================================================================
 * TLV format custom extension fields
 */
static inline bool log_record_add_field_str(
		log_record *rec,
		log_field_type type,
		const char *key,
		const char *value)
{
	if (!rec || rec->field_count >= LOG_MAX_CUSTOM_FIELDS)
	{
		return false;
	}

	log_custom_field *field = &rec->custom_fields[rec->field_count];
	field->type = type;
	field->key = key;
	field->key_len = key ? (uint8_t) strlen(key) : 0;
	field->value.str = value;
	field->val_len = value ? (uint16_t) strlen(value) : 0;

	rec->field_count++;
	return true;
}

static inline bool log_record_add_field_int(
		log_record *rec,
		log_field_type type,
		const char *key,
		int64_t value)
{
	if (!rec || rec->field_count >= LOG_MAX_CUSTOM_FIELDS)
	{
		return false;
	}

	log_custom_field *field = &rec->custom_fields[rec->field_count];
	field->type = type;
	field->key = key;
	field->key_len = key ? (uint8_t) strlen(key) : 0;
	field->value.i64 = value;
	field->val_len = sizeof(int64_t);

	rec->field_count++;
	return true;
}

static inline bool log_record_add_field_uint(
		log_record *rec,
		log_field_type type,
		const char *key,
		uint64_t value)
{
	if (!rec || rec->field_count >= LOG_MAX_CUSTOM_FIELDS)
	{
		return false;
	}

	log_custom_field *field = &rec->custom_fields[rec->field_count];
	field->type = type;
	field->key = key;
	field->key_len = key ? (uint8_t) strlen(key) : 0;
	field->value.u64 = value;
	field->val_len = sizeof(uint64_t);

	rec->field_count++;
	return true;
}

static inline bool log_record_add_field_float(
		log_record *rec,
		log_field_type type,
		const char *key,
		double value)
{
	if (!rec || rec->field_count >= LOG_MAX_CUSTOM_FIELDS)
	{
		return false;
	}

	log_custom_field *field = &rec->custom_fields[rec->field_count];
	field->type = type;
	field->key = key;
	field->key_len = key ? (uint8_t) strlen(key) : 0;
	field->value.f64 = value;
	field->val_len = sizeof(double);

	rec->field_count++;
	return true;
}

/* 便捷宏：添加常见类型的custom fields */
#define log_record_add_tag(rec, tag_name) \
    log_record_add_field_str((rec), LOG_FIELD_TAG, NULL, (tag_name))

#define log_record_add_module_field(rec, module_name) \
    log_record_add_field_str((rec), LOG_FIELD_MODULE, NULL, (module_name))

#define log_record_add_component_field(rec, comp_name) \
    log_record_add_field_str((rec), LOG_FIELD_COMPONENT, NULL, (comp_name))

#define log_record_add_trace_id_field(rec, trace_id) \
    log_record_add_field_uint((rec), LOG_FIELD_TRACE_ID, NULL, (trace_id))

#define log_record_add_span_id_field(rec, span_id) \
    log_record_add_field_uint((rec), LOG_FIELD_SPAN_ID, NULL, (span_id))

#define log_record_add_request_id(rec, request_id) \
    log_record_add_field_str((rec), LOG_FIELD_REQUEST_ID, NULL, (request_id))

#define log_record_add_custom_str(rec, key, value) \
    log_record_add_field_str((rec), LOG_FIELD_CUSTOM_STR, (key), (value))

#define log_record_add_custom_int(rec, key, value) \
    log_record_add_field_int((rec), LOG_FIELD_CUSTOM_INT, (key), (value))

#define log_record_add_custom_float(rec, key, value) \
    log_record_add_field_float((rec), LOG_FIELD_CUSTOM_FLOAT, (key), (value))

/* ============================================================================
 * 提交log record（设置 ready 标志）
 * ============================================================================ */
static inline void log_record_commit(log_record *rec)
{
	atomic_store_explicit(&rec->ready, true, memory_order_release);
}

/* ============================================================================
 * 检查log record是否就绪
 * ============================================================================ */
static inline bool log_record_is_ready(log_record *rec)
{
	return atomic_load_explicit(&rec->ready, memory_order_acquire);
}

/* ============================================================================
 * Format Output Interface
 * ============================================================================ */

/**
 * 使用预编译模式格式化log record（最高性能）
 * @param rec       log record
 * @param pattern   Pre-compiled format pattern (use LOG_PATTERN_* macros)
 * @param output    output buffer
 * @param out_size  buffer size
 * @return          bytes written (excluding \0), -1 on failure
 *
 * Usage example:
 *   static const log_fmt_pattern pattern = LOG_PATTERN_DEFAULT;
 *   log_record_format_pattern(&rec, &pattern, buf, sizeof(buf));
 */
int log_record_format_pattern(const log_record *rec,
                              const log_fmt_pattern *pattern,
                              char *output, size_t out_size);

/**
 * Format with global default pattern (convenience)
 * @param rec       log record
 * @param output    output buffer
 * @param out_size  buffer size
 * @return          bytes written (excluding \0), -1 on failure
 */
int log_record_format(const log_record *rec, char *output, size_t out_size);

/**
 * Colored format function (for terminal output)
 * @param rec       log record
 * @param output    output buffer
 * @param out_size  buffer size
 * @param use_color enable color
 * @return          bytes written (excluding \0), -1 on failure
 */
int log_record_format_colored(const log_record *rec, char *output, size_t out_size, bool use_color);

/* ============================================================================
 * High performance inline format functions (no switch-case, hardcoded)
 * ============================================================================
 * These functions provide max performance for specific formats, fully inlinable
 */

/**
 * Default format inline version
 * 格式: [时间] [级别] [模块#标签] [T:线程] [trace:] [文件:行@函数] message {fields}
 */
int log_record_format_default_inline(const log_record *rec, char *output, size_t out_size);

/**
 * Simple format inline version
 * Format: [time] [level] message
 */
int log_record_format_simple_inline(const log_record *rec, char *output, size_t out_size);

/**
 * Production format inline version (no location)
 * 格式: [时间] [级别] [模块#标签] [T:线程] message {fields}
 */
int log_record_format_prod_inline(const log_record *rec, char *output, size_t out_size);

/**
 * Set global default format pattern
 * @param pattern   Pattern pointer (must be static/global lifetime)
 */
void log_format_set_pattern(const log_fmt_pattern *pattern);

/**
 * Get current global default format pattern
 * @return          Current pattern pointer
 */
const log_fmt_pattern *log_format_get_pattern(void);

/**
 * Get string representation of argument
 * @param type      argument type
 * @param value     argument value（raw uint64_t）
 * @param output    output buffer
 * @param out_size  buffer size
 * @return          bytes written
 */
int log_arg_to_string(log_arg_type type, uint64_t value, char *output, size_t out_size);

/**
 * 格式化custom fields到output buffer
 * @param field     custom fields
 * @param output    output buffer
 * @param out_size  buffer size
 * @return          bytes written
 */
int log_field_to_string(const log_custom_field *field, char *output, size_t out_size);

/**
 * 获取field type名称
 * @param type      field type
 * @return          Type name string
 */
const char *log_field_type_name(log_field_type type);

/**
 * Format log context info
 * @param ctx       log context
 * @param output    output buffer
 * @param out_size  buffer size
 * @return          bytes written
 */
int log_context_format(const log_context *ctx, char *output, size_t out_size);

/**
 * 调试输出log record详情
 * @param rec       log record
 * @param fp        output file pointer
 */
void log_record_dump(const log_record *rec, FILE *fp);

/* ============================================================================
 * Convenience macros: dynamic string handling
 * ============================================================================
 *
 * For async logging, dynamic string lifetime management is important:
 *
 * 1. Static strings (literals) - no lifetime concerns
 *    LOG_INFO("User {} logged in", "admin");  // "admin" is literal, safe
 *
 * 2. Dynamic strings - use _safe version for safety
 *    char *name = get_username();
 *    log_record_add_string_safe(&rec, name, false);  // will deep copy or truncate
 *    free(name);  // safe, already deep copied
 *
 * 3. If consumer will process log before string is freed:
 *    log_record_add_string(&rec, name, false);  // may be marked as EXTERN
 *    // must wait for log consumption before free(name)
 */

/* Add dynamic string (safe version, recommended)*/
#define LOG_ADD_DYN_STR(rec, str) \
    log_record_add_string_safe((rec), (str), false)

/* Add static string (literal)*/
#define LOG_ADD_STATIC_STR(rec, str) \
    log_record_add_string((rec), (str), true)

/* Check if string can fully fit in inline_buf */
#define LOG_CAN_INLINE_STR(rec, str) \
    ((strlen(str) + 1) <= (LOG_INLINE_BUF_SIZE - (rec)->inline_buf_used))

#ifdef __cplusplus
}
#endif

#endif /* XLOG_LOG_RECORD_H */

