/* =====================================================================================
 *       Filename:  log_record.c
 *    Description:  Log record formatting and argument parsing implementation
 *        Version:  1.0
 *        Created:  2026-02-07
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>
#include "platform.h"
#include "log_record.h"
#include "simd.h"

/* ============================================================================
 * log level name
 * ============================================================================ */
static const char *const LOG_LEVEL_NAMES[] = {
		"TRACE",
		"DEBUG",
		"INFO",
		"WARN",      /* matches XLOG_LEVEL_WARNING = 3 */
		"ERROR",
		"FATAL"
};

static const char *log_level_to_str(xlog_level level)
{
	if (level >= 0 && level <= XLOG_LEVEL_FATAL)
	{
		return LOG_LEVEL_NAMES[level];
	}
	return "UNKNOWN";
}

/* ============================================================================
 * custom field type name
 * ============================================================================ */
const char *log_field_type_name(log_field_type type)
{
	switch (type)
	{
		case LOG_FIELD_NONE:
			return "none";
		case LOG_FIELD_TAG:
			return "tag";
		case LOG_FIELD_MODULE:
			return "module";
		case LOG_FIELD_COMPONENT:
			return "component";
		case LOG_FIELD_TRACE_ID:
			return "trace_id";
		case LOG_FIELD_SPAN_ID:
			return "span_id";
		case LOG_FIELD_REQUEST_ID:
			return "request_id";
		case LOG_FIELD_USER_ID:
			return "user_id";
		case LOG_FIELD_SESSION_ID:
			return "session_id";
		case LOG_FIELD_CORRELATION_ID:
			return "correlation_id";
		case LOG_FIELD_CUSTOM_INT:
			return "custom_int";
		case LOG_FIELD_CUSTOM_STR:
			return "custom_str";
		case LOG_FIELD_CUSTOM_FLOAT:
			return "custom_float";
		case LOG_FIELD_CUSTOM_BINARY:
			return "custom_binary";
		default:
			return "unknown";
	}
}

/* ============================================================================
 * Global format config - use pre-compiled pattern
 * ============================================================================ */
#ifdef _MSC_VER
/* MSVC: initialize at runtime */
static log_fmt_pattern g_default_pattern;
static atomic_uintptr_t g_current_pattern_atomic = 0;
static atomic_int g_pattern_initialized = 0;

static void init_default_pattern(void)
{
	/* Double-checked locking with atomics */
	if (atomic_load(&g_pattern_initialized)) return;

	/* Simple spinlock-free initialization - worst case: init runs multiple times */
	memset(&g_default_pattern, 0, sizeof(g_default_pattern));
	g_default_pattern.steps[0].type = LOG_STEP_META_BLOCK;
	g_default_pattern.steps[0].literal = NULL;
	g_default_pattern.steps[1].type = LOG_STEP_MESSAGE;
	g_default_pattern.steps[1].literal = NULL;
	g_default_pattern.steps[2].type = LOG_STEP_FIELDS;
	g_default_pattern.steps[2].literal = NULL;
	g_default_pattern.steps[3].type = LOG_STEP_NEWLINE;
	g_default_pattern.steps[3].literal = NULL;
	g_default_pattern.steps[4].type = LOG_STEP_END;
	g_default_pattern.steps[4].literal = NULL;
	g_default_pattern.step_count = 4;
	atomic_store(&g_current_pattern_atomic, (uintptr_t)&g_default_pattern);
	atomic_store(&g_pattern_initialized, 1);
}
#define ENSURE_PATTERN_INIT() do { if (!atomic_load(&g_pattern_initialized)) init_default_pattern(); } while(0)
#define GET_CURRENT_PATTERN() ((const log_fmt_pattern*)atomic_load(&g_current_pattern_atomic))
#define SET_CURRENT_PATTERN(p) atomic_store(&g_current_pattern_atomic, (uintptr_t)(p))
#else
/* GCC/Clang: use designated initializers */
static const log_fmt_pattern g_default_pattern = LOG_PATTERN_DEFAULT;
static atomic_uintptr_t g_current_pattern_atomic = (uintptr_t)&g_default_pattern;
#define ENSURE_PATTERN_INIT() ((void)0)
#define GET_CURRENT_PATTERN() ((const log_fmt_pattern*)atomic_load(&g_current_pattern_atomic))
#define SET_CURRENT_PATTERN(p) atomic_store(&g_current_pattern_atomic, (uintptr_t)(p))
#endif

void log_format_set_pattern(const log_fmt_pattern *pattern)
{
	ENSURE_PATTERN_INIT();
	if (pattern)
	{
		SET_CURRENT_PATTERN(pattern);
	}
	else
	{
		SET_CURRENT_PATTERN(&g_default_pattern);
	}
}

const log_fmt_pattern *log_format_get_pattern(void)
{
	ENSURE_PATTERN_INIT();
	return GET_CURRENT_PATTERN();
}

/* ============================================================================
 * High Performance Timestamp Formatting (Second-level Cache)
 * ============================================================================
 * Design principles:
 * - localtime_r has high overhead (locks, timezone calculation)
 * - Second-level cache: only call localtime_r when second changes
 * - Microsecond part calculated in real-time (lock-free, very low overhead)
 *
 * Performance improvement: ~10-20x speedup in high-frequency logging
 */

/* Cache structure (thread-local storage, avoids lock contention) */
typedef struct timestamp_cache
{
	time_t cached_sec;         /* cached seconds */
	char date_time[20];        /* "YYYY-MM-DD HH:MM:SS" (19 chars + '\0') */
	int date_time_len;         /* datetime string length */
} timestamp_cache;

/* Thread-local cache - MSVC requires different syntax */
#ifdef _MSC_VER
static __declspec(thread) timestamp_cache g_ts_cache;
#else
static __thread timestamp_cache g_ts_cache;
#endif

static int format_timestamp(uint64_t timestamp_ns, char *buf, size_t size)
{
	if (!buf || size < 27)
	{  /* Need at least "YYYY-MM-DD HH:MM:SS.uuuuuu\0" */
		return snprintf(buf, size, "0000-00-00 00:00:00.000000");
	}

	time_t sec = (time_t) (timestamp_ns / 1000000000ULL);
	uint32_t usec = (uint32_t) ((timestamp_ns % 1000000000ULL) / 1000);

	/* Check if cache is valid (same second, reuse) */
	if (sec != g_ts_cache.cached_sec)
	{
		/* Second changed, need to recalculate */
		struct tm tm_buf;
		xlog_get_localtime(sec, &tm_buf);

		/* Update cache - use SIMD optimized datetime formatting */
		g_ts_cache.cached_sec = sec;
		g_ts_cache.date_time_len = xlog_simd_format_datetime(
				tm_buf.tm_year + 1900,
				tm_buf.tm_mon + 1,
				tm_buf.tm_mday,
				tm_buf.tm_hour,
				tm_buf.tm_min,
				tm_buf.tm_sec,
				g_ts_cache.date_time);
	}

	/* 复用缓存的日期时间部分，使用 SIMD 优化的微秒格式化 */
	memcpy(buf, g_ts_cache.date_time, (size_t) g_ts_cache.date_time_len);
	xlog_simd_format_usec(usec, buf + g_ts_cache.date_time_len);
	return g_ts_cache.date_time_len + 7;  /* 7 = ".NNNNNN" + '\0' 算 7 字符 */
}

