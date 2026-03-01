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

/* MSVC compatibility for stdatomic and stdalign */
#ifdef _MSC_VER
    #if _MSC_VER >= 1928  /* Visual Studio 2019 16.8+ */
        #include <stdatomic.h>
        #include <stdalign.h>
    #else
        /* Fallback for older MSVC */
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
        #ifndef alignas
            #define alignas(x) __declspec(align(x))
        #endif
        #ifndef ATOMIC_VAR_INIT
            #define ATOMIC_VAR_INIT(val) (val)
        #endif
        #ifndef atomic_store
            #define atomic_store(ptr, val) (*(ptr) = (val))
        #endif
        #ifndef atomic_load
            #define atomic_load(ptr) (*(ptr))
        #endif
    #endif
#else
    #include <stdatomic.h>
    #include <stdalign.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 配置常量
 * ============================================================================ */
#define CACHE_LINE_SIZE         64      /* 缓存行大小 */
#define LOG_MAX_ARGS            8       /* 单条日志最大参数数量 */
#define LOG_INLINE_BUF_SIZE     64      /* 内联缓冲区大小（用于动态字符串深拷贝）*/
#define LOG_MAX_MSG_SIZE        256     /* 预格式化消息最大长度 */
#define LOG_MAX_CUSTOM_FIELDS   2       /* 最大自定义字段数量 */
#define LOG_TAG_MAX_LEN         32      /* 标签最大长度 */
#define LOG_MODULE_MAX_LEN      32      /* 模块名最大长度 */

/* ============================================================================
 * TLV 参数类型定义 (Type-Length-Value)
 * ============================================================================
 * 使用紧凑的类型编码，支持高效的序列化/反序列化
 */
typedef enum log_arg_type
{
	LOG_ARG_NONE = 0x00,     /* 空参数，用于标记结束 */
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
	LOG_ARG_CHAR = 0x0B,     /* char */
	LOG_ARG_BOOL = 0x0C,     /* bool */
	LOG_ARG_PTR = 0x0D,     /* void* (printed as hex address) */
	LOG_ARG_STR_STATIC = 0x10,     /* 静态字符串，只存指针 */
	LOG_ARG_STR_INLINE = 0x11,     /* 动态字符串，深拷贝到 inline_buf */
	LOG_ARG_STR_EXTERN = 0x12,     /* 外部字符串，需要调用者保证生命周期 */
	LOG_ARG_BINARY = 0x20,     /* 二进制数据 (len + data) */
} log_arg_type;

/* ============================================================================
 * 自定义字段类型定义 (Custom Field Types)
 * ============================================================================
 * 用于支持可扩展的元数据字段（如组件标签、模块名、追踪 ID 等）
 */
typedef enum log_field_type
{
	LOG_FIELD_NONE = 0x00,     /* 无效字段 */
	LOG_FIELD_TAG = 0x01,     /* 组件/模块标签 (字符串) */
	LOG_FIELD_MODULE = 0x02,     /* 模块名 */
	LOG_FIELD_COMPONENT = 0x03,     /* 组件名 */
	LOG_FIELD_TRACE_ID = 0x04,     /* 追踪 ID (64-bit) */
	LOG_FIELD_SPAN_ID = 0x05,     /* Span ID (64-bit) */
	LOG_FIELD_REQUEST_ID = 0x06,     /* 请求 ID (字符串) */
	LOG_FIELD_USER_ID = 0x07,     /* 用户 ID */
	LOG_FIELD_SESSION_ID = 0x08,     /* 会话 ID */
	LOG_FIELD_CORRELATION_ID = 0x09,    /* 关联 ID */
	LOG_FIELD_CUSTOM_INT = 0x10,     /* 自定义整数字段 */
	LOG_FIELD_CUSTOM_STR = 0x11,     /* 自定义字符串字段 */
	LOG_FIELD_CUSTOM_FLOAT = 0x12,     /* 自定义浮点字段 */
	LOG_FIELD_CUSTOM_BINARY = 0x13,     /* 自定义二进制字段 */
} log_field_type;

