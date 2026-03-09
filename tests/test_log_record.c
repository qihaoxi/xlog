/* =====================================================================================
 *       Filename:  test_log_record.c
 *    Description:  Test log_record structure and formatting
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
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include "../src/log_record.h"

/* 获取当前纳秒时间戳 */
static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* 获取线程 ID */
static uint32_t get_thread_id(void) {
    return (uint32_t)syscall(SYS_gettid);
}

/* 测试基本日志记录 */
static void test_basic_record(void) {
    printf("\n=== Test 1: Basic log record ===\n");

    log_record rec;
    log_record_init(&rec);

    /* 设置元信息 */
    log_record_set_meta(&rec,
                        LOG_LEVEL_INFO,
                        "User {} logged in from {}",
                        __FILE__,
                        __func__,
                        __LINE__,
                        get_thread_id(),
                        get_timestamp_ns());

    /* 添加参数 */
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)"admin");
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)"192.168.1.100");

    /* 提交记录 */
    log_record_commit(&rec);

    /* 格式化输出 */
    char output[1024];
    int len = log_record_format(&rec, output, sizeof(output));
    printf("Formatted (%d bytes): %s", len, output);

    /* 调试输出 */
    log_record_dump(&rec, stdout);
}

/* 测试带上下文的日志记录 */
static void test_context_record(void) {
    printf("\n=== Test 2: Log record with context ===\n");

    log_record rec;
    log_record_init(&rec);

    /* 设置元信息 */
    log_record_set_meta(&rec,
                        LOG_LEVEL_WARNING,
                        "Connection timeout after {} ms",
                        __FILE__,
                        __func__,
                        __LINE__,
                        get_thread_id(),
                        get_timestamp_ns());

    /* 设置上下文 */
    log_record_set_module(&rec, "network");
    log_record_set_component(&rec, "tcp_client");
    log_record_set_tag(&rec, "conn");
    log_record_set_trace(&rec, 0x123456789ABCDEF0ULL, 0x0FEDCBA987654321ULL);

    /* 添加参数 */
    log_record_add_arg(&rec, LOG_ARG_I32, 5000);

    /* 提交记录 */
    log_record_commit(&rec);

    /* 格式化输出 */
    char output[1024];
    int len = log_record_format(&rec, output, sizeof(output));
    printf("Formatted (%d bytes): %s", len, output);

    /* 调试输出 */
    log_record_dump(&rec, stdout);
}

/* 测试自定义字段 */
static void test_custom_fields(void) {
    printf("\n=== Test 3: Log record with custom fields ===\n");

    log_record rec;
    log_record_init(&rec);

    /* 设置元信息 */
    log_record_set_meta(&rec,
                        LOG_LEVEL_ERROR,
                        "Database query failed: {}",
                        __FILE__,
                        __func__,
                        __LINE__,
                        get_thread_id(),
                        get_timestamp_ns());

    /* 添加参数 */
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)"connection refused");

    /* 添加自定义字段（最多 LOG_MAX_CUSTOM_FIELDS 个）*/
    log_record_add_field_str(&rec, LOG_FIELD_TAG, NULL, "database");
    log_record_add_field_str(&rec, LOG_FIELD_REQUEST_ID, NULL, "req-12345-67890");

    /* 提交记录 */
    log_record_commit(&rec);

    /* 格式化输出 */
    char output[1024];
    int len = log_record_format(&rec, output, sizeof(output));
    printf("Formatted (%d bytes): %s", len, output);

    /* 调试输出 */
    log_record_dump(&rec, stdout);
}

/* 测试多种参数类型 */
static void test_multiple_arg_types(void) {
    printf("\n=== Test 4: Multiple argument types ===\n");

    log_record rec;
    log_record_init(&rec);

    /* 设置元信息 */
    log_record_set_meta(&rec,
                        LOG_LEVEL_DEBUG,
                        "Values: int={} float={} bool={} ptr={} str={}",
                        __FILE__,
                        __func__,
                        __LINE__,
                        get_thread_id(),
                        get_timestamp_ns());

    /* 添加不同类型的参数 */
    log_record_add_arg(&rec, LOG_ARG_I64, 42);
    log_record_add_arg(&rec, LOG_ARG_F64, log_f64_to_u64(3.14159));
    log_record_add_arg(&rec, LOG_ARG_BOOL, 1);
    log_record_add_arg(&rec, LOG_ARG_PTR, (uint64_t)(uintptr_t)&rec);
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)"hello");

    /* 提交记录 */
    log_record_commit(&rec);

    /* 格式化输出 */
    char output[1024];
    int len = log_record_format(&rec, output, sizeof(output));
    printf("Formatted (%d bytes): %s", len, output);

    /* 调试输出 */
    log_record_dump(&rec, stdout);
}