/* ============================================================================
 * 参数格式化 - 使用 Fast IToA 替代 snprintf
 * ============================================================================ */

/* Fast string copy with length return */
static inline int fast_strcpy(char *dst, size_t dst_size, const char *src)
{
	size_t len = strlen(src);
	if (len >= dst_size)
	{
		len = dst_size - 1;
	}
	memcpy(dst, src, len);
	dst[len] = '\0';
	return (int) len;
}

/* Fast signed integer conversion using SIMD routines */
static inline int fast_i32toa(int32_t value, char *output, size_t out_size)
{
	if (out_size < 12) /* -2147483648 + '\0' = 12 bytes max */
	{
		return snprintf(output, out_size, "%" PRId32, value);
	}
	/* Special handling for INT32_MIN: use i64 path to avoid overflow and u32 precision issues */
	if (value == INT32_MIN)
	{
		return xlog_simd_i64toa((int64_t) value, output);
	}
	int len;
	if (value < 0)
	{
		output[0] = '-';
		len = 1 + xlog_simd_u32toa((uint32_t)(-value), output + 1);
	}
	else
	{
		len = xlog_simd_u32toa((uint32_t) value, output);
	}
	return len;
}

/* Format float/double with original format specifier (e.g., "%.2f", "%8.4e") */
static int format_float_with_spec(double value, const char *fmt_start, const char *fmt_end,
                                   char *output, size_t out_size)
{
	/* Build format string from original format specifier */
	char fmt_buf[32];
	size_t fmt_len = (size_t)(fmt_end - fmt_start);

	if (fmt_len >= sizeof(fmt_buf) - 1)
	{
		/* Fallback to default format if spec is too long */
		return snprintf(output, out_size, "%g", value);
	}

	/* Copy format spec: "%.2f" or similar */
	fmt_buf[0] = '%';
	memcpy(fmt_buf + 1, fmt_start, fmt_len);
	fmt_buf[fmt_len + 1] = '\0';

	return snprintf(output, out_size, fmt_buf, value);
}

int log_arg_to_string(log_arg_type type, uint64_t value, char *output, size_t out_size)
{
	if (!output || out_size == 0)
	{
		return -1;
	}

	switch (type)
	{
		case LOG_ARG_NONE:
			return fast_strcpy(output, out_size, "(null)");

		case LOG_ARG_I8:
			return fast_i32toa((int8_t) value, output, out_size);

		case LOG_ARG_I16:
			return fast_i32toa((int16_t) value, output, out_size);

		case LOG_ARG_I32:
			return fast_i32toa((int32_t) value, output, out_size);

		case LOG_ARG_I64:
			if (out_size < 22)
			{
				return snprintf(output, out_size, "%" PRId64, (int64_t) value);
			}
			return xlog_simd_i64toa((int64_t) value, output);

		case LOG_ARG_U8:
			if (out_size < 4)
			{
				return snprintf(output, out_size, "%" PRIu8, (uint8_t) value);
			}
			return xlog_simd_u32toa((uint8_t) value, output);

		case LOG_ARG_U16:
			if (out_size < 6)
			{
				return snprintf(output, out_size, "%" PRIu16, (uint16_t) value);
			}
			return xlog_simd_u32toa((uint16_t) value, output);

		case LOG_ARG_U32:
			if (out_size < 11)
			{
				return snprintf(output, out_size, "%" PRIu32, (uint32_t) value);
			}
			return xlog_simd_u32toa((uint32_t) value, output);

		case LOG_ARG_U64:
			if (out_size < 21)
			{
				return snprintf(output, out_size, "%" PRIu64, value);
			}
			return xlog_simd_u64toa(value, output);

		case LOG_ARG_F32:
		{
			float f = log_u64_to_f32(value);
			return snprintf(output, out_size, "%g", (double) f);
		}

		case LOG_ARG_F64:
		{
			double d = log_u64_to_f64(value);
			return snprintf(output, out_size, "%g", d);
		}

		case LOG_ARG_CHAR:
			if (out_size < 2)
			{
				return -1;
			}
			output[0] = (char) value;
			output[1] = '\0';
			return 1;

		case LOG_ARG_BOOL:
			return fast_strcpy(output, out_size, value ? "true" : "false");

		case LOG_ARG_PTR:
			if (value == 0)
			{
				return fast_strcpy(output, out_size, "(nil)");
			}
			return snprintf(output, out_size, "%p", (void *) (uintptr_t) value);

		case LOG_ARG_STR_STATIC:
		case LOG_ARG_STR_INLINE:
		case LOG_ARG_STR_EXTERN:
		{
			const char *str = (const char *) (uintptr_t) value;
			if (str == NULL)
			{
				return fast_strcpy(output, out_size, "(null)");
			}
			return fast_strcpy(output, out_size, str);
		}

		case LOG_ARG_BINARY:
			return snprintf(output, out_size, "<binary:%p>", (void *) (uintptr_t) value);

		default:
			return snprintf(output, out_size, "<unknown:%#" PRIx64 ">", value);
	}
}

/* ============================================================================
 * 自定义字段格式化
 * ============================================================================ */
int log_field_to_string(const log_custom_field *field, char *output, size_t out_size)
{
	if (!field || !output || out_size == 0)
	{
		return -1;
	}

	const char *type_name = log_field_type_name(field->type);

	switch (field->type)
	{
		case LOG_FIELD_TAG:
		case LOG_FIELD_MODULE:
		case LOG_FIELD_COMPONENT:
		case LOG_FIELD_REQUEST_ID:
		case LOG_FIELD_SESSION_ID:
		case LOG_FIELD_CORRELATION_ID:
			/* 预定义字符串类型字段 */
			if (field->value.str)
			{
				return snprintf(output, out_size, "%s=%s", type_name, field->value.str);
			}
			return snprintf(output, out_size, "%s=(null)", type_name);

		case LOG_FIELD_TRACE_ID:
		case LOG_FIELD_SPAN_ID:
		case LOG_FIELD_USER_ID:
			/* 预定义整数类型字段（十六进制输出）*/
			return snprintf(output, out_size, "%s=%016" PRIx64, type_name, field->value.u64);

		case LOG_FIELD_CUSTOM_INT:
			/* 自定义整数字段 */
			if (field->key)
			{
				return snprintf(output, out_size, "%s=%" PRId64, field->key, field->value.i64);
			}
			return snprintf(output, out_size, "custom_int=%" PRId64, field->value.i64);

		case LOG_FIELD_CUSTOM_STR:
			/* 自定义字符串字段 */
			if (field->key && field->value.str)
			{
				return snprintf(output, out_size, "%s=%s", field->key, field->value.str);
			}
			else if (field->key)
			{
				return snprintf(output, out_size, "%s=(null)", field->key);
			}
			return snprintf(output, out_size, "custom_str=%s",
			                field->value.str ? field->value.str : "(null)");

		case LOG_FIELD_CUSTOM_FLOAT:
			/* 自定义浮点字段 */
			if (field->key)
			{
				return snprintf(output, out_size, "%s=%g", field->key, field->value.f64);
			}
			return snprintf(output, out_size, "custom_float=%g", field->value.f64);

		case LOG_FIELD_CUSTOM_BINARY:
			/* 自定义二进制字段 */
			if (field->key)
			{
				return snprintf(output, out_size, "%s=<binary:%u>", field->key, field->val_len);
			}
			return snprintf(output, out_size, "custom_binary=<binary:%u>", field->val_len);

		default:
			return snprintf(output, out_size, "<%s>", type_name);
	}
}