/* ============================================================================
 * 日志参数值联合体
 * ============================================================================
 * 使用 64 位存储所有基础类型，确保内存对齐和快速访问
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
	const char *str;       /* 字符串指针（静态或指向 inline_buf）*/
	uint64_t raw;        /* 原始 64 位值 */
} log_arg_value;

/* ============================================================================
 * 日志参数结构体
 * ============================================================================ */
typedef struct log_arg
{
	log_arg_type type;   /* 参数类型 */
	uint16_t len;    /* 长度（仅用于字符串和二进制数据）*/
	log_arg_value val;    /* 参数值 */
} log_arg;

/* ============================================================================
 * 自定义字段结构体 (Custom Field)
 * ============================================================================
 * 支持灵活的 Key-Value 形式的自定义元数据
 */
typedef struct log_custom_field
{
	log_field_type type;       /* 字段类型 */
	uint8_t key_len;    /* Key 长度（用于自定义字段名）*/
	uint16_t val_len;    /* Value 长度 */
	union
	{
		int64_t i64;        /* 整数值 */
		uint64_t u64;        /* 无符号整数值 */
		double f64;        /* 浮点值 */
		const char *str;       /* 字符串指针 */
		void *ptr;       /* 通用指针 */
	} value;
	const char *key;       /* 字段名（用于 CUSTOM 类型）*/
} log_custom_field;

/* ============================================================================
 * 源码位置信息
 * ============================================================================ */
typedef struct log_source_loc
{
	const char *file;      /* 文件名（静态字符串 __FILE__）*/
	const char *func;      /* 函数名（静态字符串 __func__）*/
	uint32_t line;       /* 行号 __LINE__ */
} log_source_loc;

/* ============================================================================
 * 日志上下文信息 (Log Context)
 * ============================================================================
 * 提供可选的上下文信息，如模块名、组件标签、追踪信息等
 */
typedef struct log_context
{
	const char *module;        /* 模块名（静态或长生命周期字符串）*/
	const char *component;     /* 组件名（静态或长生命周期字符串）*/
	const char *tag;           /* 标签（静态或长生命周期字符串）*/
	uint64_t trace_id;       /* 分布式追踪 ID */
	uint64_t span_id;        /* Span ID */
	uint32_t flags;          /* 上下文标志位 */
} log_context;

/* 上下文标志位定义 */
#define LOG_CTX_HAS_MODULE      (1U << 0)
#define LOG_CTX_HAS_COMPONENT   (1U << 1)
#define LOG_CTX_HAS_TAG         (1U << 2)
#define LOG_CTX_HAS_TRACE_ID    (1U << 3)
#define LOG_CTX_HAS_SPAN_ID     (1U << 4)

/* ============================================================================
 * 高性能格式化配置 (Log Format Pattern)
 * ============================================================================
 * 设计原则：一次定义，编译成固定格式化序列，避免运行时判断
 *
 * 方案：使用预编译的格式化模式数组，每个元素是一个格式化步骤
 * 格式化时直接遍历步骤数组，无需条件判断
 */

/* 格式化步骤类型 */
typedef enum log_fmt_step_type
{
	LOG_STEP_END = 0,    /* 结束标记 */
	LOG_STEP_LITERAL = 1,    /* 字面量字符串 */
	LOG_STEP_TIMESTAMP = 2,    /* 时间戳 */
	LOG_STEP_LEVEL = 3,    /* 日志级别 */
	LOG_STEP_MODULE = 4,    /* 模块名（有值才输出）*/
	LOG_STEP_COMPONENT = 5,    /* 组件名（有值才输出）*/
	LOG_STEP_TAG = 6,    /* 标签（有值才输出）*/
	LOG_STEP_THREAD_ID = 7,    /* 线程 ID */
	LOG_STEP_TRACE_ID = 8,    /* 追踪 ID（有值才输出）*/
	LOG_STEP_SPAN_ID = 9,    /* Span ID（有值才输出）*/
	LOG_STEP_FILE = 10,   /* 文件名 */
	LOG_STEP_LINE = 11,   /* 行号 */
	LOG_STEP_FUNC = 12,   /* 函数名 */
	LOG_STEP_MESSAGE = 13,   /* 消息内容 */
	LOG_STEP_FIELDS = 14,   /* 自定义字段 */
	LOG_STEP_NEWLINE = 15,   /* 换行符 */
	/* 组合步骤 - 减少判断次数 */
	LOG_STEP_MODULE_TAG = 20,   /* [module#tag] 组合 */
	LOG_STEP_LOCATION = 21,   /* [file:line@func] 组合 */
	LOG_STEP_FILE_LINE = 22,   /* [file:line] 组合 */
	/* 统一元信息块 - 所有元数据在一个 [] 中 */
	LOG_STEP_META_BLOCK = 30,   /* [时间  级别  T:线程  模块#标签  trace:xxx  文件:行] */
} log_fmt_step_type;

