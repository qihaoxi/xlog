/* =====================================================================================
 *       Filename:  test_formatter.c
 *    Description:  Test Raw and JSON formatters
 *        Version:  1.0
 *        Created:  2026-02-24
 *       Compiler:  gcc/clang (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "../src/log_record.h"
#include "../src/xlog_formatter.h"
#include "../src/platform.h"

/* Test helper to create a simple log record */
static void create_test_record(log_record* rec, const char* msg)
{
    memset(rec, 0, sizeof(*rec));
    rec->level = LOG_LEVEL_INFO;
    rec->thread_id = xlog_get_tid();
    rec->timestamp_ns = xlog_get_timestamp_ns();
    rec->fmt = msg;
    rec->arg_count = 0;
    rec->loc.file = "test_formatter.c";
    rec->loc.line = 42;
    rec->loc.func = "test_function";
    atomic_store(&rec->ready, true);
}

/* Test helper with arguments */
static void create_test_record_with_args(log_record* rec, const char* fmt,
                                          const char* arg1, int arg2)
{
    memset(rec, 0, sizeof(*rec));
    rec->level = LOG_LEVEL_WARNING;
    rec->thread_id = xlog_get_tid();
    rec->timestamp_ns = xlog_get_timestamp_ns();
    rec->fmt = fmt;

    /* Add string argument */
    rec->arg_types[0] = LOG_ARG_STR_STATIC;
    rec->arg_values[0] = (uint64_t)(uintptr_t)arg1;

    /* Add integer argument */
    rec->arg_types[1] = LOG_ARG_I32;
    rec->arg_values[1] = (uint64_t)(int64_t)arg2;

    rec->arg_count = 2;
    rec->loc.file = "test_formatter.c";
    rec->loc.line = 100;
    rec->loc.func = "test_with_args";

    /* Add context */
    rec->ctx.flags = LOG_CTX_HAS_MODULE | LOG_CTX_HAS_TAG;
    rec->ctx.module = "test_module";
    rec->ctx.tag = "unit_test";

    atomic_store(&rec->ready, true);
}

/* ============================================================================
 * Test Cases
 * ============================================================================ */

static void test_raw_formatter(void)
{
    printf("\n=== Test: Raw Formatter ===\n");

    log_record rec;
    char buf[1024];

    /* Test simple message */
    create_test_record(&rec, "Hello, World!");

    xlog_formatter* formatter = xlog_raw_formatter_create();
    assert(formatter != NULL);
    assert(formatter->type == XLOG_FMT_RAW);

    size_t written = formatter->format(formatter, &rec, buf, sizeof(buf));
    printf("Raw output (%zu bytes): %s", written, buf);

    /* Should only contain the message, no timestamp/level */
    assert(strstr(buf, "Hello, World!") != NULL);
    assert(strstr(buf, "INFO") == NULL);  /* No level in raw format */
    assert(strstr(buf, "2026") == NULL);   /* No timestamp in raw format */

    xlog_formatter_destroy(formatter);
    printf("✓ Raw formatter test passed\n");
}

static void test_raw_formatter_with_args(void)
{
    printf("\n=== Test: Raw Formatter with Arguments ===\n");

    log_record rec;
    char buf[1024];

    create_test_record_with_args(&rec, "User {} has {} items", "alice", 42);

    xlog_formatter* formatter = xlog_raw_formatter_create();
    size_t written = formatter->format(formatter, &rec, buf, sizeof(buf));
    printf("Raw output (%zu bytes): %s", written, buf);

    /* Should interpolate arguments */
    assert(strstr(buf, "User alice has 42 items") != NULL);

    xlog_formatter_destroy(formatter);
    printf("✓ Raw formatter with args test passed\n");
}

static void test_json_formatter(void)
{
    printf("\n=== Test: JSON Formatter ===\n");

    log_record rec;
    char buf[2048];

    create_test_record(&rec, "Test JSON message");

    xlog_formatter* formatter = xlog_json_formatter_create();
    assert(formatter != NULL);
    assert(formatter->type == XLOG_FMT_JSON);

    size_t written = formatter->format(formatter, &rec, buf, sizeof(buf));
    printf("JSON output (%zu bytes):\n%s", written, buf);

    /* Validate JSON structure */
    assert(buf[0] == '{');
    assert(strstr(buf, "\"timestamp\"") != NULL);
    assert(strstr(buf, "\"level\":\"INFO\"") != NULL);
    assert(strstr(buf, "\"message\":\"Test JSON message\"") != NULL);
    assert(strstr(buf, "\"file\":\"test_formatter.c\"") != NULL);
    assert(strstr(buf, "\"line\":42") != NULL);

    xlog_formatter_destroy(formatter);
    printf("✓ JSON formatter test passed\n");
}