/* ============================================================================
 * 日志上下文格式化
 * ============================================================================ */
int log_context_format(const log_context *ctx, char *output, size_t out_size)
{
	if (!ctx || !output || out_size == 0)
	{
		return 0;
	}

	char *p = output;
	char *end = output + out_size - 1;
	int written;
	bool first = true;

	/* 模块名 */
	if ((ctx->flags & LOG_CTX_HAS_MODULE) && ctx->module)
	{
		written = snprintf(p, (size_t) (end - p), "%s[%s]",
		                   first ? "" : " ", ctx->module);
		if (written > 0 && p + written < end)
		{
			p += written;
			first = false;
		}
	}

	/* 组件名 */
	if ((ctx->flags & LOG_CTX_HAS_COMPONENT) && ctx->component)
	{
		written = snprintf(p, (size_t) (end - p), "%s<%s>",
		                   first ? "" : " ", ctx->component);
		if (written > 0 && p + written < end)
		{
			p += written;
			first = false;
		}
	}

	/* 标签 */
	if ((ctx->flags & LOG_CTX_HAS_TAG) && ctx->tag)
	{
		written = snprintf(p, (size_t) (end - p), "%s#%s",
		                   first ? "" : " ", ctx->tag);
		if (written > 0 && p + written < end)
		{
			p += written;
			first = false;
		}
	}

	/* 追踪 ID */
	if (ctx->flags & LOG_CTX_HAS_TRACE_ID)
	{
		written = snprintf(p, (size_t) (end - p), "%strace:%016" PRIx64,
		                   first ? "" : " ", ctx->trace_id);
		if (written > 0 && p + written < end)
		{
			p += written;
			first = false;
		}
	}

	/* Span ID */
	if (ctx->flags & LOG_CTX_HAS_SPAN_ID)
	{
		written = snprintf(p, (size_t) (end - p), "%sspan:%016" PRIx64,
		                   first ? "" : " ", ctx->span_id);
		if (written > 0 && p + written < end)
		{
			p += written;
		}
	}

	*p = '\0';
	return (int) (p - output);
}

/* ============================================================================
 * 消息格式化辅助函数
 * ============================================================================ */
static int format_message_content(const log_record *rec, char *p, char *end)
{
	if (p >= end)
	{
		return 0;
	}

	if (!rec->fmt)
	{
		int avail = (int) (end - p);
		if (avail <= 0)
		{
			return 0;
		}
		int written = snprintf(p, (size_t) avail, "(no message)");
		return (written > 0 && written < avail) ? written : (avail > 0 ? avail - 1 : 0);
	}

	char *start = p;
	const char *fmt = rec->fmt;
	int arg_idx = 0;
	char arg_buf[256];

	while (*fmt && p < end)
	{
		if (*fmt == '%')
		{
			const char *spec_start = fmt + 1; /* Position after '%' for float precision */
			fmt++;
			if (*fmt == '%')
			{
				if (p < end)
				{
					*p++ = '%';
				}
				fmt++;
			}
			else if (*fmt == '\0')
			{
				break;
			}
			else
			{
				/* Skip format modifiers but remember start for float precision */
				while (*fmt && (*fmt == '-' || *fmt == '+' || *fmt == ' ' ||
				                *fmt == '#' || *fmt == '0' || (*fmt >= '1' && *fmt <= '9') ||
				                *fmt == '.' || *fmt == 'l' || *fmt == 'h' || *fmt == 'z' ||
				                *fmt == 'j' || *fmt == 't' || *fmt == 'L'))
				{
					fmt++;
				}

				if (arg_idx < rec->arg_count)
				{
					log_arg_type type = (log_arg_type) rec->arg_types[arg_idx];
					uint64_t value = rec->arg_values[arg_idx];

					/* String types: copy directly to avoid truncation */
					if (type == LOG_ARG_STR_STATIC || type == LOG_ARG_STR_INLINE || type == LOG_ARG_STR_EXTERN)
					{
						const char *str = (const char *) (uintptr_t) value;
						if (str == NULL)
						{
							str = "(null)";
						}
						while (*str && p < end)
						{
							*p++ = *str++;
						}
					}
					/* Float types: use original format spec to preserve precision */
					else if (type == LOG_ARG_F32 || type == LOG_ARG_F64)
					{
						double d = (type == LOG_ARG_F32) ?
						           (double)log_u64_to_f32(value) : log_u64_to_f64(value);
						int n = format_float_with_spec(d, spec_start, fmt + 1, arg_buf, sizeof(arg_buf));
						if (n > 0 && p < end)
						{
							int actual_len = (n < (int) sizeof(arg_buf)) ? n : (int) sizeof(arg_buf) - 1;
							int avail = (int) (end - p);
							int copy_len = (actual_len < avail) ? actual_len : avail;
							if (copy_len > 0)
							{
								memcpy(p, arg_buf, (size_t) copy_len);
								p += copy_len;
							}
						}
					}
					else
					{
						int n = log_arg_to_string(type, value, arg_buf, sizeof(arg_buf));
						if (n > 0 && p < end)
						{
							int actual_len = (n < (int) sizeof(arg_buf)) ? n : (int) sizeof(arg_buf) - 1;
							int avail = (int) (end - p);
							int copy_len = (actual_len < avail) ? actual_len : avail;
							if (copy_len > 0)
							{
								memcpy(p, arg_buf, (size_t) copy_len);
								p += copy_len;
							}
						}
					}
					arg_idx++;
				}
				else
				{
					if (p < end)
					{
						int avail = (int) (end - p);
						int w = snprintf(p, (size_t) avail, "<?>");
						if (w > 0 && w < avail)
						{
							p += w;
						}
					}
				}
				if (*fmt)
				{
					fmt++;
				}
			}
		}
		else if (*fmt == '{' && *(fmt + 1) == '}')
		{
			if (arg_idx < rec->arg_count)
			{
				log_arg_type type = (log_arg_type) rec->arg_types[arg_idx];
				uint64_t value = rec->arg_values[arg_idx];

				/* 对字符串类型特殊处理，直接复制避免中间缓冲区截断 */
				if (type == LOG_ARG_STR_STATIC || type == LOG_ARG_STR_INLINE || type == LOG_ARG_STR_EXTERN)
				{
					const char *str = (const char *) (uintptr_t) value;
					if (str == NULL)
					{
						str = "(null)";
					}
					while (*str && p < end)
					{
						*p++ = *str++;
					}
				}
				else
				{
					int n = log_arg_to_string(type, value, arg_buf, sizeof(arg_buf));
					if (n > 0 && p < end)
					{
						/* n 可能比 arg_buf 大（snprintf 返回需要的字节数）*/
						int actual_len = (n < (int) sizeof(arg_buf)) ? n : (int) sizeof(arg_buf) - 1;
						int avail = (int) (end - p);
						int copy_len = (actual_len < avail) ? actual_len : avail;
						if (copy_len > 0)
						{
							memcpy(p, arg_buf, (size_t) copy_len);
							p += copy_len;
						}
					}
				}
				arg_idx++;
			}
			else
			{
				if (p < end)
				{
					int avail = (int) (end - p);
					int w = snprintf(p, (size_t) avail, "<?>");
					if (w > 0 && w < avail)
					{
						p += w;
					}
				}
			}
			fmt += 2;
		}
		else
		{
			if (p < end)
			{
				*p++ = *fmt;
			}
			fmt++;
		}
	}

	return (int) (p - start);
}