/* 格式化步骤 */
typedef struct log_fmt_step
{
	uint8_t type;           /* 步骤类型 */
	const char *literal;       /* 字面量（仅 LOG_STEP_LITERAL 使用）*/
} log_fmt_step;

/* 最大步骤数 */
#define LOG_FMT_MAX_STEPS   16

/* 预编译格式化模式 */
typedef struct log_fmt_pattern
{
	log_fmt_step steps[LOG_FMT_MAX_STEPS];
	uint8_t step_count;
} log_fmt_pattern;

/* ============================================================================
 * 预定义格式化模式（编译时常量，零运行时开销）
 * ============================================================================ */

/* 默认格式（统一元信息块）: [时间  级别  T:线程  模块#标签  trace:xxx  文件:行] 消息 {字段} */
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

/* 简洁格式: [时间  级别] 消息 */
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

/* 带模块格式: [时间  级别  模块] 消息 */
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

/* 带标签格式: [时间  级别  模块#标签] 消息 */
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

/* 生产格式（统一元信息块，无位置）: [时间  级别  T:线程  模块#标签] 消息 {字段} */
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

/* 调试格式: 全部信息（保持旧格式，每个字段有单独的[]）*/
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

/* 无位置格式: [时间  级别  T:线程] 消息 */
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

/* 仅文件行号格式: [时间  级别  文件:行] 消息 */
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

/* ============================================================================
 * 日志记录结构体 (Log Record)
 * ============================================================================
 * 这是存入 ring_buffer 的核心数据结构
 * 设计原则：
 *   1. 缓存行对齐，避免 false sharing
 *   2. 热数据（ready, timestamp）在前
 *   3. 固定大小，便于 ring_buffer 管理
 *   4. 支持自定义字段扩展（TLV 协议）
 */

/* 先定义内部布局结构体用于计算大小 */
typedef struct log_record_layout
{
	/* === 热数据区 (Hot) === */
	atomic_bool ready;              /* 槽位就绪标志 */
	uint8_t level;              /* 日志级别 */
	uint8_t arg_count;          /* 参数数量 */
	uint8_t field_count;        /* 自定义字段数量 */
	uint32_t thread_id;          /* 线程 ID */
	uint64_t timestamp_ns;       /* 纳秒时间戳 */

	/* === 格式化信息 === */
	const char *fmt;               /* 格式字符串指针（必须是静态字符串）*/
	log_source_loc loc;                /* 源码位置 */

	/* === 上下文信息（可选）=== */
	log_context ctx;                /* 日志上下文 */

	/* === 参数区 === */
	uint8_t arg_types[LOG_MAX_ARGS];    /* 参数类型数组 */
	uint64_t arg_values[LOG_MAX_ARGS];   /* 参数值数组（raw 形式）*/

	/* === 自定义字段区（TLV 格式）=== */
	log_custom_field custom_fields[LOG_MAX_CUSTOM_FIELDS];

	/* === 内联缓冲区（用于动态字符串深拷贝）=== */
	char inline_buf[LOG_INLINE_BUF_SIZE];
	uint16_t inline_buf_used;    /* 已使用的缓冲区大小 */
} log_record_layout;

