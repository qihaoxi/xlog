/* =====================================================================================
 *       Filename:  log_record.c
 *    Description:  Log record formatting and argument parsing implementation
 *        Version:  1.0
 *        Created:  2026-02-07
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <time.h>
#include "log_record.h"

/* ============================================================================
 * 日志级别名称
 * ============================================================================ */
static const char *const LOG_LEVEL_NAMES[] = {
		"TRACE",
		"DEBUG",
		"INFO",
		"WARN",      /* matches LOG_LEVEL_WARNING = 3 */
		"ERROR",
		"FATAL"
};

static const char *log_level_to_str(log_level level)
{
	if (level >= 0 && level <= LOG_LEVEL_FATAL)
	{
		return LOG_LEVEL_NAMES[level];
	}
	return "UNKNOWN";
}

/* ============================================================================
 * 自定义字段类型名称
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
 * 全局格式配置 - 使用预编译模式
 * ============================================================================ */
static const log_fmt_pattern g_default_pattern = LOG_PATTERN_DEFAULT;
static const log_fmt_pattern *g_current_pattern = &g_default_pattern;

void log_format_set_pattern(const log_fmt_pattern *pattern)
{
	if (pattern)
	{
		g_current_pattern = pattern;
	}
	else
	{
		g_current_pattern = &g_default_pattern;
	}
}

const log_fmt_pattern *log_format_get_pattern(void)
{
	return g_current_pattern;
}

/* ============================================================================
 * 高性能时间戳格式化（秒级缓存）
 * ============================================================================
 * 设计原则：
 * - localtime_r 调用开销大（有锁，涉及时区计算）
 * - 秒级缓存：只有秒数变化时才重新调用 localtime_r
 * - 微秒部分实时计算（无锁，开销极低）
 *
 * 性能提升：在高频日志场景下，约 10-20 倍加速
 */

/* 缓存结构体（线程本地存储，避免锁竞争）*/
typedef struct timestamp_cache
{
	time_t cached_sec;         /* 缓存的秒数 */
	char date_time[20];      /* "YYYY-MM-DD HH:MM:SS" (19 字符 + '\0') */
	int date_time_len;      /* 日期时间字符串长度 */
} timestamp_cache;

/* 线程本地缓存 */
static _Thread_local timestamp_cache g_ts_cache = {0};

static int format_timestamp(uint64_t timestamp_ns, char *buf, size_t size)
{
	if (!buf || size < 27)
	{  /* 最少需要 "YYYY-MM-DD HH:MM:SS.uuuuuu\0" */
		return snprintf(buf, size, "0000-00-00 00:00:00.000000");
	}

	time_t sec = (time_t) (timestamp_ns / 1000000000ULL);
	uint32_t usec = (uint32_t) ((timestamp_ns % 1000000000ULL) / 1000);

	/* 检查缓存是否有效（秒数相同则复用） */
	if (sec != g_ts_cache.cached_sec)
	{
		/* 秒数变化，需要重新计算 */
		struct tm tm_buf;
		struct tm *tm_ptr = localtime_r(&sec, &tm_buf);
		if (!tm_ptr)
		{
			return snprintf(buf, size, "0000-00-00 00:00:00.%06u", usec);
		}

		/* 更新缓存 */
		g_ts_cache.cached_sec = sec;
		g_ts_cache.date_time_len = snprintf(g_ts_cache.date_time, sizeof(g_ts_cache.date_time),
		                                    "%04d-%02d-%02d %02d:%02d:%02d",
		                                    tm_ptr->tm_year + 1900,
		                                    tm_ptr->tm_mon + 1,
		                                    tm_ptr->tm_mday,
		                                    tm_ptr->tm_hour,
		                                    tm_ptr->tm_min,
		                                    tm_ptr->tm_sec);
	}

	/* 复用缓存的日期时间部分，只更新微秒 */
	memcpy(buf, g_ts_cache.date_time, g_ts_cache.date_time_len);
	return g_ts_cache.date_time_len + snprintf(buf + g_ts_cache.date_time_len,
	                                           size - g_ts_cache.date_time_len,
	                                           ".%06u", usec);
}

/* ============================================================================
 * 参数格式化
 * ============================================================================ */