static int format_meta_block(const log_record *rec, char *p, char *end)
{
	if (p >= end)
	{
		return 0;
	}

	char *start = p;
	char tmp[64];
	int len;

	/* 开始 [ */
	if (p < end)
	{
		*p++ = '[';
	}

	/* 时间戳 */
	len = format_timestamp(rec->timestamp_ns, tmp, sizeof(tmp));
	if (len > 0)
	{
		int copy = (p + len <= end) ? len : (int) (end - p);
		if (copy > 0)
		{
			memcpy(p, tmp, copy);
			p += copy;
		}
	}

	/* 双空格 + 级别 */
	if (p + 2 <= end)
	{
		*p++ = ' ';
		*p++ = ' ';
	}
	const char *lvl = log_level_to_str((xlog_level) rec->level);
	while (*lvl && p < end) *p++ = *lvl++;

	/* 双空格 + T:线程ID */
	if (p + 4 <= end)
	{
		*p++ = ' ';
		*p++ = ' ';
		*p++ = 'T';
		*p++ = ':';
	}
	len = snprintf(tmp, sizeof(tmp), "%u", rec->thread_id);
	if (len > 0)
	{
		int copy = (p + len <= end) ? len : (int) (end - p);
		if (copy > 0)
		{
			memcpy(p, tmp, copy);
			p += copy;
		}
	}

	/* 模块#标签（可选）*/
	bool has_mod = (rec->ctx.flags & LOG_CTX_HAS_MODULE) && rec->ctx.module;
	bool has_tag = (rec->ctx.flags & LOG_CTX_HAS_TAG) && rec->ctx.tag;
	if (has_mod || has_tag)
	{
		if (p + 2 <= end)
		{
			*p++ = ' ';
			*p++ = ' ';
		}
		if (has_mod)
		{
			const char *s = rec->ctx.module;
			while (*s && p < end) *p++ = *s++;
		}
		if (has_tag)
		{
			if (p < end)
			{
				*p++ = '#';
			}
			const char *s = rec->ctx.tag;
			while (*s && p < end) *p++ = *s++;
		}
	}

	/* trace:xxx（可选）*/
	if (rec->ctx.flags & LOG_CTX_HAS_TRACE_ID)
	{
		if (p + 2 <= end)
		{
			*p++ = ' ';
			*p++ = ' ';
		}
		len = snprintf(tmp, sizeof(tmp), "trace:%016" PRIx64, rec->ctx.trace_id);
		if (len > 0)
		{
			int copy = (p + len <= end) ? len : (int) (end - p);
			if (copy > 0)
			{
				memcpy(p, tmp, copy);
				p += copy;
			}
		}
	}

	/* 文件:行（可选）*/
	if (rec->loc.file)
	{
		if (p + 2 <= end)
		{
			*p++ = ' ';
			*p++ = ' ';
		}
		const char *filename = rec->loc.file;
		const char *slash = strrchr(filename, '/');
		if (slash)
		{
			filename = slash + 1;
		}
		while (*filename && p < end) *p++ = *filename++;
		if (p < end)
		{
			*p++ = ':';
		}
		len = snprintf(tmp, sizeof(tmp), "%u", rec->loc.line);
		for (int j = 0; j < len && p < end; j++)
			*p++ = tmp[j];
	}

	/* 结束 ] + 空格 */
	if (p < end)
	{
		*p++ = ']';
	}
	if (p < end)
	{
		*p++ = ' ';
	}

	return (int) (p - start);
}