static void test_json_formatter_with_context(void)
{
    printf("\n=== Test: JSON Formatter with Context ===\n");

    log_record rec;
    char buf[2048];

    create_test_record_with_args(&rec, "User {} logged in, attempts: {}", "bob", 3);

    /* Add trace ID */
    rec.ctx.flags |= LOG_CTX_HAS_TRACE_ID;
    rec.ctx.trace_id = 0x123456789ABCDEF0ULL;

    xlog_formatter* formatter = xlog_json_formatter_create();
    size_t written = formatter->format(formatter, &rec, buf, sizeof(buf));
    printf("JSON output (%zu bytes):\n%s", written, buf);

    /* Validate context fields */
    assert(strstr(buf, "\"module\":\"test_module\"") != NULL);
    assert(strstr(buf, "\"tag\":\"unit_test\"") != NULL);
    assert(strstr(buf, "\"trace_id\":\"123456789abcdef0\"") != NULL);
    assert(strstr(buf, "\"message\":\"User bob logged in, attempts: 3\"") != NULL);

    xlog_formatter_destroy(formatter);
    printf("✓ JSON formatter with context test passed\n");
}

static void test_json_escaping(void)
{
    printf("\n=== Test: JSON String Escaping ===\n");

    char escaped[256];

    /* Test basic escaping */
    const char* input1 = "Hello \"World\"";
    size_t len1 = xlog_json_escape_string(input1, (size_t)-1, escaped, sizeof(escaped));
    printf("Input:  %s\n", input1);
    printf("Output: %s (%zu bytes)\n", escaped, len1);
    assert(strcmp(escaped, "Hello \\\"World\\\"") == 0);

    /* Test newline and tab */
    const char* input2 = "Line1\nLine2\tTabbed";
    size_t len2 = xlog_json_escape_string(input2, (size_t)-1, escaped, sizeof(escaped));
    printf("Input:  Line1\\nLine2\\tTabbed\n");
    printf("Output: %s (%zu bytes)\n", escaped, len2);
    assert(strstr(escaped, "\\n") != NULL);
    assert(strstr(escaped, "\\t") != NULL);

    /* Test backslash */
    const char* input3 = "path\\to\\file";
    size_t len3 = xlog_json_escape_string(input3, (size_t)-1, escaped, sizeof(escaped));
    printf("Input:  path\\to\\file\n");
    printf("Output: %s (%zu bytes)\n", escaped, len3);
    assert(strcmp(escaped, "path\\\\to\\\\file") == 0);

    printf("✓ JSON escaping test passed\n");
}

static void test_text_formatter(void)
{
    printf("\n=== Test: Text Formatter ===\n");

    log_record rec;
    char buf[1024];

    create_test_record(&rec, "Standard text message");

    xlog_formatter* formatter = xlog_text_formatter_create();
    assert(formatter != NULL);
    assert(formatter->type == XLOG_FMT_TEXT);

    size_t written = formatter->format(formatter, &rec, buf, sizeof(buf));
    printf("Text output (%zu bytes): %s", written, buf);

    /* Should contain timestamp and level */
    assert(strstr(buf, "INFO") != NULL);
    assert(strstr(buf, "Standard text message") != NULL);

    xlog_formatter_destroy(formatter);
    printf("✓ Text formatter test passed\n");
}

static void test_inline_functions(void)
{
    printf("\n=== Test: Inline Format Functions ===\n");

    log_record rec;
    char buf[2048];

    create_test_record(&rec, "Inline test message");

    /* Test JSON inline */
    int json_len = log_record_format_json_inline(&rec, buf, sizeof(buf));
    printf("JSON inline (%d bytes):\n%s", json_len, buf);
    assert(json_len > 0);
    assert(strstr(buf, "\"message\":\"Inline test message\"") != NULL);

    /* Test Raw inline */
    int raw_len = log_record_format_raw_inline(&rec, buf, sizeof(buf));
    printf("Raw inline (%d bytes): %s", raw_len, buf);
    assert(raw_len > 0);
    assert(strstr(buf, "Inline test message") != NULL);

    printf("✓ Inline functions test passed\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
    printf("===========================================\n");
    printf("   xlog Formatter Test Suite\n");
    printf("===========================================\n");

    test_raw_formatter();
    test_raw_formatter_with_args();
    test_json_formatter();
    test_json_formatter_with_context();
    test_json_escaping();
    test_text_formatter();
    test_inline_functions();

    printf("\n===========================================\n");
    printf("   All formatter tests PASSED!\n");
    printf("===========================================\n");

    return 0;
}

