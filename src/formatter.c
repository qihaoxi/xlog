/* =====================================================================================
 *       Filename:  formatter.c
 *    Description:  Formatter implementation for xlog - Text, Raw, and JSON formats
 *        Version:  1.0
 *        Created:  2026-02-21
 *       Compiler:  gcc/clang (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include "formatter.h"
#include "simd.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <time.h>

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/* Format message content from log_record (interpolate format string with args) */
static size_t format_message_only(const log_record* rec, char* buf, size_t buf_size)
{
	if (!rec || !buf || buf_size == 0)
	{
		return 0;
	}

	if (!rec->fmt)
	{
		return (size_t)snprintf(buf, buf_size, "(no message)");
	}

	char* p = buf;
	char* end = buf + buf_size - 1;
	const char* fmt = rec->fmt;
	int arg_idx = 0;
	char arg_buf[256];

	while (*fmt && p < end)
	{
		if (*fmt == '%')
		{
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
				/* Skip format modifiers */
				while (*fmt && (*fmt == '-' || *fmt == '+' || *fmt == ' ' ||
				                *fmt == '#' || *fmt == '0' || (*fmt >= '1' && *fmt <= '9') ||
				                *fmt == '.' || *fmt == 'l' || *fmt == 'h' || *fmt == 'z' ||
				                *fmt == 'j' || *fmt == 't' || *fmt == 'L'))
				{
					fmt++;
				}

				if (arg_idx < rec->arg_count)
				{
					log_arg_type type = (log_arg_type)rec->arg_types[arg_idx];
					uint64_t value = rec->arg_values[arg_idx];

					if (type == LOG_ARG_STR_STATIC || type == LOG_ARG_STR_INLINE || type == LOG_ARG_STR_EXTERN)
					{
						const char* str = (const char*)(uintptr_t)value;
						if (!str) str = "(null)";
						while (*str && p < end) *p++ = *str++;
					}
					else
					{
						int n = log_arg_to_string(type, value, arg_buf, sizeof(arg_buf));
						if (n > 0)
						{
							int copy_len = (n < (int)(end - p)) ? n : (int)(end - p);
							if (copy_len > 0)
							{
								memcpy(p, arg_buf, (size_t)copy_len);
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
				log_arg_type type = (log_arg_type)rec->arg_types[arg_idx];
				uint64_t value = rec->arg_values[arg_idx];

				if (type == LOG_ARG_STR_STATIC || type == LOG_ARG_STR_INLINE || type == LOG_ARG_STR_EXTERN)
				{
					const char* str = (const char*)(uintptr_t)value;
					if (!str) str = "(null)";
					while (*str && p < end) *p++ = *str++;
				}
				else
				{
					int n = log_arg_to_string(type, value, arg_buf, sizeof(arg_buf));
					if (n > 0)
					{
						int copy_len = (n < (int)(end - p)) ? n : (int)(end - p);
						if (copy_len > 0)
						{
							memcpy(p, arg_buf, (size_t)copy_len);
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
			*p++ = *fmt++;
		}
	}

	*p = '\0';
	return (size_t)(p - buf);
}

/* ============================================================================
 * Formatter Creation/Destruction
 * ============================================================================ */

xlog_formatter* xlog_formatter_create(xlog_format_type type, format_fn format, void* ctx)
{
	xlog_formatter* f = (xlog_formatter*)malloc(sizeof(xlog_formatter));
	if (!f)
	{
		return NULL;
	}
	f->type = type;
	f->format = format;
	f->ctx = ctx;
	f->append_newline = true;
	f->pretty_print = false;
	return f;
}

void xlog_formatter_destroy(xlog_formatter* f)
{
	if (f)
	{
		free(f);
	}
}

/* Legacy function names for backward compatibility */
xlog_formatter* xlog_formater_create(xlog_format_type type, format_fn format, void* ctx)
{
	return xlog_formatter_create(type, format, ctx);
}

void xlog_formater_destroy(xlog_formatter* f)
{
	xlog_formatter_destroy(f);
}

/* ============================================================================
 * Raw Formatter Implementation
 * ============================================================================
 * Outputs only the message content, no timestamp, level, or other metadata.
 * Ideal for:
 * - High-performance data pipelines where formatting is done externally
 * - Cases where user has already formatted the string
 * - Streaming to systems that add their own metadata
 */

size_t xlog_format_raw(xlog_formatter* formatter, const log_record* rec,
                       char* buf, size_t buf_size)
{
	if (!rec || !buf || buf_size == 0)
	{
		return 0;
	}

	size_t written = format_message_only(rec, buf, buf_size - 1);

	/* Append newline if configured */
	if (formatter && formatter->append_newline && written < buf_size - 1)
	{
		buf[written++] = '\n';
		buf[written] = '\0';
	}

	return written;
}

xlog_formatter* xlog_raw_formatter_create(void)
{
	xlog_formatter* f = xlog_formatter_create(XLOG_FMT_RAW, xlog_format_raw, NULL);
	if (f)
	{
		f->append_newline = true;
	}
	return f;
}

/* ============================================================================
 * Text Formatter Implementation
 * ============================================================================
 * Uses the existing log_record_format function with global pattern.
 */

size_t xlog_format_text(xlog_formatter* formatter, const log_record* rec,
                        char* buf, size_t buf_size)
{
	(void)formatter;  /* May use formatter->ctx for custom pattern in future */

	if (!rec || !buf || buf_size == 0)
	{
		return 0;
	}

	int written = log_record_format(rec, buf, buf_size);
	return (written > 0) ? (size_t)written : 0;
}

xlog_formatter* xlog_text_formatter_create(void)
{
	return xlog_formatter_create(XLOG_FMT_TEXT, xlog_format_text, NULL);
}

/* ============================================================================
 * JSON String Escaping
 * ============================================================================
 * JSON requires escaping: " \ / and control characters (\b \f \n \r \t)
 * Characters < 0x20 are escaped as \uXXXX
 */

/* Lookup table for characters that need escaping */
static const char* json_escape_table[256] = {
	/* 0x00-0x1F: Control characters */
	"\\u0000", "\\u0001", "\\u0002", "\\u0003", "\\u0004", "\\u0005", "\\u0006", "\\u0007",
	"\\b",     "\\t",     "\\n",     "\\u000b", "\\f",     "\\r",     "\\u000e", "\\u000f",
	"\\u0010", "\\u0011", "\\u0012", "\\u0013", "\\u0014", "\\u0015", "\\u0016", "\\u0017",
	"\\u0018", "\\u0019", "\\u001a", "\\u001b", "\\u001c", "\\u001d", "\\u001e", "\\u001f",
	/* 0x20-0x7F: Printable ASCII (most don't need escaping) */
	NULL, NULL, "\\\"", NULL, NULL, NULL, NULL, NULL,  /* 0x20-0x27: space ! " # $ % & ' */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,    /* 0x28-0x2F: ( ) * + , - . / */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,    /* 0x30-0x37: 0-7 */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,    /* 0x38-0x3F: 8 9 : ; < = > ? */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,    /* 0x40-0x47: @ A-G */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,    /* 0x48-0x4F: H-O */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,    /* 0x50-0x57: P-W */
	NULL, NULL, NULL, NULL, "\\\\", NULL, NULL, NULL,  /* 0x58-0x5F: X Y Z [ \ ] ^ _ */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,    /* 0x60-0x67: ` a-g */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,    /* 0x68-0x6F: h-o */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,    /* 0x70-0x77: p-w */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,    /* 0x78-0x7F: x y z { | } ~ DEL */
	/* 0x80-0xFF: Extended ASCII / UTF-8 continuation bytes (pass through) */
	[0x80 ... 0xFF] = NULL
};

size_t xlog_json_escape_string(const char* src, size_t src_len,
                               char* dst, size_t dst_size)
{
	if (!src)
	{
		if (dst && dst_size > 0) dst[0] = '\0';
		return 0;
	}

	if (src_len == (size_t)-1)
	{
		src_len = strlen(src);
	}

	size_t written = 0;
	const char* end = src + src_len;

	while (src < end)
	{
		unsigned char c = (unsigned char)*src;
		const char* escaped = json_escape_table[c];

		if (escaped)
		{
			/* Character needs escaping */
			size_t esc_len = strlen(escaped);
			if (dst && written + esc_len < dst_size)
			{
				memcpy(dst + written, escaped, esc_len);
			}
			written += esc_len;
		}
		else
		{
			/* Character can be copied directly */
			if (dst && written < dst_size - 1)
			{
				dst[written] = (char)c;
			}
			written++;
		}
		src++;
	}

	if (dst && dst_size > 0)
	{
		size_t null_pos = (written < dst_size - 1) ? written : dst_size - 1;
		dst[null_pos] = '\0';
	}

	return written;
}

/* SIMD-optimized version */
size_t xlog_json_escape_string_simd(const char* src, size_t src_len,
                                    char* dst, size_t dst_size)
{
#if defined(XLOG_HAS_SSE42)
	/*
	 * SSE4.2 optimization: Use pcmpistri to find characters that need escaping
	 * Characters to find: " \ and control chars (0x00-0x1F)
	 */
	if (!src)
	{
		if (dst && dst_size > 0) dst[0] = '\0';
		return 0;
	}

	if (src_len == (size_t)-1)
	{
		src_len = strlen(src);
	}

	/* For short strings, use scalar version */
	if (src_len < 16)
	{
		return xlog_json_escape_string(src, src_len, dst, dst_size);
	}

	/* Characters that need escaping: " \ and 0x00-0x1F */
	const __m128i escape_chars = _mm_setr_epi8(
		'"', '\\', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
	);
	const __m128i control_range = _mm_setr_epi8(
		0, 0x1F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	);

	size_t written = 0;
	size_t i = 0;

	while (i + 16 <= src_len)
	{
		__m128i chunk = _mm_loadu_si128((const __m128i*)(src + i));

		/* Check for escape characters */
		int has_quote = _mm_cmpistri(escape_chars, chunk,
			_SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_LEAST_SIGNIFICANT);
		int has_control = _mm_cmpistri(control_range, chunk,
			_SIDD_UBYTE_OPS | _SIDD_CMP_RANGES | _SIDD_LEAST_SIGNIFICANT);

		int first_escape = (has_quote < has_control) ? has_quote : has_control;

		if (first_escape == 16)
		{
			/* No escaping needed in this chunk - fast copy */
			if (dst && written + 16 < dst_size)
			{
				_mm_storeu_si128((__m128i*)(dst + written), chunk);
			}
			written += 16;
			i += 16;
		}
		else
		{
			/* Copy safe prefix, then handle escape character */
			if (first_escape > 0)
			{
				if (dst && written + (size_t)first_escape < dst_size)
				{
					memcpy(dst + written, src + i, (size_t)first_escape);
				}
				written += (size_t)first_escape;
				i += (size_t)first_escape;
			}

			/* Handle the escape character */
			unsigned char c = (unsigned char)src[i];
			const char* escaped = json_escape_table[c];
			if (escaped)
			{
				size_t esc_len = strlen(escaped);
				if (dst && written + esc_len < dst_size)
				{
					memcpy(dst + written, escaped, esc_len);
				}
				written += esc_len;
			}
			else
			{
				if (dst && written < dst_size - 1)
				{
					dst[written] = (char)c;
				}
				written++;
			}
			i++;
		}
	}

	/* Handle remaining bytes with scalar code */
	while (i < src_len)
	{
		unsigned char c = (unsigned char)src[i];
		const char* escaped = json_escape_table[c];

		if (escaped)
		{
			size_t esc_len = strlen(escaped);
			if (dst && written + esc_len < dst_size)
			{
				memcpy(dst + written, escaped, esc_len);
			}
			written += esc_len;
		}
		else
		{
			if (dst && written < dst_size - 1)
			{
				dst[written] = (char)c;
			}
			written++;
		}
		i++;
	}

	if (dst && dst_size > 0)
	{
		size_t null_pos = (written < dst_size - 1) ? written : dst_size - 1;
		dst[null_pos] = '\0';
	}

	return written;
#else
	/* Fallback to scalar implementation */
	return xlog_json_escape_string(src, src_len, dst, dst_size);
#endif
}

/* ============================================================================
 * JSON Formatter Implementation
 * ============================================================================
 * Output format:
 * {
 *   "timestamp": "2026-02-24T12:34:56.789123",
 *   "level": "INFO",
 *   "thread_id": 12345,
 *   "file": "main.c",
 *   "line": 42,
 *   "function": "main",
 *   "module": "app",
 *   "tag": "startup",
 *   "message": "Application started",
 *   "trace_id": "abc123",
 *   "fields": { "key1": "value1", "key2": 123 }
 * }
 */

/* Log level names for JSON */
static const char* const JSON_LEVEL_NAMES[] = {
	"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char* json_level_name(log_level level)
{
	if (level >= 0 && level <= LOG_LEVEL_FATAL)
	{
		return JSON_LEVEL_NAMES[level];
	}
	return "UNKNOWN";
}

/* Format timestamp as ISO 8601 for JSON */
static size_t format_timestamp_iso8601(uint64_t timestamp_ns, char* buf, size_t size)
{
	if (!buf || size < 28)
	{
		return 0;
	}

	time_t sec = (time_t)(timestamp_ns / 1000000000ULL);
	uint32_t usec = (uint32_t)((timestamp_ns % 1000000000ULL) / 1000);

	struct tm tm_buf;
	struct tm* tm_ptr = localtime_r(&sec, &tm_buf);
	if (!tm_ptr)
	{
		return (size_t)snprintf(buf, size, "1970-01-01T00:00:00.%06u", usec);
	}

	return (size_t)snprintf(buf, size, "%04d-%02d-%02dT%02d:%02d:%02d.%06u",
	                        tm_ptr->tm_year + 1900, tm_ptr->tm_mon + 1, tm_ptr->tm_mday,
	                        tm_ptr->tm_hour, tm_ptr->tm_min, tm_ptr->tm_sec, usec);
}

/* Helper macro for JSON field writing */
#define JSON_WRITE(p, end, ...) do { \
	int _n = snprintf((p), (size_t)((end) - (p)), __VA_ARGS__); \
	if (_n > 0) (p) += (_n < (end) - (p)) ? _n : (end) - (p) - 1; \
} while(0)

#define JSON_WRITE_STR(p, end, s) do { \
	const char* _s = (s); \
	while (*_s && (p) < (end)) *((p)++) = *_s++; \
} while(0)

size_t xlog_format_json(xlog_formatter* formatter, const log_record* rec,
                        char* buf, size_t buf_size)
{
	if (!rec || !buf || buf_size == 0)
	{
		return 0;
	}

	char* p = buf;
	char* end = buf + buf_size - 1;
	char tmp[512];
	char escaped[1024];

	/* Start JSON object */
	JSON_WRITE_STR(p, end, "{");

	/* Timestamp (ISO 8601) */
	format_timestamp_iso8601(rec->timestamp_ns, tmp, sizeof(tmp));
	JSON_WRITE(p, end, "\"timestamp\":\"%s\"", tmp);

	/* Level */
	JSON_WRITE(p, end, ",\"level\":\"%s\"", json_level_name((log_level)rec->level));

	/* Thread ID */
	JSON_WRITE(p, end, ",\"thread_id\":%u", rec->thread_id);

	/* File (basename only) */
	if (rec->loc.file)
	{
		const char* filename = rec->loc.file;
		const char* slash = strrchr(filename, '/');
		if (slash) filename = slash + 1;
		xlog_json_escape_string_simd(filename, (size_t)-1, escaped, sizeof(escaped));
		JSON_WRITE(p, end, ",\"file\":\"%s\"", escaped);
	}

	/* Line */
	JSON_WRITE(p, end, ",\"line\":%u", rec->loc.line);

	/* Function */
	if (rec->loc.func)
	{
		xlog_json_escape_string_simd(rec->loc.func, (size_t)-1, escaped, sizeof(escaped));
		JSON_WRITE(p, end, ",\"function\":\"%s\"", escaped);
	}

	/* Module (from context) */
	if ((rec->ctx.flags & LOG_CTX_HAS_MODULE) && rec->ctx.module)
	{
		xlog_json_escape_string_simd(rec->ctx.module, (size_t)-1, escaped, sizeof(escaped));
		JSON_WRITE(p, end, ",\"module\":\"%s\"", escaped);
	}

	/* Component (from context) */
	if ((rec->ctx.flags & LOG_CTX_HAS_COMPONENT) && rec->ctx.component)
	{
		xlog_json_escape_string_simd(rec->ctx.component, (size_t)-1, escaped, sizeof(escaped));
		JSON_WRITE(p, end, ",\"component\":\"%s\"", escaped);
	}

	/* Tag (from context) */
	if ((rec->ctx.flags & LOG_CTX_HAS_TAG) && rec->ctx.tag)
	{
		xlog_json_escape_string_simd(rec->ctx.tag, (size_t)-1, escaped, sizeof(escaped));
		JSON_WRITE(p, end, ",\"tag\":\"%s\"", escaped);
	}

	/* Trace ID */
	if (rec->ctx.flags & LOG_CTX_HAS_TRACE_ID)
	{
		JSON_WRITE(p, end, ",\"trace_id\":\"%016" PRIx64 "\"", rec->ctx.trace_id);
	}

	/* Span ID */
	if (rec->ctx.flags & LOG_CTX_HAS_SPAN_ID)
	{
		JSON_WRITE(p, end, ",\"span_id\":\"%016" PRIx64 "\"", rec->ctx.span_id);
	}

	/* Message (interpolated and escaped) */
	size_t msg_len = format_message_only(rec, tmp, sizeof(tmp));
	if (msg_len > 0)
	{
		xlog_json_escape_string_simd(tmp, msg_len, escaped, sizeof(escaped));
		JSON_WRITE(p, end, ",\"message\":\"%s\"", escaped);
	}
	else
	{
		JSON_WRITE_STR(p, end, ",\"message\":\"\"");
	}

	/* Custom fields */
	if (rec->field_count > 0)
	{
		JSON_WRITE_STR(p, end, ",\"fields\":{");
		for (int i = 0; i < rec->field_count && p < end; i++)
		{
			const log_custom_field* field = &rec->custom_fields[i];
			if (i > 0) JSON_WRITE_STR(p, end, ",");

			/* Determine field key */
			const char* key = field->key;
			if (!key)
			{
				key = log_field_type_name(field->type);
			}
			xlog_json_escape_string_simd(key, (size_t)-1, escaped, sizeof(escaped));

			/* Write based on field type */
			switch (field->type)
			{
				case LOG_FIELD_TAG:
				case LOG_FIELD_MODULE:
				case LOG_FIELD_COMPONENT:
				case LOG_FIELD_REQUEST_ID:
				case LOG_FIELD_SESSION_ID:
				case LOG_FIELD_CORRELATION_ID:
				case LOG_FIELD_CUSTOM_STR:
					if (field->value.str)
					{
						char val_escaped[256];
						xlog_json_escape_string_simd(field->value.str, (size_t)-1, val_escaped, sizeof(val_escaped));
						JSON_WRITE(p, end, "\"%s\":\"%s\"", escaped, val_escaped);
					}
					else
					{
						JSON_WRITE(p, end, "\"%s\":null", escaped);
					}
					break;

				case LOG_FIELD_TRACE_ID:
				case LOG_FIELD_SPAN_ID:
				case LOG_FIELD_USER_ID:
					JSON_WRITE(p, end, "\"%s\":\"%016" PRIx64 "\"", escaped, field->value.u64);
					break;

				case LOG_FIELD_CUSTOM_INT:
					JSON_WRITE(p, end, "\"%s\":%" PRId64, escaped, field->value.i64);
					break;

				case LOG_FIELD_CUSTOM_FLOAT:
					JSON_WRITE(p, end, "\"%s\":%g", escaped, field->value.f64);
					break;

				default:
					JSON_WRITE(p, end, "\"%s\":null", escaped);
					break;
			}
		}
		JSON_WRITE_STR(p, end, "}");
	}

	/* End JSON object */
	JSON_WRITE_STR(p, end, "}");

	/* Append newline if configured */
	if (formatter && formatter->append_newline && p < end)
	{
		*p++ = '\n';
	}

	*p = '\0';
	return (size_t)(p - buf);
}

xlog_formatter* xlog_json_formatter_create(void)
{
	xlog_formatter* f = xlog_formatter_create(XLOG_FMT_JSON, xlog_format_json, NULL);
	if (f)
	{
		f->append_newline = true;
	}
	return f;
}

/* ============================================================================
 * Inline JSON Format Function (for direct use without formatter object)
 * ============================================================================
 * This is the function called by json_formater.c
 */
int log_record_format_json_inline(const log_record* rec, char* buf, size_t buf_size)
{
	/* Create a temporary formatter config */
	xlog_formatter tmp_formatter =
	{
		.type = XLOG_FMT_JSON,
		.format = NULL,
		.ctx = NULL,
		.append_newline = true,
		.pretty_print = false
	};

	size_t written = xlog_format_json(&tmp_formatter, rec, buf, buf_size);
	return (written > 0) ? (int)written : -1;
}

/* ============================================================================
 * Raw Format Inline Function
 * ============================================================================ */
int log_record_format_raw_inline(const log_record* rec, char* buf, size_t buf_size)
{
	xlog_formatter tmp_formatter =
	{
		.type = XLOG_FMT_RAW,
		.format = NULL,
		.ctx = NULL,
		.append_newline = true,
		.pretty_print = false
	};

	size_t written = xlog_format_raw(&tmp_formatter, rec, buf, buf_size);
	return (written > 0) ? (int)written : -1;
}