int log_record_format_pattern(const log_record *rec,
                              const log_fmt_pattern *pattern,
                              char *output, size_t out_size)
{
	if (!rec || !pattern || !output || out_size == 0)
	{
		return -1;
	}

	/* At least 1 byte for '\0' */
	if (out_size == 1)
	{
		output[0] = '\0';
		return 0;
	}

	char *p = output;
	char *end = output + out_size - 1;  /* Reserve space for '\0' */
	int written;
	char tmp_buf[64];
	int i, j, copy;

	/* Declare all variables used in switch-case here for MSVC C mode compatibility */
	const char *s;
	const char *lvl;
	const char *filename;
	const char *slash;
	const char *f;
	int has_mod, has_tag;

	/* Iterate pre-compiled step array */
	for (i = 0; i < pattern->step_count && p < end; i++)
	{
		const log_fmt_step *step = &pattern->steps[i];

		switch (step->type)
		{
			case LOG_STEP_END:
				goto done;

			case LOG_STEP_LITERAL:
				if (step->literal)
				{
					s = step->literal;
					while (*s && p < end)
					{
						*p++ = *s++;
					}
				}
				break;

			case LOG_STEP_TIMESTAMP:
				written = format_timestamp(rec->timestamp_ns, tmp_buf, sizeof(tmp_buf));
				if (written > 0)
				{
					copy = (p + written <= end) ? written : (int) (end - p);
					if (copy > 0)
					{
						memcpy(p, tmp_buf, (size_t)copy);
						p += copy;
					}
				}
				break;

			case LOG_STEP_LEVEL:
				lvl = log_level_to_str((xlog_level) rec->level);
				while (*lvl && p < end)
				{
					*p++ = *lvl++;
				}
				break;

			case LOG_STEP_MODULE:
				if ((rec->ctx.flags & LOG_CTX_HAS_MODULE) && rec->ctx.module)
				{
					if (p < end) *p++ = '[';
					s = rec->ctx.module;
					while (*s && p < end) *p++ = *s++;
					if (p < end) *p++ = ']';
					if (p < end) *p++ = ' ';
				}
				break;

			case LOG_STEP_COMPONENT:
				if ((rec->ctx.flags & LOG_CTX_HAS_COMPONENT) && rec->ctx.component)
				{
					if (p < end) *p++ = '[';
					s = rec->ctx.component;
					while (*s && p < end) *p++ = *s++;
					if (p < end) *p++ = ']';
					if (p < end) *p++ = ' ';
				}
				break;

			case LOG_STEP_TAG:
				if ((rec->ctx.flags & LOG_CTX_HAS_TAG) && rec->ctx.tag)
				{
					if (p < end) *p++ = '[';
					if (p < end) *p++ = '#';
					s = rec->ctx.tag;
					while (*s && p < end) *p++ = *s++;
					if (p < end) *p++ = ']';
					if (p < end) *p++ = ' ';
				}
				break;

			case LOG_STEP_MODULE_TAG:
				/* Combined output: module#tag or module or #tag */
				has_mod = (rec->ctx.flags & LOG_CTX_HAS_MODULE) && rec->ctx.module;
				has_tag = (rec->ctx.flags & LOG_CTX_HAS_TAG) && rec->ctx.tag;
				if (has_mod || has_tag)
				{
					if (has_mod)
					{
						s = rec->ctx.module;
						while (*s && p < end) *p++ = *s++;
					}
					if (has_tag)
					{
						if (p < end) *p++ = '#';
						s = rec->ctx.tag;
						while (*s && p < end) *p++ = *s++;
					}
				}
				break;

			case LOG_STEP_THREAD_ID:
				written = snprintf(tmp_buf, sizeof(tmp_buf), "%u", rec->thread_id);
				if (written > 0)
				{
					copy = (p + written <= end) ? written : (int) (end - p);
					if (copy > 0)
					{
						memcpy(p, tmp_buf, (size_t)copy);
						p += copy;
					}
				}
				break;

			case LOG_STEP_TRACE_ID:
				if (rec->ctx.flags & LOG_CTX_HAS_TRACE_ID)
				{
					written = snprintf(tmp_buf, sizeof(tmp_buf),
					                   "[trace:%016" PRIx64 "] ", rec->ctx.trace_id);
					if (written > 0)
					{
						copy = (p + written <= end) ? written : (int) (end - p);
						if (copy > 0)
						{
							memcpy(p, tmp_buf, (size_t)copy);
							p += copy;
						}
					}
				}
				break;

			case LOG_STEP_SPAN_ID:
				if (rec->ctx.flags & LOG_CTX_HAS_SPAN_ID)
				{
					written = snprintf(tmp_buf, sizeof(tmp_buf),
					                   "[span:%016" PRIx64 "] ", rec->ctx.span_id);
					if (written > 0)
					{
						copy = (p + written <= end) ? written : (int) (end - p);
						if (copy > 0)
						{
							memcpy(p, tmp_buf, (size_t)copy);
							p += copy;
						}
					}
				}
				break;

			case LOG_STEP_FILE:
				if (rec->loc.file)
				{
					filename = rec->loc.file;
					slash = strrchr(rec->loc.file, '/');
					if (slash) filename = slash + 1;
					if (p < end) *p++ = '[';
					while (*filename && p < end) *p++ = *filename++;
					if (p < end) *p++ = ']';
					if (p < end) *p++ = ' ';
				}
				break;

			case LOG_STEP_LINE:
				written = snprintf(tmp_buf, sizeof(tmp_buf), "[%u] ", rec->loc.line);
				if (written > 0)
				{
					copy = (p + written <= end) ? written : (int) (end - p);
					if (copy > 0)
					{
						memcpy(p, tmp_buf, (size_t)copy);
						p += copy;
					}
				}
				break;

			case LOG_STEP_FUNC:
				if (rec->loc.func)
				{
					if (p < end) *p++ = '[';
					s = rec->loc.func;
					while (*s && p < end) *p++ = *s++;
					if (p < end) *p++ = ']';
					if (p < end) *p++ = ' ';
				}
				break;

			case LOG_STEP_LOCATION:
				/* [file:line@func] */
				if (rec->loc.file)
				{
					filename = rec->loc.file;
					slash = strrchr(rec->loc.file, '/');
					if (slash) filename = slash + 1;

					if (p < end) *p++ = '[';
					while (*filename && p < end) *p++ = *filename++;
					if (p < end) *p++ = ':';
					written = snprintf(tmp_buf, sizeof(tmp_buf), "%u", rec->loc.line);
					for (j = 0; j < written && p < end; j++) *p++ = tmp_buf[j];
					if (rec->loc.func)
					{
						if (p < end) *p++ = '@';
						f = rec->loc.func;
						while (*f && p < end) *p++ = *f++;
					}
					if (p < end) *p++ = ']';
					if (p < end) *p++ = ' ';
				}
				break;

			case LOG_STEP_FILE_LINE:
				/* file:line (no brackets, for unified format) */
				if (rec->loc.file)
				{
					filename = rec->loc.file;
					slash = strrchr(rec->loc.file, '/');
					if (slash) filename = slash + 1;

					while (*filename && p < end) *p++ = *filename++;
					if (p < end) *p++ = ':';
					written = snprintf(tmp_buf, sizeof(tmp_buf), "%u", rec->loc.line);
					for (j = 0; j < written && p < end; j++) *p++ = tmp_buf[j];
				}
				break;

			case LOG_STEP_MESSAGE:
				if (p < end)
				{
					int msg_len = format_message_content(rec, p, end);
					p += msg_len;
				}
				break;

			case LOG_STEP_FIELDS:
				if (rec->field_count > 0)
				{
					if (p < end)
					{
						*p++ = ' ';
					}
					if (p < end)
					{
						*p++ = '{';
					}
					for (int fi = 0; fi < rec->field_count && p < end; fi++)
					{
						char field_buf[128];
						int flen = log_field_to_string(&rec->custom_fields[fi],
						                               field_buf, sizeof(field_buf));
						if (flen > 0)
						{
							if (fi > 0)
							{
								if (p < end)
								{
									*p++ = ',';
								}
								if (p < end)
								{
									*p++ = ' ';
								}
							}
							for (int j = 0; j < flen && p < end; j++)
							{
								*p++ = field_buf[j];
							}
						}
					}
					if (p < end)
					{
						*p++ = '}';
					}
				}
				break;

			case LOG_STEP_NEWLINE:
				if (p < end)
				{
					*p++ = '\n';
				}
				break;

			case LOG_STEP_META_BLOCK:
				/* 统一元信息块: [时间  级别  T:线程  模块#标签  trace:xxx  文件:行] */
				if (p < end)
				{
					int meta_len = format_meta_block(rec, p, end);
					p += meta_len;
				}
				break;

			default:
				break;
		}
	}

done:
	*p = '\0';  /* p 始终 <= end，所以这是安全的 */
	return (int) (p - output);
}

/* ============================================================================
 * Format using global default pattern
 * ============================================================================ */