/* 测试动态字符串深拷贝 */
static void test_dynamic_string(void) {
    printf("\n=== Test 5: Dynamic string deep copy ===\n");

    log_record rec;
    log_record_init(&rec);

    /* 创建一个动态字符串（模拟从堆分配）*/
    char *dynamic_str = strdup("This is a dynamically allocated string!");

    /* 设置元信息 */
    log_record_set_meta(&rec,
                        LOG_LEVEL_INFO,
                        "Dynamic message: {}",
                        __FILE__,
                        __func__,
                        __LINE__,
                        get_thread_id(),
                        get_timestamp_ns());

    /* 添加动态字符串（会深拷贝到 inline_buf）*/
    log_record_add_string(&rec, dynamic_str, false);

    /* 释放原始字符串（模拟原字符串超出作用域）*/
    free(dynamic_str);
    dynamic_str = NULL;

    /* 提交记录 */
    log_record_commit(&rec);

    /* 格式化输出（应该仍然能正确输出，因为已经深拷贝）*/
    char output[1024];
    int len = log_record_format(&rec, output, sizeof(output));
    printf("Formatted (%d bytes): %s", len, output);
    printf("(Original string was freed, but message is still intact due to deep copy)\n");

    /* 调试输出 */
    log_record_dump(&rec, stdout);
}

/* 测试结构体大小 */
static void test_struct_sizes(void) {
    printf("\n=== Test 6: Structure sizes ===\n");

    printf("sizeof(log_arg_value)     = %zu bytes\n", sizeof(log_arg_value));
    printf("sizeof(log_arg)           = %zu bytes\n", sizeof(log_arg));
    printf("sizeof(log_source_loc)    = %zu bytes\n", sizeof(log_source_loc));
    printf("sizeof(log_context)       = %zu bytes\n", sizeof(log_context));
    printf("sizeof(log_custom_field)  = %zu bytes\n", sizeof(log_custom_field));
    printf("sizeof(log_record)        = %zu bytes\n", sizeof(log_record));
    printf("CACHE_LINE_SIZE           = %d bytes\n", CACHE_LINE_SIZE);
    printf("Cache lines per record    = %zu\n", sizeof(log_record) / CACHE_LINE_SIZE);

    /* 验证对齐 */
    if (sizeof(log_record) % CACHE_LINE_SIZE == 0) {
        printf("✓ log_record is properly cache-line aligned\n");
    } else {
        printf("✗ log_record is NOT cache-line aligned!\n");
    }
}

/* 测试自定义格式配置 */
static void test_custom_format(void) {
    printf("\n=== Test 7: Pre-compiled format patterns ===\n");

    log_record rec;
    log_record_init(&rec);

    log_record_set_meta(&rec,
                        LOG_LEVEL_INFO,
                        "Custom format test message: {}",
                        __FILE__,
                        __func__,
                        __LINE__,
                        get_thread_id(),
                        get_timestamp_ns());

    log_record_set_module(&rec, "auth");
    log_record_set_tag(&rec, "login");
    log_record_add_arg(&rec, LOG_ARG_I32, 12345);
    log_record_commit(&rec);

    char output[1024];

    /* 测试默认格式 */
    printf("\n1) Default pattern (LOG_PATTERN_DEFAULT):\n");
    static const log_fmt_pattern pat_default = LOG_PATTERN_DEFAULT;
    log_record_format_pattern(&rec, &pat_default, output, sizeof(output));
    printf("   %s", output);

    /* 测试简洁格式 */
    printf("\n2) Simple pattern (LOG_PATTERN_SIMPLE):\n");
    static const log_fmt_pattern pat_simple = LOG_PATTERN_SIMPLE;
    log_record_format_pattern(&rec, &pat_simple, output, sizeof(output));
    printf("   %s", output);

    /* 测试带模块格式 */
    printf("\n3) Module pattern (LOG_PATTERN_WITH_MODULE):\n");
    static const log_fmt_pattern pat_module = LOG_PATTERN_WITH_MODULE;
    log_record_format_pattern(&rec, &pat_module, output, sizeof(output));
    printf("   %s", output);

    /* 测试带标签格式 */
    printf("\n4) Tag pattern (LOG_PATTERN_WITH_TAG):\n");
    static const log_fmt_pattern pat_tag = LOG_PATTERN_WITH_TAG;
    log_record_format_pattern(&rec, &pat_tag, output, sizeof(output));
    printf("   %s", output);

    /* 测试生产格式 */
    printf("\n5) Production pattern (LOG_PATTERN_PROD):\n");
    static const log_fmt_pattern pat_prod = LOG_PATTERN_PROD;
    log_record_format_pattern(&rec, &pat_prod, output, sizeof(output));
    printf("   %s", output);

    /* 测试调试格式 */
    printf("\n6) Debug pattern (LOG_PATTERN_DEBUG):\n");
    static const log_fmt_pattern pat_debug = LOG_PATTERN_DEBUG;
    log_record_format_pattern(&rec, &pat_debug, output, sizeof(output));
    printf("   %s", output);

    /* 测试设置全局默认格式 */
    printf("\n7) Set global default to simple pattern:\n");
    log_format_set_pattern(&pat_simple);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 恢复默认格式 */
    log_format_set_pattern(NULL);
    printf("\n8) Restored to default pattern:\n");
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);
}