/* 计算需要的填充大小 */
enum
{
	LOG_RECORD_PAD_SIZE = (CACHE_LINE_SIZE - (sizeof(log_record_layout) % CACHE_LINE_SIZE)) % CACHE_LINE_SIZE
};

typedef struct log_record
{
	/* === 热数据区 (Hot) === */
	atomic_bool ready;              /* 槽位就绪标志 */
	uint8_t level;              /* 日志级别 */
	uint8_t arg_count;          /* 参数数量 */
	uint8_t field_count;        /* 自定义字段数量 */
	uint32_t thread_id;          /* 线程 ID */
	uint64_t timestamp_ns;       /* 纳秒时间戳 */

	/* === 格式化信息 === */
	const char *fmt;               /* 格式字符串指针（必须是静态字符串）*/
	log_source_loc loc;                /* 源码位置 */

	/* === 上下文信息（可选）=== */
	log_context ctx;                /* 日志上下文 */

	/* === 参数区 === */
	uint8_t arg_types[LOG_MAX_ARGS];    /* 参数类型数组 */
	uint64_t arg_values[LOG_MAX_ARGS];   /* 参数值数组（raw 形式）*/

	/* === 自定义字段区（TLV 格式）=== */
	log_custom_field custom_fields[LOG_MAX_CUSTOM_FIELDS];

	/* === 内联缓冲区（用于动态字符串深拷贝）=== */
	char inline_buf[LOG_INLINE_BUF_SIZE];
	uint16_t inline_buf_used;    /* 已使用的缓冲区大小 */

	/* === 显式填充至缓存行对齐 === */
	char _padding[LOG_RECORD_PAD_SIZE > 0 ? LOG_RECORD_PAD_SIZE : 1];
} log_record __attribute__((aligned(CACHE_LINE_SIZE)));

/* 计算填充需要的宏（用于验证对齐）*/
#define LOG_RECORD_SIZE_CHECK ((sizeof(log_record) + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE * CACHE_LINE_SIZE)

/* 静态断言确保缓存行对齐 */
_Static_assert(sizeof(log_record) % CACHE_LINE_SIZE == 0,
               "log_record must be cache-line aligned");

/* ============================================================================
 * C11 _Generic 类型推导宏
 * ============================================================================
 * 自动推导参数类型，简化 API 使用
 */
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
 * 参数值转换宏
 * ============================================================================
 * 将参数安全地转换为 64 位存储
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

/* ============================================================================
 * 辅助函数：浮点数与 uint64_t 相互转换
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
 * 日志记录初始化
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
	/* 重置上下文 */
	rec->ctx.module = NULL;
	rec->ctx.component = NULL;
	rec->ctx.tag = NULL;
	rec->ctx.trace_id = 0;
	rec->ctx.span_id = 0;
	rec->ctx.flags = 0;
}

/* ============================================================================
 * 设置日志记录的基本信息
 * ============================================================================ */
static inline void log_record_set_meta(
		log_record *rec,
		log_level level,
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
 * 添加参数到日志记录
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
 * 添加动态字符串（深拷贝到内联缓冲区）
 * ============================================================================
 * 注意：如果字符串长度超过 inline_buf 剩余空间，有两种处理方式：
 *   1. log_record_add_string: 标记为 EXTERN（需调用者保证生命周期直到日志消费完成）
 *   2. log_record_add_string_safe: 截断字符串以确保安全（推荐用于异步日志）
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
		/* 静态字符串或 NULL，直接存指针 */
		rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_STATIC;
		rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) str;
	}
	else
	{
		/* 动态字符串，尝试深拷贝 */
		size_t len = strlen(str);
		size_t need = len + 1;  /* 包含 '\0' */

		if (rec->inline_buf_used + need <= LOG_INLINE_BUF_SIZE)
		{
			/* 有足够空间，深拷贝 */
			char *dest = rec->inline_buf + rec->inline_buf_used;
			memcpy(dest, str, need);
			rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_INLINE;
			rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) dest;
			rec->inline_buf_used += (uint16_t) need;
		}
		else
		{
			/* 空间不足，标记为外部字符串（调用者需保证生命周期）*/
			/* 警告：在异步日志中，这可能导致悬空指针！*/
			/* 建议使用 log_record_add_string_safe 替代 */
			rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_EXTERN;
			rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) str;
		}
	}

	rec->arg_count++;
	return true;
}