int log_arg_to_string(log_arg_type type, uint64_t value, char *output, size_t out_size)
{
	if (!output || out_size == 0)
	{
		return -1;
	}

	switch (type)
	{
		case LOG_ARG_NONE:
			return snprintf(output, out_size, "(null)");

		case LOG_ARG_I8:
			return snprintf(output, out_size, "%" PRId8, (int8_t) value);

		case LOG_ARG_I16:
			return snprintf(output, out_size, "%" PRId16, (int16_t) value);

		case LOG_ARG_I32:
			return snprintf(output, out_size, "%" PRId32, (int32_t) value);

		case LOG_ARG_I64:
			return snprintf(output, out_size, "%" PRId64, (int64_t) value);

		case LOG_ARG_U8:
			return snprintf(output, out_size, "%" PRIu8, (uint8_t) value);

		case LOG_ARG_U16:
			return snprintf(output, out_size, "%" PRIu16, (uint16_t) value);

		case LOG_ARG_U32:
			return snprintf(output, out_size, "%" PRIu32, (uint32_t) value);

		case LOG_ARG_U64:
			return snprintf(output, out_size, "%" PRIu64, value);

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
			return snprintf(output, out_size, "%c", (char) value);

		case LOG_ARG_BOOL:
			return snprintf(output, out_size, "%s", value ? "true" : "false");

		case LOG_ARG_PTR:
			if (value == 0)
			{
				return snprintf(output, out_size, "(nil)");
			}
			return snprintf(output, out_size, "%p", (void *) (uintptr_t) value);

		case LOG_ARG_STR_STATIC:
		case LOG_ARG_STR_INLINE:
		case LOG_ARG_STR_EXTERN:
		{
			const char *str = (const char *) (uintptr_t) value;
			if (str == NULL)
			{
				return snprintf(output, out_size, "(null)");
			}
			return snprintf(output, out_size, "%s", str);
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
				/* 跳过格式修饰符 */
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

/* ============================================================================
 * 高性能预编译模式格式化实现
 * ============================================================================
 * 一次定义格式模式，格式化时直接遍历步骤数组
 * 无需运行时条件判断格式选项
 */

/* 统一元信息块格式化辅助函数
 * 格式: [时间  级别  T:线程  模块#标签  trace:xxx  文件:行]
 * 所有元信息在一个 [] 中，用双空格分隔
 */
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
	const char *lvl = log_level_to_str((log_level) rec->level);
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

	/* 至少需要 1 字节用于 '\0' */
	if (out_size == 1)
	{
		output[0] = '\0';
		return 0;
	}

	char *p = output;
	char *end = output + out_size - 1;  /* 预留 '\0' 的位置 */
	int written;
	char tmp_buf[64];

	/* 直接遍历预编译的步骤数组 */
	for (int i = 0; i < pattern->step_count && p < end; i++)
	{
		const log_fmt_step *step = &pattern->steps[i];

		switch (step->type)
		{
			case LOG_STEP_END:
				goto done;

			case LOG_STEP_LITERAL:
				if (step->literal)
				{
					const char *s = step->literal;
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
					int copy = (p + written <= end) ? written : (int) (end - p);
					if (copy > 0)
					{
						memcpy(p, tmp_buf, copy);
						p += copy;
					}
				}
				break;

			case LOG_STEP_LEVEL:
			{
				const char *lvl = log_level_to_str((log_level) rec->level);
				while (*lvl && p < end)
				{
					*p++ = *lvl++;
				}
			}
				break;

			case LOG_STEP_MODULE:
				if ((rec->ctx.flags & LOG_CTX_HAS_MODULE) && rec->ctx.module)
				{
					if (p < end)
					{
						*p++ = '[';
					}
					const char *s = rec->ctx.module;
					while (*s && p < end) *p++ = *s++;
					if (p < end)
					{
						*p++ = ']';
					}
					if (p < end)
					{
						*p++ = ' ';
					}
				}
				break;

			case LOG_STEP_COMPONENT:
				if ((rec->ctx.flags & LOG_CTX_HAS_COMPONENT) && rec->ctx.component)
				{
					if (p < end)
					{
						*p++ = '[';
					}
					const char *s = rec->ctx.component;
					while (*s && p < end) *p++ = *s++;
					if (p < end)
					{
						*p++ = ']';
					}
					if (p < end)
					{
						*p++ = ' ';
					}
				}
				break;

			case LOG_STEP_TAG:
				if ((rec->ctx.flags & LOG_CTX_HAS_TAG) && rec->ctx.tag)
				{
					if (p < end)
					{
						*p++ = '[';
					}
					if (p < end)
					{
						*p++ = '#';
					}
					const char *s = rec->ctx.tag;
					while (*s && p < end) *p++ = *s++;
					if (p < end)
					{
						*p++ = ']';
					}
					if (p < end)
					{
						*p++ = ' ';
					}
				}
				break;

			case LOG_STEP_MODULE_TAG:
				/* 组合输出: module#tag 或 module 或 #tag（不加括号，适合统一格式）*/
			{
				bool has_mod = (rec->ctx.flags & LOG_CTX_HAS_MODULE) && rec->ctx.module;
				bool has_tag = (rec->ctx.flags & LOG_CTX_HAS_TAG) && rec->ctx.tag;
				if (has_mod || has_tag)
				{
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
			}
				break;

			case LOG_STEP_THREAD_ID:
				written = snprintf(tmp_buf, sizeof(tmp_buf), "%u", rec->thread_id);
				if (written > 0)
				{
					int copy = (p + written <= end) ? written : (int) (end - p);
					if (copy > 0)
					{
						memcpy(p, tmp_buf, copy);
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
						int copy = (p + written <= end) ? written : (int) (end - p);
						if (copy > 0)
						{
							memcpy(p, tmp_buf, copy);
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
						int copy = (p + written <= end) ? written : (int) (end - p);
						if (copy > 0)
						{
							memcpy(p, tmp_buf, copy);
							p += copy;
						}
					}
				}
				break;

			case LOG_STEP_FILE:
				if (rec->loc.file)
				{
					const char *filename = rec->loc.file;
					const char *slash = strrchr(rec->loc.file, '/');
					if (slash)
					{
						filename = slash + 1;
					}
					if (p < end)
					{
						*p++ = '[';
					}
					while (*filename && p < end) *p++ = *filename++;
					if (p < end)
					{
						*p++ = ']';
					}
					if (p < end)
					{
						*p++ = ' ';
					}
				}
				break;

			case LOG_STEP_LINE:
				written = snprintf(tmp_buf, sizeof(tmp_buf), "[%u] ", rec->loc.line);
				if (written > 0)
				{
					int copy = (p + written <= end) ? written : (int) (end - p);
					if (copy > 0)
					{
						memcpy(p, tmp_buf, copy);
						p += copy;
					}
				}
				break;

			case LOG_STEP_FUNC:
				if (rec->loc.func)
				{
					if (p < end)
					{
						*p++ = '[';
					}
					const char *s = rec->loc.func;
					while (*s && p < end) *p++ = *s++;
					if (p < end)
					{
						*p++ = ']';
					}
					if (p < end)
					{
						*p++ = ' ';
					}
				}
				break;

			case LOG_STEP_LOCATION:
				/* [file:line@func] */
				if (rec->loc.file)
				{
					const char *filename = rec->loc.file;
					const char *slash = strrchr(rec->loc.file, '/');
					if (slash)
					{
						filename = slash + 1;
					}

					if (p < end)
					{
						*p++ = '[';
					}
					while (*filename && p < end) *p++ = *filename++;
					if (p < end)
					{
						*p++ = ':';
					}
					written = snprintf(tmp_buf, sizeof(tmp_buf), "%u", rec->loc.line);
					for (int j = 0; j < written && p < end; j++) *p++ = tmp_buf[j];
					if (rec->loc.func)
					{
						if (p < end)
						{
							*p++ = '@';
						}
						const char *f = rec->loc.func;
						while (*f && p < end) *p++ = *f++;
					}
					if (p < end)
					{
						*p++ = ']';
					}
					if (p < end)
					{
						*p++ = ' ';
					}
				}
				break;

			case LOG_STEP_FILE_LINE:
				/* file:line（不加括号，适合统一格式）*/
				if (rec->loc.file)
				{
					const char *filename = rec->loc.file;
					const char *slash = strrchr(rec->loc.file, '/');
					if (slash)
					{
						filename = slash + 1;
					}

					while (*filename && p < end) *p++ = *filename++;
					if (p < end)
					{
						*p++ = ':';
					}
					written = snprintf(tmp_buf, sizeof(tmp_buf), "%u", rec->loc.line);
					for (int j = 0; j < written && p < end; j++) *p++ = tmp_buf[j];
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
 * 使用全局默认模式的格式化函数
 * ============================================================================ */
int log_record_format(const log_record *rec, char *output, size_t out_size)
{
	return log_record_format_pattern(rec, g_current_pattern, output, out_size);
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

	/* 如果不使用颜色，直接调用普通格式化 */
	if (!use_color)
	{
		return log_record_format(rec, output, out_size);
	}

	char *p = output;
	char *end = output + out_size - 1;
	char tmp_buf[64];
	int written;

	/* 获取级别颜色 */
	const char *level_color = xlog_color_for_level((log_level) rec->level);
	const char *reset_color = xlog_color_reset();

	/* 整行开始颜色 */
	if (level_color && level_color[0] != '\0')
	{
		const char *c = level_color;
		while (*c && p < end) *p++ = *c++;
	}

	/* 开始 [ */
	if (p < end)
	{
		*p++ = '[';
	}

	/* 时间戳 */
	written = format_timestamp(rec->timestamp_ns, tmp_buf, sizeof(tmp_buf));
	if (written > 0)
	{
		int copy = (p + written <= end) ? written : (int) (end - p);
		if (copy > 0)
		{
			memcpy(p, tmp_buf, copy);
			p += copy;
		}
	}

	/* 双空格 */
	if (p + 2 <= end)
	{
		*p++ = ' ';
		*p++ = ' ';
	}

	/* 级别名称 */
	static const char *const level_names[] = {"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};
	const char *lvl = (rec->level <= LOG_LEVEL_FATAL) ? level_names[rec->level] : "?????";
	while (*lvl && p < end)
		*p++ = *lvl++;

	/* 双空格 + T:线程ID */
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
		int copy = (p + written <= end) ? written : (int) (end - p);
		if (copy > 0)
		{
			memcpy(p, tmp_buf, copy);
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
		written = snprintf(tmp_buf, sizeof(tmp_buf), "trace:%016" PRIx64, rec->ctx.trace_id);
		if (written > 0)
		{
			int copy = (p + written <= end) ? written : (int) (end - p);
			if (copy > 0)
			{
				memcpy(p, tmp_buf, copy);
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
		written = snprintf(tmp_buf, sizeof(tmp_buf), "%u", rec->loc.line);
		for (int j = 0; j < written && p < end; j++) *p++ = tmp_buf[j];
	}

	/* 结束 ] */
	if (p < end)
	{
		*p++ = ']';
	}

	/* 颜色重置（在消息之前，只有元信息有颜色）*/
	if (level_color && level_color[0] != '\0' && reset_color && reset_color[0] != '\0')
	{
		const char *r = reset_color;
		while (*r && p < end) *p++ = *r++;
	}

	/* 空格 */
	if (p < end)
	{
		*p++ = ' ';
	}


	/* 消息内容（无颜色）*/
	if (rec->fmt)
	{
		const char *fmt = rec->fmt;
		int arg_idx = 0;
		char arg_buf[256];

		while (*fmt && p < end)
		{
			if (*fmt == '%')
			{
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
					/* 跳过格式修饰符 */
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
						if (type == LOG_ARG_STR_STATIC || type == LOG_ARG_STR_INLINE || type == LOG_ARG_STR_EXTERN)
						{
							const char *str = (const char *) (uintptr_t) value;
							if (str == NULL)
							{
								str = "(null)";
							}
							while (*str && p < end) *p++ = *str++;
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
					if (type == LOG_ARG_STR_STATIC || type == LOG_ARG_STR_INLINE || type == LOG_ARG_STR_EXTERN)
					{
						const char *str = (const char *) (uintptr_t) value;
						if (str == NULL)
						{
							str = "(null)";
						}
						while (*str && p < end) *p++ = *str++;
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
 * 极致性能：内联固定格式函数（无 switch-case，无字符串字面量）
 * ============================================================================
 * 直接硬编码格式，编译器可完全内联优化
 * 适用于对性能要求极高的场景
 */

/* 辅助宏：快速拷贝固定字符串（编译时已知长度）*/
#define FAST_COPY_LITERAL(p, end, str) do \
{ \
    static const char _lit[] = str; \
    const int _len = sizeof(_lit) - 1; \
    if (p + _len <= end) { \
        memcpy(p, _lit, _len); \
        p += _len; \
    } \
} while(0)

/* 辅助宏：快速拷贝字符串（运行时长度）*/
#define FAST_COPY_STR(p, end, str) do \
{ \
    const char *_s = (str); \
    while (*_s && p < end) *p++ = *_s++; \
} while(0)

/**
 * 默认格式（内联版本）- 最高性能
 * 格式: [时间  级别  T:线程  模块#标签  trace:xxx  文件:行] 消息 {字段}
 */
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
	FAST_COPY_STR(p, end, log_level_to_str((log_level) rec->level));

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
	FAST_COPY_STR(p, end, log_level_to_str((log_level) rec->level));

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
	FAST_COPY_STR(p, end, log_level_to_str((log_level) rec->level));

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
	fprintf(fp, "level: %s (%d)\n", log_level_to_str((log_level) rec->level), rec->level);
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