/* 测试无线程ID和可选字段 */
static void test_optional_fields(void) {
    printf("\n=== Test 8: Optional fields (skip empty) ===\n");

    log_record rec;
    log_record_init(&rec);

    /* 创建一个只有基本信息的日志记录（无 module, tag, trace_id 等）*/
    log_record_set_meta(&rec,
                        LOG_LEVEL_DEBUG,
                        "Minimal log message",
                        __FILE__,
                        __func__,
                        __LINE__,
                        get_thread_id(),
                        get_timestamp_ns());
    log_record_commit(&rec);

    char output[1024];

    /* 使用调试格式，空字段会被自动跳过 */
    printf("\n1) Debug pattern (empty fields auto-skipped):\n");
    static const log_fmt_pattern pat_debug = LOG_PATTERN_DEBUG;
    log_record_format_pattern(&rec, &pat_debug, output, sizeof(output));
    printf("   %s", output);

    /* 无位置信息格式 */
    printf("\n2) No location pattern:\n");
    static const log_fmt_pattern pat_no_loc = LOG_PATTERN_NO_LOCATION;
    log_record_format_pattern(&rec, &pat_no_loc, output, sizeof(output));
    printf("   %s", output);

    /* 现在添加 module 再格式化 */
    printf("\n3) Debug pattern after adding module:\n");
    log_record_set_module(&rec, "system");
    log_record_format_pattern(&rec, &pat_debug, output, sizeof(output));
    printf("   %s", output);
}

/* 测试位置信息的各种组合 */
static void test_location_options(void) {
    printf("\n=== Test 9: Location patterns ===\n");

    log_record rec;
    log_record_init(&rec);

    log_record_set_meta(&rec,
                        LOG_LEVEL_INFO,
                        "Testing location options",
                        __FILE__,
                        __func__,
                        __LINE__,
                        get_thread_id(),
                        get_timestamp_ns());
    log_record_commit(&rec);

    char output[1024];

    /* 完整位置 */
    printf("\n1) Full location [file:line@func]:\n");
    static const log_fmt_pattern pat_default = LOG_PATTERN_DEFAULT;
    log_record_format_pattern(&rec, &pat_default, output, sizeof(output));
    printf("   %s", output);

    /* 只有文件:行号 */
    printf("\n2) File and line only [file:line]:\n");
    static const log_fmt_pattern pat_file_line = LOG_PATTERN_FILE_LINE;
    log_record_format_pattern(&rec, &pat_file_line, output, sizeof(output));
    printf("   %s", output);

    /* 无位置信息 */
    printf("\n3) No location info:\n");
    static const log_fmt_pattern pat_simple = LOG_PATTERN_SIMPLE;
    log_record_format_pattern(&rec, &pat_simple, output, sizeof(output));
    printf("   %s", output);
}

/* 测试内联格式化函数 */
static void test_inline_format(void) {
    printf("\n=== Test 10: Inline format functions (highest performance) ===\n");

    log_record rec;
    log_record_init(&rec);

    log_record_set_meta(&rec,
                        LOG_LEVEL_INFO,
                        "Inline format test: value={}",
                        __FILE__,
                        __func__,
                        __LINE__,
                        get_thread_id(),
                        get_timestamp_ns());
    log_record_set_module(&rec, "perf");
    log_record_set_tag(&rec, "test");
    log_record_add_arg(&rec, LOG_ARG_I32, 42);
    log_record_commit(&rec);

    char output[1024];

    printf("\n1) Default inline format:\n");
    log_record_format_default_inline(&rec, output, sizeof(output));
    printf("   %s", output);

    printf("\n2) Simple inline format:\n");
    log_record_format_simple_inline(&rec, output, sizeof(output));
    printf("   %s", output);

    printf("\n3) Production inline format:\n");
    log_record_format_prod_inline(&rec, output, sizeof(output));
    printf("   %s", output);
}

/* 性能对比测试 */
static void test_performance_comparison(void) {
    printf("\n=== Test 11: Performance comparison ===\n");

    log_record rec;
    log_record_init(&rec);

    log_record_set_meta(&rec,
                        LOG_LEVEL_INFO,
                        "Performance test message: {}",
                        __FILE__,
                        __func__,
                        __LINE__,
                        get_thread_id(),
                        get_timestamp_ns());
    log_record_set_module(&rec, "bench");
    log_record_set_tag(&rec, "perf");
    log_record_add_arg(&rec, LOG_ARG_I32, 12345);
    log_record_commit(&rec);

    char output[1024];
    const int iterations = 100000;
    struct timespec start, end;

    /* 预热 */
    for (int i = 0; i < 1000; i++) {
        log_record_format_default_inline(&rec, output, sizeof(output));
    }

    /* 测试 pattern-based 格式化 */
    static const log_fmt_pattern pat = LOG_PATTERN_DEFAULT;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        log_record_format_pattern(&rec, &pat, output, sizeof(output));
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    uint64_t pattern_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL +
                          (end.tv_nsec - start.tv_nsec);

    /* 测试 inline 格式化 */
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        log_record_format_default_inline(&rec, output, sizeof(output));
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    uint64_t inline_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL +
                         (end.tv_nsec - start.tv_nsec);

    printf("\nIterations: %d\n", iterations);
    printf("Pattern-based: %lu ns total, %.2f ns/op\n",
           pattern_ns, (double)pattern_ns / iterations);
    printf("Inline:        %lu ns total, %.2f ns/op\n",
           inline_ns, (double)inline_ns / iterations);
    printf("Speedup:       %.2fx\n", (double)pattern_ns / inline_ns);
}

/* ============================================================================
 * 边缘测试：空消息和 NULL 处理
 * ============================================================================ */