int log_record_format(const log_record *rec, char *output, size_t out_size)
{
	ENSURE_PATTERN_INIT();
	return log_record_format_pattern(rec, GET_CURRENT_PATTERN(), output, out_size);
}

/* ============================================================================
 * 带颜色的格式化函数（用于终端输出）
 * ============================================================================ */
#include "color.h"

int log_record_format_colored(const log_record *rec, char *output, size_t out_size, bool use_color)
{
	if (!rec || !output || out_size == 0)
	{
		return -1;
	}

	if (out_size == 1)
	{
		output[0] = '\0';
		return 0;
	}

	/* If not using color, call normal format */
	if (!use_color)
	{
		return log_record_format(rec, output, out_size);
	}

	char *p = output;
	char *end = output + out_size - 1;
	char tmp_buf[64];
	int written, copy, j;

	/* Declare all variables used in blocks for MSVC C mode compatibility */
	const char *level_color;
	const char *reset_color;
	const char *c;
	const char *r;
	const char *lvl;
	const char *s;
	const char *filename;
	const char *slash;
	const char *fmt;
	const char *str;
	int arg_idx;
	char arg_buf[256];
	int has_mod, has_tag;
	int n, actual_len, avail, copy_len;
	log_arg_type type;
	uint64_t value;

	/* Get level color */
	level_color = xlog_color_for_level((xlog_level) rec->level);
	reset_color = xlog_color_reset();

	/* Start line color */
	if (level_color && level_color[0] != '\0')
	{
		c = level_color;
		while (*c && p < end) *p++ = *c++;
	}

	/* Start [ */
	if (p < end)
	{
		*p++ = '[';
	}

	/* Timestamp */
	written = format_timestamp(rec->timestamp_ns, tmp_buf, sizeof(tmp_buf));
	if (written > 0)
	{
		copy = (p + written <= end) ? written : (int) (end - p);
		if (copy > 0)
		{
			memcpy(p, tmp_buf, (size_t)copy);
			p += copy;
		}
	}

	/* Double space */
	if (p + 2 <= end)
	{
		*p++ = ' ';
		*p++ = ' ';
	}

	/* Level name */
	{
		static const char *const level_names[] = {"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};
		lvl = (rec->level <= XLOG_LEVEL_FATAL) ? level_names[rec->level] : "?????";
	}
	while (*lvl && p < end)
		*p++ = *lvl++;

	/* Double space + T:thread ID */
	if (p + 4 <= end)
	{
		*p++ = ' ';
		*p++ = ' ';
		*p++ = 'T';
		*p++ = ':';
	}
	written = snprintf(tmp_buf, sizeof(tmp_buf), "%u", rec->thread_id);
	if (written > 0)
	{
		copy = (p + written <= end) ? written : (int) (end - p);
		if (copy > 0)
		{
			memcpy(p, tmp_buf, (size_t)copy);
			p += copy;
		}
	}

	/* module#tag (optional) */
	has_mod = (rec->ctx.flags & LOG_CTX_HAS_MODULE) && rec->ctx.module;
	has_tag = (rec->ctx.flags & LOG_CTX_HAS_TAG) && rec->ctx.tag;
	if (has_mod || has_tag)
	{
		if (p + 2 <= end)
		{
			*p++ = ' ';
			*p++ = ' ';
		}
		if (has_mod)
		{
			s = rec->ctx.module;
			while (*s && p < end) *p++ = *s++;
		}
		if (has_tag)
		{
			if (p < end) *p++ = '#';
		 s = rec->ctx.tag;
		 while (*s && p < end) *p++ = *s++;
		}
	}

	/* trace:xxx (optional) */
	if (rec->ctx.flags & LOG_CTX_HAS_TRACE_ID)
	{
		if (p + 2 <= end)
		{
			*p++ = ' ';
			*p++ = ' ';
		}
		written = snprintf(tmp_buf, sizeof(tmp_buf), "trace:%016" PRIx64, rec->ctx.trace_id);
		if (written > 0)
		{
			copy = (p + written <= end) ? written : (int) (end - p);
			if (copy > 0)
			{
				memcpy(p, tmp_buf, (size_t)copy);
				p += copy;
			}
		}
	}

	/* file:line (optional) */
	if (rec->loc.file)
	{
		if (p + 2 <= end)
		{
			*p++ = ' ';
			*p++ = ' ';
		}
		filename = rec->loc.file;
		slash = strrchr(filename, '/');
		if (slash) filename = slash + 1;
		while (*filename && p < end) *p++ = *filename++;
		if (p < end) *p++ = ':';
		written = snprintf(tmp_buf, sizeof(tmp_buf), "%u", rec->loc.line);
		for (j = 0; j < written && p < end; j++) *p++ = tmp_buf[j];
	}

	/* End ] */
	if (p < end) *p++ = ']';

	/* Color reset (before message, only meta info has color) */
	if (level_color && level_color[0] != '\0' && reset_color && reset_color[0] != '\0')
	{
		r = reset_color;
		while (*r && p < end) *p++ = *r++;
	}

	/* Space */
	if (p < end) *p++ = ' ';

	/* Message content (no color) */
	if (rec->fmt)
	{
		fmt = rec->fmt;
		arg_idx = 0;

		while (*fmt && p < end)
		{
			if (*fmt == '%')
			{
				const char *spec_start = fmt + 1; /* Position after '%' for float precision */
				fmt++;
				if (*fmt == '%')
				{
					if (p < end) *p++ = '%';
					fmt++;
				}
				else if (*fmt == '\0')
				{
					break;
				}
				else
				{
					/* Skip format modifiers but remember start for float precision */
					while (*fmt && (*fmt == '-' || *fmt == '+' || *fmt == ' ' ||
					                *fmt == '#' || *fmt == '0' || (*fmt >= '1' && *fmt <= '9') ||
					                *fmt == '.' || *fmt == 'l' || *fmt == 'h' || *fmt == 'z' ||
					                *fmt == 'j' || *fmt == 't' || *fmt == 'L'))
					{
						fmt++;
					}
					if (arg_idx < rec->arg_count)
					{
						type = (log_arg_type) rec->arg_types[arg_idx];
						value = rec->arg_values[arg_idx];
						if (type == LOG_ARG_STR_STATIC || type == LOG_ARG_STR_INLINE || type == LOG_ARG_STR_EXTERN)
						{
							str = (const char *) (uintptr_t) value;
							if (str == NULL) str = "(null)";
							while (*str && p < end) *p++ = *str++;
						}
						/* Float types: use original format spec to preserve precision */
						else if (type == LOG_ARG_F32 || type == LOG_ARG_F64)
						{
							double d = (type == LOG_ARG_F32) ?
							           (double)log_u64_to_f32(value) : log_u64_to_f64(value);
							n = format_float_with_spec(d, spec_start, fmt + 1, arg_buf, sizeof(arg_buf));
							if (n > 0 && p < end)
							{
								actual_len = (n < (int) sizeof(arg_buf)) ? n : (int) sizeof(arg_buf) - 1;
								avail = (int) (end - p);
								copy_len = (actual_len < avail) ? actual_len : avail;
								if (copy_len > 0)
								{
									memcpy(p, arg_buf, (size_t) copy_len);
									p += copy_len;
								}
							}
						}
						else
						{
							n = log_arg_to_string(type, value, arg_buf, sizeof(arg_buf));
							if (n > 0 && p < end)
							{
								actual_len = (n < (int) sizeof(arg_buf)) ? n : (int) sizeof(arg_buf) - 1;
								avail = (int) (end - p);
								copy_len = (actual_len < avail) ? actual_len : avail;
								if (copy_len > 0)
								{
									memcpy(p, arg_buf, (size_t) copy_len);
									p += copy_len;
								}
							}
						}
						arg_idx++;
					}
					if (*fmt) fmt++;
				}
			}
			else if (*fmt == '{' && *(fmt + 1) == '}')
			{
				if (arg_idx < rec->arg_count)
				{
					type = (log_arg_type) rec->arg_types[arg_idx];
					value = rec->arg_values[arg_idx];
					if (type == LOG_ARG_STR_STATIC || type == LOG_ARG_STR_INLINE || type == LOG_ARG_STR_EXTERN)
					{
						str = (const char *) (uintptr_t) value;
						if (str == NULL) str = "(null)";
						while (*str && p < end) *p++ = *str++;
					}
					else
					{
						n = log_arg_to_string(type, value, arg_buf, sizeof(arg_buf));
						if (n > 0 && p < end)
						{
							actual_len = (n < (int) sizeof(arg_buf)) ? n : (int) sizeof(arg_buf) - 1;
							avail = (int) (end - p);
							copy_len = (actual_len < avail) ? actual_len : avail;
							if (copy_len > 0)
							{
								memcpy(p, arg_buf, (size_t) copy_len);
								p += copy_len;
							}
						}
					}
					arg_idx++;
				}
				fmt += 2;
			}
			else
			{
				if (p < end) *p++ = *fmt;
				fmt++;
			}
		}
	}


	/* Newline */
	if (p < end) *p++ = '\n';

	*p = '\0';
	return (int) (p - output);
}

#define FAST_COPY_LITERAL(p, end, str) do \
{ \
    static const char _lit[] = str; \
    const int _len = sizeof(_lit) - 1; \
    if (p + _len <= end) { \
        memcpy(p, _lit, _len); \
        p += _len; \
    } \
} while(0)

#define FAST_COPY_STR(p, end, str) do \
{ \
    const char *_s = (str); \
    while (*_s && p < end) *p++ = *_s++; \
} while(0)

int log_record_format_default_inline(const log_record *rec, char *output, size_t out_size)
{
	if (!rec || !output || out_size == 0)
	{
		return -1;
	}
	if (out_size == 1)
	{
		output[0] = '\0';
		return 0;
	}

	char *p = output;
	char *end = output + out_size - 1;
	char tmp[64];
	int len;

	/* 开始 [ */
	if (p < end)
	{
		*p++ = '[';
	}

	/* 时间戳 */
	len = format_timestamp(rec->timestamp_ns, tmp, sizeof(tmp));
	if (len > 0 && p + len <= end)
	{
		memcpy(p, tmp, len);
		p += len;
	}

	/* 双空格 + 级别 */
	FAST_COPY_LITERAL(p, end, "  ");
	FAST_COPY_STR(p, end, log_level_to_str((xlog_level) rec->level));

	/* 双空格 + T:线程ID */
	FAST_COPY_LITERAL(p, end, "  T:");
	len = snprintf(tmp, sizeof(tmp), "%u", rec->thread_id);
	if (len > 0 && p + len <= end)
	{
		memcpy(p, tmp, len);
		p += len;
	}

	/* 模块#标签 - 可选 */
	{
		bool has_mod = (rec->ctx.flags & LOG_CTX_HAS_MODULE) && rec->ctx.module;
		bool has_tag = (rec->ctx.flags & LOG_CTX_HAS_TAG) && rec->ctx.tag;
		if (has_mod || has_tag)
		{
			FAST_COPY_LITERAL(p, end, "  ");
			if (has_mod) FAST_COPY_STR(p, end, rec->ctx.module);
			if (has_tag)
			{
				if (p < end)
				{
					*p++ = '#';
				}
				FAST_COPY_STR(p, end, rec->ctx.tag);
			}
		}
	}

	/* trace:xxx - 可选 */
	if (rec->ctx.flags & LOG_CTX_HAS_TRACE_ID)
	{
		FAST_COPY_LITERAL(p, end, "  trace:");
		len = snprintf(tmp, sizeof(tmp), "%016" PRIx64, rec->ctx.trace_id);
		if (len > 0 && p + len <= end)
		{
			memcpy(p, tmp, len);
			p += len;
		}
	}

	/* 文件:行 */
	if (rec->loc.file)
	{
		const char *fname = rec->loc.file;
		const char *slash = strrchr(fname, '/');
		if (slash)
		{
			fname = slash + 1;
		}

		FAST_COPY_LITERAL(p, end, "  ");
		FAST_COPY_STR(p, end, fname);
		if (p < end)
		{
			*p++ = ':';
		}
		len = snprintf(tmp, sizeof(tmp), "%u", rec->loc.line);
		if (len > 0 && p + len <= end)
		{
			memcpy(p, tmp, len);
			p += len;
		}
	}

	/* 结束 ] + 空格 */
	FAST_COPY_LITERAL(p, end, "] ");

	/* 消息 */
	if (p < end)
	{
		len = format_message_content(rec, p, end);
		p += len;
	}

	/* {自定义字段} - 可选 */
	if (rec->field_count > 0)
	{
		FAST_COPY_LITERAL(p, end, " {");
		for (int i = 0; i < rec->field_count && p < end; i++)
		{
			if (i > 0) FAST_COPY_LITERAL(p, end, ", ");
			char fbuf[128];
			int flen = log_field_to_string(&rec->custom_fields[i], fbuf, sizeof(fbuf));
			if (flen > 0 && p + flen <= end)
			{
				memcpy(p, fbuf, flen);
				p += flen;
			}
		}
		if (p < end)
		{
			*p++ = '}';
		}
	}

	/* 换行 */
	if (p < end)
	{
		*p++ = '\n';
	}
	*p = '\0';
	return (int) (p - output);
}

/**
 * 简洁格式（内联版本）- 最高性能
 * 格式: [时间  级别] 消息
 */
int log_record_format_simple_inline(const log_record *rec, char *output, size_t out_size)
{
	if (!rec || !output || out_size == 0)
	{
		return -1;
	}
	if (out_size == 1)
	{
		output[0] = '\0';
		return 0;
	}

	char *p = output;
	char *end = output + out_size - 1;
	char tmp[64];
	int len;

	/* 开始 [ */
	if (p < end)
	{
		*p++ = '[';
	}

	/* 时间戳 */
	len = format_timestamp(rec->timestamp_ns, tmp, sizeof(tmp));
	if (len > 0 && p + len <= end)
	{
		memcpy(p, tmp, len);
		p += len;
	}

	/* 双空格 + 级别 */
	FAST_COPY_LITERAL(p, end, "  ");
	FAST_COPY_STR(p, end, log_level_to_str((xlog_level) rec->level));

	/* 结束 ] + 空格 */
	FAST_COPY_LITERAL(p, end, "] ");

	/* 消息 */
	if (p < end)
	{
		len = format_message_content(rec, p, end);
		p += len;
	}

	/* 换行 */
	if (p < end)
	{
		*p++ = '\n';
	}
	*p = '\0';
	return (int) (p - output);
}

/**
 * 生产格式（内联版本）- 无位置信息
 * 格式: [时间  级别  T:线程  模块#标签] 消息 {字段}
 */
int log_record_format_prod_inline(const log_record *rec, char *output, size_t out_size)
{
	if (!rec || !output || out_size == 0)
	{
		return -1;
	}
	if (out_size == 1)
	{
		output[0] = '\0';
		return 0;
	}

	char *p = output;
	char *end = output + out_size - 1;
	char tmp[64];
	int len;

	/* 开始 [ */
	if (p < end)
	{
		*p++ = '[';
	}

	/* 时间戳 */
	len = format_timestamp(rec->timestamp_ns, tmp, sizeof(tmp));
	if (len > 0 && p + len <= end)
	{
		memcpy(p, tmp, len);
		p += len;
	}

	/* 双空格 + 级别 */
	FAST_COPY_LITERAL(p, end, "  ");
	FAST_COPY_STR(p, end, log_level_to_str((xlog_level) rec->level));

	/* 双空格 + T:线程ID */
	FAST_COPY_LITERAL(p, end, "  T:");
	len = snprintf(tmp, sizeof(tmp), "%u", rec->thread_id);
	if (len > 0 && p + len <= end)
	{
		memcpy(p, tmp, len);
		p += len;
	}

	/* 模块#标签 - 可选 */
	{
		bool has_mod = (rec->ctx.flags & LOG_CTX_HAS_MODULE) && rec->ctx.module;
		bool has_tag = (rec->ctx.flags & LOG_CTX_HAS_TAG) && rec->ctx.tag;
		if (has_mod || has_tag)
		{
			FAST_COPY_LITERAL(p, end, "  ");
			if (has_mod) FAST_COPY_STR(p, end, rec->ctx.module);
			if (has_tag)
			{
				if (p < end)
				{
					*p++ = '#';
				}
				FAST_COPY_STR(p, end, rec->ctx.tag);
			}
		}
	}

	/* 结束 ] + 空格 */
	FAST_COPY_LITERAL(p, end, "] ");

	/* 消息 */
	if (p < end)
	{
		len = format_message_content(rec, p, end);
		p += len;
	}

	/* {自定义字段} - 可选 */
	if (rec->field_count > 0)
	{
		FAST_COPY_LITERAL(p, end, " {");
		for (int i = 0; i < rec->field_count && p < end; i++)
		{
			if (i > 0) FAST_COPY_LITERAL(p, end, ", ");
			char fbuf[128];
			int flen = log_field_to_string(&rec->custom_fields[i], fbuf, sizeof(fbuf));
			if (flen > 0 && p + flen <= end)
			{
				memcpy(p, fbuf, flen);
				p += flen;
			}
		}
		if (p < end)
		{
			*p++ = '}';
		}
	}

	/* 换行 */
	if (p < end)
	{
		*p++ = '\n';
	}
	*p = '\0';
	return (int) (p - output);
}

/* ============================================================================
 * 便捷格式化函数（用于调试）
 * ============================================================================ */
void log_record_dump(const log_record *rec, FILE *fp)
{
	if (!rec || !fp)
	{
		return;
	}

	fprintf(fp, "=== Log Record ===\n");
	fprintf(fp, "ready: %s\n", atomic_load(&rec->ready) ? "true" : "false");
	fprintf(fp, "level: %s (%d)\n", log_level_to_str((xlog_level) rec->level), rec->level);
	fprintf(fp, "thread_id: %u\n", rec->thread_id);
	fprintf(fp, "timestamp_ns: %" PRIu64 "\n", rec->timestamp_ns);
	fprintf(fp, "fmt: %s\n", rec->fmt ? rec->fmt : "(null)");
	fprintf(fp, "location: %s:%u@%s\n",
	        rec->loc.file ? rec->loc.file : "(null)",
	        rec->loc.line,
	        rec->loc.func ? rec->loc.func : "(null)");

	/* 上下文信息 */
	fprintf(fp, "context:\n");
	fprintf(fp, "  flags: 0x%08x\n", rec->ctx.flags);
	if (rec->ctx.flags & LOG_CTX_HAS_MODULE)
	{
		fprintf(fp, "  module: %s\n", rec->ctx.module ? rec->ctx.module : "(null)");
	}
	if (rec->ctx.flags & LOG_CTX_HAS_COMPONENT)
	{
		fprintf(fp, "  component: %s\n", rec->ctx.component ? rec->ctx.component : "(null)");
	}
	if (rec->ctx.flags & LOG_CTX_HAS_TAG)
	{
		fprintf(fp, "  tag: %s\n", rec->ctx.tag ? rec->ctx.tag : "(null)");
	}
	if (rec->ctx.flags & LOG_CTX_HAS_TRACE_ID)
	{
		fprintf(fp, "  trace_id: %016" PRIx64 "\n", rec->ctx.trace_id);
	}
	if (rec->ctx.flags & LOG_CTX_HAS_SPAN_ID)
	{
		fprintf(fp, "  span_id: %016" PRIx64 "\n", rec->ctx.span_id);
	}

	/* 参数 */
	fprintf(fp, "arg_count: %d\n", rec->arg_count);
	for (int i = 0; i < rec->arg_count; i++)
	{
		char buf[128];
		log_arg_to_string((log_arg_type) rec->arg_types[i], rec->arg_values[i], buf, sizeof(buf));
		fprintf(fp, "  arg[%d]: type=%02x, value=%" PRIu64 " => %s\n",
		        i, rec->arg_types[i], rec->arg_values[i], buf);
	}

	/* 自定义字段 */
	fprintf(fp, "field_count: %d\n", rec->field_count);
	for (int i = 0; i < rec->field_count; i++)
	{
		char buf[128];
		log_field_to_string(&rec->custom_fields[i], buf, sizeof(buf));
		fprintf(fp, "  field[%d]: type=%02x (%s), key=%s => %s\n",
		        i, rec->custom_fields[i].type,
		        log_field_type_name(rec->custom_fields[i].type),
		        rec->custom_fields[i].key ? rec->custom_fields[i].key : "(none)",
		        buf);
	}

	fprintf(fp, "inline_buf_used: %u\n", rec->inline_buf_used);
	fprintf(fp, "==================\n");
}