/**
 * 安全地添加动态字符串（推荐用于异步日志）
 * 当 inline_buf 空间不足时，会截断字符串而不是使用外部引用
 *
 * @param rec       日志记录
 * @param str       字符串（可以是动态分配的）
 * @param is_static 是否是静态字符串（字面量）
 * @return          true=成功, false=失败（参数已满）
 *
 * 截断行为：如果字符串太长，会截断并添加 "..." 后缀
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
		/* 静态字符串或 NULL，直接存指针 */
		rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_STATIC;
		rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) str;
	}
	else
	{
		/* 动态字符串，尝试深拷贝 */
		size_t len = strlen(str);
		size_t avail = LOG_INLINE_BUF_SIZE - rec->inline_buf_used;

		if (avail == 0)
		{
			/* 完全没有空间，使用静态占位符 */
			rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_STATIC;
			rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) "<truncated>";
		}
		else if (len + 1 <= avail)
		{
			/* 有足够空间，完整深拷贝 */
			char *dest = rec->inline_buf + rec->inline_buf_used;
			memcpy(dest, str, len + 1);
			rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_INLINE;
			rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) dest;
			rec->inline_buf_used += (uint16_t) (len + 1);
		}
		else
		{
			/* 空间不足，截断字符串 */
			char *dest = rec->inline_buf + rec->inline_buf_used;
			size_t copy_len = avail - 4;  /* 预留 "...\0" */
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
				/* 连 "..." 都放不下 */
				rec->arg_types[rec->arg_count] = (uint8_t) LOG_ARG_STR_STATIC;
				rec->arg_values[rec->arg_count] = (uint64_t) (uintptr_t) "...";
			}
		}
	}

	rec->arg_count++;
	return true;
}

/* ============================================================================
 * 设置日志上下文信息
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
 * 添加自定义字段 (Custom Fields)
 * ============================================================================
 * 支持 TLV 格式的自定义扩展字段
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

/* 便捷宏：添加常见类型的自定义字段 */
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
 * 提交日志记录（设置 ready 标志）
 * ============================================================================ */
static inline void log_record_commit(log_record *rec)
{
	atomic_store_explicit(&rec->ready, true, memory_order_release);
}

/* ============================================================================
 * 检查日志记录是否就绪
 * ============================================================================ */
static inline bool log_record_is_ready(log_record *rec)
{
	return atomic_load_explicit(&rec->ready, memory_order_acquire);
}

/* ============================================================================
 * 格式化输出接口
 * ============================================================================ */

/**
 * 使用预编译模式格式化日志记录（最高性能）
 * @param rec       日志记录
 * @param pattern   预编译格式模式（使用 LOG_PATTERN_* 宏定义）
 * @param output    输出缓冲区
 * @param out_size  缓冲区大小
 * @return          写入的字节数（不含 '\0'），失败返回 -1
 *
 * 用法示例:
 *   static const log_fmt_pattern pattern = LOG_PATTERN_DEFAULT;
 *   log_record_format_pattern(&rec, &pattern, buf, sizeof(buf));
 */
int log_record_format_pattern(const log_record *rec,
                              const log_fmt_pattern *pattern,
                              char *output, size_t out_size);

/**
 * 使用全局默认模式格式化（便捷接口）
 * @param rec       日志记录
 * @param output    输出缓冲区
 * @param out_size  缓冲区大小
 * @return          写入的字节数（不含 '\0'），失败返回 -1
 */
int log_record_format(const log_record *rec, char *output, size_t out_size);

/**
 * 带颜色的格式化函数（用于终端输出）
 * @param rec       日志记录
 * @param output    输出缓冲区
 * @param out_size  缓冲区大小
 * @param use_color 是否启用颜色
 * @return          写入的字节数（不含 '\0'），失败返回 -1
 */