static void test_null_and_empty(void) {
    printf("\n=== Test 12: NULL and empty message handling ===\n");

    log_record rec;
    char output[1024];
    int result;

    /* 测试 1: 空格式字符串 */
    printf("\n1) Empty format string:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result (%d bytes): %s", result, output);

    /* 测试 2: NULL 格式字符串 */
    printf("\n2) NULL format string:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_WARNING, NULL,
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result (%d bytes): %s", result, output);

    /* 测试 3: NULL 文件名 */
    printf("\n3) NULL file name:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_ERROR, "Test message",
                        NULL, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result (%d bytes): %s", result, output);

    /* 测试 4: NULL 函数名 */
    printf("\n4) NULL function name:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_DEBUG, "Test message",
                        __FILE__, NULL, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result (%d bytes): %s", result, output);

    /* 测试 5: 全部为 NULL */
    printf("\n5) All NULL (file, func, fmt):\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_FATAL, NULL,
                        NULL, NULL, 0,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result (%d bytes): %s", result, output);

    /* 测试 6: NULL 字符串参数 */
    printf("\n6) NULL string argument:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Value: {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)NULL);
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result (%d bytes): %s", result, output);

    /* 测试 7: NULL 输出缓冲区 */
    printf("\n7) NULL output buffer:\n");
    result = log_record_format(&rec, NULL, sizeof(output));
    printf("   Result: %d (expected -1)\n", result);

    /* 测试 8: 零大小输出缓冲区 */
    printf("\n8) Zero size output buffer:\n");
    result = log_record_format(&rec, output, 0);
    printf("   Result: %d (expected -1)\n", result);

    /* 测试 9: NULL 记录 */
    printf("\n9) NULL record:\n");
    result = log_record_format(NULL, output, sizeof(output));
    printf("   Result: %d (expected -1)\n", result);
}

/* ============================================================================
 * 边缘测试：超长消息处理
 * ============================================================================ */
static void test_long_messages(void) {
    printf("\n=== Test 13: Long message handling ===\n");

    log_record rec;
    char output[1024*2024+1024]={0};
    int result;

    /* 测试 1: 超长格式字符串 */
    printf("\n1) Very long format string (500 chars):\n");
    char long_fmt[512];
    memset(long_fmt, 'A', 500);
    long_fmt[500] = '\0';

    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, long_fmt,
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result length: %d bytes\n", result);
    printf("   First 80 chars: %.80s...\n", output);

    /* 测试 2: 超长字符串参数 */
    printf("\n2) Very long string argument (200 chars):\n");
    char long_arg[256];
    memset(long_arg, 'B', 200);
    long_arg[200] = '\0';

    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Long arg: {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)long_arg);
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result length: %d bytes\n", result);

    /* 测试 3: 输出缓冲区太小（截断测试）*/
    printf("\n3) Small output buffer (truncation test):\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "This is a test message that will be truncated",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);

    char small_buf[50];
    result = log_record_format(&rec, small_buf, sizeof(small_buf));
    printf("   Buffer size: 50, Result length: %d\n", result);
    printf("   Output: %s\n", small_buf);

    /* 测试 4: 极小缓冲区 */
    printf("\n4) Tiny output buffer (10 bytes):\n");
    char tiny_buf[10];
    result = log_record_format(&rec, tiny_buf, sizeof(tiny_buf));
    printf("   Buffer size: 10, Result length: %d\n", result);
    printf("   Output: %s\n", tiny_buf);

    /* 测试 5: 超长模块名 */
    printf("\n5) Very long module name (100 chars):\n");
    char long_module[128];
    memset(long_module, 'M', 100);
    long_module[100] = '\0';

    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Test",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_set_module(&rec, long_module);
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result length: %d bytes\n", result);

    /* 测试 6: 超长标签 */
    printf("\n6) Very long tag (100 chars):\n");
    char long_tag[128];
    memset(long_tag, 'T', 100);
    long_tag[100] = '\0';

    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Test",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_set_tag(&rec, long_tag);
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result length: %d bytes\n", result);

    /* 测试 7: 超大消息（1MB）*/
    printf("\n7) Very large message (1MB string argument):\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Large message: {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    char *huge_arg = malloc(1024 * 1024);
    memset(huge_arg, 'H', 1024 * 1024 - 1);
    huge_arg[1024 * 1024 - 1] = '\0';
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)huge_arg);
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Result length: %d bytes (truncated to output buffer)\n", result);
    free(huge_arg);
}

/* ============================================================================
 * 边缘测试：参数数量边界
 * ============================================================================ */
static void test_argument_boundaries(void) {
    printf("\n=== Test 14: Argument count boundaries ===\n");

    log_record rec;
    char output[1024];
    //int result;
    bool add_result;

    /* 测试 1: 最大参数数量 */
    printf("\n1) Maximum arguments (%d):\n", LOG_MAX_ARGS);
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO,
                        "Args: {} {} {} {} {} {} {} {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    for (int i = 0; i < LOG_MAX_ARGS; i++) {
        add_result = log_record_add_arg(&rec, LOG_ARG_I32, (uint64_t)i);
        if (!add_result) {
            printf("   Failed to add arg %d\n", i);
        }
    }
    printf("   arg_count after adding %d: %d\n", LOG_MAX_ARGS, rec.arg_count);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Result: %s", output);

    /* 测试 2: 超过最大参数数量 */
    printf("\n2) Exceed maximum arguments:\n");
    add_result = log_record_add_arg(&rec, LOG_ARG_I32, 999);
    printf("   Adding 9th argument: %s\n", add_result ? "SUCCESS (unexpected)" : "FAILED (expected)");

    /* 测试 3: 零参数 */
    printf("\n3) Zero arguments with placeholders:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO,
                        "No args but placeholders: {} {} {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Result: %s", output);

    /* 测试 4: 参数多于占位符 */
    printf("\n4) More arguments than placeholders:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Only one: {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_I32, 111);
    log_record_add_arg(&rec, LOG_ARG_I32, 222);
    log_record_add_arg(&rec, LOG_ARG_I32, 333);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Result: %s", output);

    /* 测试 5: 占位符多于参数 */
    printf("\n5) More placeholders than arguments:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Three placeholders: {} {} {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_I32, 42);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Result: %s", output);
}

/* ============================================================================
 * 边缘测试：自定义字段边界
 * ============================================================================ */
static void test_custom_field_boundaries(void) {
    printf("\n=== Test 15: Custom field boundaries ===\n");

    log_record rec;
    char output[1024];
    //int result;
    bool add_result;

    /* 测试 1: 最大自定义字段数量 - 使用静态键名 */
    printf("\n1) Maximum custom fields (%d):\n", LOG_MAX_CUSTOM_FIELDS);
    static const char *field_keys[] = {"field0", "field1", "field2", "field3", "field4", "field5", "field6", "field7"};

    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Test fields",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    for (int i = 0; i < LOG_MAX_CUSTOM_FIELDS && i < 8; i++) {
        add_result = log_record_add_field_int(&rec, LOG_FIELD_CUSTOM_INT, field_keys[i], i * 100);
        if (!add_result) {
            printf("   Failed to add field %d\n", i);
        }
    }
    printf("   field_count after adding %d: %d\n", LOG_MAX_CUSTOM_FIELDS, rec.field_count);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Result: %s", output);

    /* 测试 2: 超过最大字段数量 */
    printf("\n2) Exceed maximum custom fields:\n");
    add_result = log_record_add_field_int(&rec, LOG_FIELD_CUSTOM_INT, "extra", 999);
    printf("   Adding extra field: %s\n", add_result ? "SUCCESS (unexpected)" : "FAILED (expected)");

    /* 测试 3: NULL key 的自定义字段 */
    printf("\n3) Custom field with NULL key:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Test",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_field_str(&rec, LOG_FIELD_CUSTOM_STR, NULL, "value_without_key");
    log_record_commit(&rec);
     log_record_format(&rec, output, sizeof(output));
    printf("   Result: %s", output);

    /* 测试 4: NULL value 的自定义字段 */
    printf("\n4) Custom field with NULL value:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Test",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_field_str(&rec, LOG_FIELD_CUSTOM_STR, "mykey", NULL);
    log_record_commit(&rec);
     log_record_format(&rec, output, sizeof(output));
    printf("   Result: %s", output);

    /* 测试 5: 空字符串 key 和 value */
    printf("\n5) Empty string key and value:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Test",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_field_str(&rec, LOG_FIELD_CUSTOM_STR, "", "");
    log_record_commit(&rec);
     log_record_format(&rec, output, sizeof(output));
    printf("   Result: %s", output);
}

/* ============================================================================
 * 边缘测试：内联缓冲区边界
 * ============================================================================ */
static void test_inline_buffer_boundaries(void) {
    printf("\n=== Test 16: Inline buffer boundaries ===\n");

    log_record rec;
    char output[2048];
    int result;

    /* 测试 1: 填满内联缓冲区 */
    printf("\n1) Fill inline buffer completely:\n");
    char fill_str[LOG_INLINE_BUF_SIZE];
    memset(fill_str, 'X', LOG_INLINE_BUF_SIZE - 1);
    fill_str[LOG_INLINE_BUF_SIZE - 1] = '\0';

    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Inline: {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    /* 复制动态字符串 */
    char *dyn_str = strdup(fill_str);
    bool add_result = log_record_add_string(&rec, dyn_str, false);
    printf("   inline_buf_used after adding %zu byte string: %u\n",
           strlen(dyn_str) + 1, rec.inline_buf_used);
    printf("   Add result: %s\n", add_result ? "SUCCESS" : "FAILED");
    free(dyn_str);

    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Format result length: %d\n", result);

    /* 测试 2: 超过内联缓冲区容量 */
    printf("\n2) Exceed inline buffer capacity:\n");
    char exceed_str[LOG_INLINE_BUF_SIZE + 50];
    memset(exceed_str, 'Y', LOG_INLINE_BUF_SIZE + 49);
    exceed_str[LOG_INLINE_BUF_SIZE + 49] = '\0';

    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Exceed: {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    dyn_str = strdup(exceed_str);
    add_result = log_record_add_string(&rec, dyn_str, false);
    printf("   Trying to add %zu byte string to %d byte buffer\n",
           strlen(dyn_str) + 1, LOG_INLINE_BUF_SIZE);
    printf("   Add result: %s (should be true but marked as EXTERN)\n",
           add_result ? "SUCCESS" : "FAILED");
    printf("   Arg type: 0x%02x (expected 0x%02x for STR_EXTERN)\n",
           rec.arg_types[0], LOG_ARG_STR_EXTERN);
    free(dyn_str);

    /* 测试 3: 多个动态字符串，逐步填满 */
    printf("\n3) Multiple dynamic strings filling buffer:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Multi: {} {} {} {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    char small_str[40];
    memset(small_str, 'Z', 39);
    small_str[39] = '\0';

    int count = 0;
    while (rec.arg_count < LOG_MAX_ARGS) {
        dyn_str = strdup(small_str);
        add_result = log_record_add_string(&rec, dyn_str, false);
        if (!add_result) break;

        printf("   After string %d: inline_buf_used=%u, type=0x%02x\n",
               count + 1, rec.inline_buf_used, rec.arg_types[count]);
        free(dyn_str);
        count++;
    }
    printf("   Total strings added: %d\n", count);

    /* 测试 4: 安全字符串函数（截断测试）*/
    printf("\n4) Safe string function (truncation test):\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Safe: {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    /* 创建一个超长字符串 */
    char long_str[200];
    memset(long_str, 'L', 199);
    long_str[199] = '\0';

    dyn_str = strdup(long_str);
    add_result = log_record_add_string_safe(&rec, dyn_str, false);
    printf("   Adding 199 byte string to %d byte buffer using _safe\n", LOG_INLINE_BUF_SIZE);
    printf("   Add result: %s\n", add_result ? "SUCCESS" : "FAILED");
    printf("   Arg type: 0x%02x (expected 0x%02x for STR_INLINE - truncated)\n",
           rec.arg_types[0], LOG_ARG_STR_INLINE);
    printf("   inline_buf_used: %u\n", rec.inline_buf_used);

    /* 格式化输出看截断效果 */
    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Formatted output: %.100s...\n", output);
    free(dyn_str);

    /* 测试 5: 安全字符串函数（完整拷贝）*/
    printf("\n5) Safe string function (full copy):\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Safe full: {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    dyn_str = strdup("short string");
    add_result = log_record_add_string_safe(&rec, dyn_str, false);
    printf("   Adding short string using _safe\n");
    printf("   Arg type: 0x%02x (expected 0x%02x for STR_INLINE)\n",
           rec.arg_types[0], LOG_ARG_STR_INLINE);
    free(dyn_str);  /* 安全释放，因为已深拷贝 */

    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Formatted output: %s", output);

    /* 测试 6: 安全字符串函数（缓冲区已满）*/
    printf("\n6) Safe string function (buffer full):\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Full: {} {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    /* 先填满缓冲区 */
    char fill_buf[LOG_INLINE_BUF_SIZE];
    memset(fill_buf, 'F', LOG_INLINE_BUF_SIZE - 1);
    fill_buf[LOG_INLINE_BUF_SIZE - 1] = '\0';
    dyn_str = strdup(fill_buf);
    log_record_add_string_safe(&rec, dyn_str, false);
    free(dyn_str);
    printf("   After filling buffer: inline_buf_used=%u\n", rec.inline_buf_used);

    /* 再添加一个字符串 */
    dyn_str = strdup("another string");
    add_result = log_record_add_string_safe(&rec, dyn_str, false);
    printf("   Adding another string when buffer full\n");
    printf("   Arg type: 0x%02x (expected 0x%02x for STR_STATIC - placeholder)\n",
           rec.arg_types[1], LOG_ARG_STR_STATIC);
    free(dyn_str);

    log_record_commit(&rec);
    result = log_record_format(&rec, output, sizeof(output));
    printf("   Formatted output: %.120s...\n", output);
}

/* ============================================================================
 * 边缘测试：特殊数值边界
 * ============================================================================ */
static void test_numeric_boundaries(void) {
    printf("\n=== Test 17: Numeric value boundaries ===\n");

    log_record rec;
    char output[1024];

    /* 测试 1: 整数边界值 */
    printf("\n1) Integer boundary values:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO,
                        "i8:{} i16:{} i32:{} i64:{} u64:{}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    log_record_add_arg(&rec, LOG_ARG_I8, (uint64_t)(int64_t)INT8_MIN);
    log_record_add_arg(&rec, LOG_ARG_I16, (uint64_t)(int64_t)INT16_MIN);
    log_record_add_arg(&rec, LOG_ARG_I32, (uint64_t)(int64_t)INT32_MIN);
    log_record_add_arg(&rec, LOG_ARG_I64, (uint64_t)INT64_MIN);
    log_record_add_arg(&rec, LOG_ARG_U64, UINT64_MAX);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Min values: %s", output);

    /* 测试 2: 浮点数特殊值 */
    printf("\n2) Float special values:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO,
                        "zero:{} neg_zero:{} tiny:{} huge:{}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    log_record_add_arg(&rec, LOG_ARG_F64, log_f64_to_u64(0.0));
    log_record_add_arg(&rec, LOG_ARG_F64, log_f64_to_u64(-0.0));
    log_record_add_arg(&rec, LOG_ARG_F64, log_f64_to_u64(1e-308));
    log_record_add_arg(&rec, LOG_ARG_F64, log_f64_to_u64(1e308));
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Special floats: %s", output);

    /* 测试 3: 指针值 */
    printf("\n3) Pointer values:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO,
                        "null:{} stack:{} heap:{}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    int stack_var = 42;
    void *heap_ptr = malloc(1);

    log_record_add_arg(&rec, LOG_ARG_PTR, (uint64_t)(uintptr_t)NULL);
    log_record_add_arg(&rec, LOG_ARG_PTR, (uint64_t)(uintptr_t)&stack_var);
    log_record_add_arg(&rec, LOG_ARG_PTR, (uint64_t)(uintptr_t)heap_ptr);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Pointers: %s", output);

    free(heap_ptr);

    /* 测试 4: 布尔值 */
    printf("\n4) Boolean values:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO,
                        "true:{} false:{} nonzero:{}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    log_record_add_arg(&rec, LOG_ARG_BOOL, 1);
    log_record_add_arg(&rec, LOG_ARG_BOOL, 0);
    log_record_add_arg(&rec, LOG_ARG_BOOL, 255);  /* 非零值也应该是 true */
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Booleans: %s", output);

    /* 测试 5: 字符值 */
    printf("\n5) Character values:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO,
                        "normal:{} newline:{} tab:{} null:{}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());

    log_record_add_arg(&rec, LOG_ARG_CHAR, 'A');
    log_record_add_arg(&rec, LOG_ARG_CHAR, '\n');
    log_record_add_arg(&rec, LOG_ARG_CHAR, '\t');
    log_record_add_arg(&rec, LOG_ARG_CHAR, '\0');
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Characters: %s", output);
}

/* ============================================================================
 * 边缘测试：日志级别边界
 * ============================================================================ */
static void test_log_level_boundaries(void) {
    printf("\n=== Test 18: Log level boundaries ===\n");

    log_record rec;
    char output[1024];

    /* 测试所有有效级别 */
    printf("\n1) All valid log levels:\n");
    const char *levels[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    for (int i = LOG_LEVEL_TRACE; i <= LOG_LEVEL_FATAL; i++) {
        log_record_init(&rec);
        log_record_set_meta(&rec, (log_level)i, "Level test",
                            __FILE__, __func__, __LINE__,
                            get_thread_id(), get_timestamp_ns());
        log_record_commit(&rec);
        log_record_format(&rec, output, sizeof(output));
        printf("   Level %d (%s): %.60s...\n", i, levels[i], output);
    }

    /* 测试无效级别 */
    printf("\n2) Invalid log levels:\n");
    log_record_init(&rec);
    rec.level = 100;  /* 无效级别 */
    log_record_set_meta(&rec, (log_level)100, "Invalid level",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   Level 100: %.60s...\n", output);
}

/* ============================================================================
 * 边缘测试：时间戳边界
 * ============================================================================ */
static void test_timestamp_boundaries(void) {
    printf("\n=== Test 19: Timestamp boundaries ===\n");

    log_record rec;
    char output[1024];

    /* 测试 1: 零时间戳 */
    printf("\n1) Zero timestamp (epoch):\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Zero timestamp",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), 0);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 测试 2: 最大时间戳 */
    printf("\n2) Very large timestamp:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Large timestamp",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), UINT64_MAX);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 测试 3: 特定日期时间戳 */
    printf("\n3) Specific date timestamp (2026-02-09):\n");
    uint64_t ts_2026 = 1770508800ULL * 1000000000ULL;  /* 大约 2026-02-09 */
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Year 2026",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), ts_2026);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);
}

/* ============================================================================
 * 边缘测试：格式字符串特殊字符
 * ============================================================================ */
static void test_special_format_chars(void) {
    printf("\n=== Test 20: Special characters in format string ===\n");

    log_record rec;
    char output[1024];

    /* 测试 1: 百分号转义 */
    printf("\n1) Percent sign escape:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "100%% complete, value={}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_I32, 42);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 测试 2: 多个百分号 */
    printf("\n2) Multiple percent signs:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "%%%%%% {} %%%%%%",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_I32, 123);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 测试 3: 连续占位符 */
    printf("\n3) Consecutive placeholders:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "{}{}{}{}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_I32, 1);
    log_record_add_arg(&rec, LOG_ARG_I32, 2);
    log_record_add_arg(&rec, LOG_ARG_I32, 3);
    log_record_add_arg(&rec, LOG_ARG_I32, 4);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 测试 4: 混合 printf 和 {} 占位符 */
    printf("\n4) Mixed printf and {} placeholders:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "printf %d and rust {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_I32, 111);
    log_record_add_arg(&rec, LOG_ARG_I32, 222);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 测试 5: 特殊字符 */
    printf("\n5) Special characters:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO,
                        "Tab:\tNewline:\\n Quote:\" Backslash:\\ {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)"end");
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 测试 6: 不完整的占位符 */
    printf("\n6) Incomplete placeholders:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Open brace { and close } separately",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 测试 7: 末尾的百分号 */
    printf("\n7) Trailing percent sign:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Ends with percent%",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);
}

/* ============================================================================
 * 边缘测试：记录状态
 * ============================================================================ */
static void test_record_state(void) {
    printf("\n=== Test 21: Record state management ===\n");

    log_record rec;
    char output[1024];

    /* 测试 1: 未提交的记录 */
    printf("\n1) Uncommitted record:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Not committed",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    printf("   Before commit: ready=%d\n", log_record_is_ready(&rec));

    log_record_commit(&rec);
    printf("   After commit: ready=%d\n", log_record_is_ready(&rec));

    /* 测试 2: 重置记录 */
    printf("\n2) Reset record:\n");
    log_record_reset(&rec);
    printf("   After reset: ready=%d, arg_count=%d, field_count=%d\n",
           log_record_is_ready(&rec), rec.arg_count, rec.field_count);

    /* 测试 3: 重用记录 */
    printf("\n3) Reuse record after reset:\n");
    log_record_set_meta(&rec, LOG_LEVEL_DEBUG, "Reused record",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_I32, 999);
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 测试 4: 多次提交 */
    printf("\n4) Multiple commits:\n");
    log_record_commit(&rec);
    log_record_commit(&rec);
    printf("   After multiple commits: ready=%d\n", log_record_is_ready(&rec));
}

/* ============================================================================
 * 综合测试：复杂场景
 * ============================================================================ */
static void test_complex_scenarios(void) {
    printf("\n=== Test 22: Complex scenarios ===\n");

    log_record rec;
    char output[2048];

    /* 测试 1: 所有元信息都设置 */
    printf("\n1) Full metadata with all fields:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Complex: {} {} {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_set_module(&rec, "payment");
    log_record_set_component(&rec, "validator");
    log_record_set_tag(&rec, "txn");
    log_record_set_trace(&rec, 0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL);
    log_record_add_arg(&rec, LOG_ARG_I32, 100);
    log_record_add_arg(&rec, LOG_ARG_F64, log_f64_to_u64(3.14));
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)"success");
    log_record_add_field_str(&rec, LOG_FIELD_REQUEST_ID, NULL, "req-abc-123");
    log_record_add_field_int(&rec, LOG_FIELD_CUSTOM_INT, "amount", 9999);
    log_record_commit(&rec);

    /* 使用调试格式输出 */
    static const log_fmt_pattern pat_debug = LOG_PATTERN_DEBUG;
    log_record_format_pattern(&rec, &pat_debug, output, sizeof(output));
    printf("   %s", output);

    /* 测试 2: Unicode 字符（如果系统支持）*/
    printf("\n2) Unicode characters:\n");
    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_INFO, "Unicode: {} {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)"中文");
    log_record_add_arg(&rec, LOG_ARG_STR_STATIC, (uint64_t)(uintptr_t)"日本語");
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);

    /* 测试 3: 大量数据的日志 */
    printf("\n3) Log with maximum data:\n");
    static const char *max_field_keys[] = {"f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7"};

    log_record_init(&rec);
    log_record_set_meta(&rec, LOG_LEVEL_WARNING,
                        "Max: {} {} {} {} {} {} {} {}",
                        __FILE__, __func__, __LINE__,
                        get_thread_id(), get_timestamp_ns());
    log_record_set_module(&rec, "max_module");
    log_record_set_tag(&rec, "max_tag");
    for (int i = 0; i < LOG_MAX_ARGS; i++) {
        log_record_add_arg(&rec, LOG_ARG_I32, i * 111);
    }
    for (int i = 0; i < LOG_MAX_CUSTOM_FIELDS && i < 8; i++) {
        log_record_add_field_int(&rec, LOG_FIELD_CUSTOM_INT, max_field_keys[i], i * 1000);
    }
    log_record_commit(&rec);
    log_record_format(&rec, output, sizeof(output));
    printf("   %s", output);
}
/* ============================================================================
 * Benchmark: Timestamp Caching Performance
 * ============================================================================ */
static void bench_timestamp_cache(void) {
    printf("\n=== Benchmark: Timestamp Caching ===\n");
    char buf[256];
    int iterations = 1000000;
    /* Get current time */
    uint64_t base_ns = get_timestamp_ns();
    /* Benchmark 1: Same second (cache hit) */
    uint64_t start = get_timestamp_ns();
    for (int i = 0; i < iterations; i++) {
        /* Same second, different microseconds */
        uint64_t ts = base_ns + (i * 1000);  /* +1 microsecond each */
        log_record rec = {0};
        rec.timestamp_ns = ts;
        rec.level = LOG_LEVEL_INFO;
        rec.fmt = "test";
        log_record_format(&rec, buf, sizeof(buf));
    }
    uint64_t same_sec_time = get_timestamp_ns() - start;
    /* Benchmark 2: Different seconds (cache miss) */
    start = get_timestamp_ns();
    for (int i = 0; i < iterations; i++) {
        /* Different second each time */
        uint64_t ts = base_ns + ((uint64_t)i * 1000000000ULL);  /* +1 second each */
        log_record rec = {0};
        rec.timestamp_ns = ts;
        rec.level = LOG_LEVEL_INFO;
        rec.fmt = "test";
        log_record_format(&rec, buf, sizeof(buf));
    }
    uint64_t diff_sec_time = get_timestamp_ns() - start;
    printf("  Same second (cache hit):  %.2f ns/call\n", (double)same_sec_time / iterations);
    printf("  Diff seconds (cache miss): %.2f ns/call\n", (double)diff_sec_time / iterations);
    printf("  Cache speedup: %.2fx\n", (double)diff_sec_time / same_sec_time);
}
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("===========================================\n");
    printf("   xlog log_record Test Suite\n");
    printf("===========================================\n");
    test_struct_sizes();
    test_basic_record();
    test_context_record();
    test_custom_fields();
    test_multiple_arg_types();
    test_dynamic_string();
    test_custom_format();
    test_optional_fields();
    test_location_options();
    test_inline_format();
    test_performance_comparison();
    /* Edge and error tests */
    test_null_and_empty();
    test_long_messages();
    test_argument_boundaries();
    test_custom_field_boundaries();
    test_inline_buffer_boundaries();
    test_numeric_boundaries();
    test_log_level_boundaries();
    test_timestamp_boundaries();
    test_special_format_chars();
    test_record_state();
    test_complex_scenarios();
    /* Performance benchmark */
    bench_timestamp_cache();
    printf("\n===========================================\n");
    printf("   All tests completed!\n");
    printf("===========================================\n");
    return 0;
}