int log_record_format_colored(const log_record *rec, char *output, size_t out_size, bool use_color);

/* ============================================================================
 * 极致性能内联格式化函数（无 switch-case，直接硬编码）
 * ============================================================================
 * 这些函数为特定格式提供最高性能，编译器可完全内联优化
 */

/**
 * 默认格式内联版本
 * 格式: [时间] [级别] [模块#标签] [T:线程] [trace:] [文件:行@函数] 消息 {字段}
 */
int log_record_format_default_inline(const log_record *rec, char *output, size_t out_size);

/**
 * 简洁格式内联版本
 * 格式: [时间] [级别] 消息
 */
int log_record_format_simple_inline(const log_record *rec, char *output, size_t out_size);

/**
 * 生产格式内联版本（无位置信息）
 * 格式: [时间] [级别] [模块#标签] [T:线程] 消息 {字段}
 */
int log_record_format_prod_inline(const log_record *rec, char *output, size_t out_size);

/**
 * 设置全局默认格式模式
 * @param pattern   格式模式指针（必须是静态/全局生命周期）
 */
void log_format_set_pattern(const log_fmt_pattern *pattern);

/**
 * 获取当前全局默认格式模式
 * @return          当前格式模式指针
 */
const log_fmt_pattern *log_format_get_pattern(void);

/**
 * 获取参数的字符串表示
 * @param type      参数类型
 * @param value     参数值（raw uint64_t）
 * @param output    输出缓冲区
 * @param out_size  缓冲区大小
 * @return          写入的字节数
 */
int log_arg_to_string(log_arg_type type, uint64_t value, char *output, size_t out_size);

/**
 * 格式化自定义字段到输出缓冲区
 * @param field     自定义字段
 * @param output    输出缓冲区
 * @param out_size  缓冲区大小
 * @return          写入的字节数
 */
int log_field_to_string(const log_custom_field *field, char *output, size_t out_size);

/**
 * 获取字段类型名称
 * @param type      字段类型
 * @return          类型名称字符串
 */
const char *log_field_type_name(log_field_type type);

/**
 * 格式化日志上下文信息
 * @param ctx       日志上下文
 * @param output    输出缓冲区
 * @param out_size  缓冲区大小
 * @return          写入的字节数
 */
int log_context_format(const log_context *ctx, char *output, size_t out_size);

/**
 * 调试输出日志记录详情
 * @param rec       日志记录
 * @param fp        输出文件指针
 */
void log_record_dump(const log_record *rec, FILE *fp);

/* ============================================================================
 * 便捷宏：动态字符串处理
 * ============================================================================
 *
 * 对于异步日志系统，动态字符串的生命周期管理很重要：
 *
 * 1. 静态字符串（字面量）- 无需担心生命周期
 *    LOG_INFO("User {} logged in", "admin");  // "admin" 是字面量，安全
 *
 * 2. 动态字符串 - 使用 _safe 版本确保安全
 *    char *name = get_username();
 *    log_record_add_string_safe(&rec, name, false);  // 会深拷贝或截断
 *    free(name);  // 安全，因为已经深拷贝
 *
 * 3. 如果确定消费者会在字符串释放前处理完日志：
 *    log_record_add_string(&rec, name, false);  // 可能标记为 EXTERN
 *    // 必须等待日志消费完成后才能 free(name)
 */

/* 添加动态字符串（安全版本，推荐）*/
#define LOG_ADD_DYN_STR(rec, str) \
    log_record_add_string_safe((rec), (str), false)

/* 添加静态字符串（字面量）*/
#define LOG_ADD_STATIC_STR(rec, str) \
    log_record_add_string((rec), (str), true)

/* 检查字符串是否可以完整存储到 inline_buf */
#define LOG_CAN_INLINE_STR(rec, str) \
    ((strlen(str) + 1) <= (LOG_INLINE_BUF_SIZE - (rec)->inline_buf_used))

#ifdef __cplusplus
}
#endif

#endif /* XLOG_LOG_RECORD_H */

