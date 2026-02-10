/* =====================================================================================
 *       Filename:  test_builder.c
 *    Description:  Test xlog_builder configuration API
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include <stdio.h>
#include <string.h>

/* Use public API header */
#include <xlog.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  ✗ FAILED: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    printf("  ✓ %s\n", msg); \
    tests_passed++; \
} while(0)

/* ============================================================================
 * Test: Basic Builder Creation
 * ============================================================================ */
static void test_builder_creation(void) {
    printf("\n=== Test: Builder Creation ===\n");

    xlog_builder *cfg = xlog_builder_new();
    TEST_ASSERT(cfg != NULL, "Builder creation failed");
    TEST_PASS("Builder created");

    xlog_builder_free(cfg);
    TEST_PASS("Builder freed");
}

/* ============================================================================
 * Test: Chain Style Configuration
 * ============================================================================ */
static void test_chain_style(void) {
    printf("\n=== Test: Chain Style Configuration ===\n");

    xlog_builder *cfg = xlog_builder_new();
    TEST_ASSERT(cfg != NULL, "Builder creation failed");

    /* Chain style calls */
    xlog_builder_set_name(cfg, "test_app");
    xlog_builder_set_level(cfg, LOG_LEVEL_DEBUG);
    xlog_builder_enable_console(cfg, true);
    xlog_builder_console_level(cfg, LOG_LEVEL_DEBUG);
    xlog_builder_console_color(cfg, XLOG_COLOR_ALWAYS);

    TEST_PASS("Chain configuration successful");

    /* Apply and test */
    TEST_ASSERT(xlog_builder_apply(cfg), "Apply configuration failed");
    TEST_PASS("Configuration applied");

    /* Log some messages */
    XLOG_DEBUG("Debug message from chain test");
    XLOG_INFO("Info message with value=%d", 42);

    xlog_flush();
    TEST_PASS("Logging successful");

    xlog_builder_free(cfg);
    xlog_shutdown();
}

/* ============================================================================
 * Test: Quick Setup API
 * ============================================================================ */
static void test_quick_setup(void) {
    printf("\n=== Test: Quick Setup API ===\n");

    /* Console only */
    TEST_ASSERT(xlog_init_console(LOG_LEVEL_INFO), "Console init failed");
    TEST_PASS("Console init successful");

    XLOG_INFO("Quick setup test message");
    xlog_flush();

    xlog_shutdown();
    TEST_PASS("Console shutdown successful");

    /* File logging */
    TEST_ASSERT(xlog_init_file("/tmp/xlog_builder_test", "test", LOG_LEVEL_DEBUG),
                "File init failed");
    TEST_PASS("File init successful");

    XLOG_INFO("File logging test");
    xlog_flush();

    xlog_shutdown();
    TEST_PASS("File shutdown successful");
}

/* ============================================================================
 * Test: Preset Configurations
 * ============================================================================ */
static void test_presets(void) {
    printf("\n=== Test: Preset Configurations ===\n");

    /* Development preset */
    xlog_builder *dev = xlog_preset_development();
    TEST_ASSERT(dev != NULL, "Development preset failed");
    TEST_PASS("Development preset created");

    xlog_builder_apply(dev);
    XLOG_DEBUG("Development mode logging");
    xlog_flush();
    xlog_builder_free(dev);
    xlog_shutdown();

    /* Production preset */
    xlog_builder *prod = xlog_preset_production("/tmp/xlog_builder_test", "prod_test");
    TEST_ASSERT(prod != NULL, "Production preset failed");
    TEST_PASS("Production preset created");

    xlog_builder_apply(prod);
    XLOG_INFO("Production mode logging");
    xlog_flush();
    xlog_builder_free(prod);
    xlog_shutdown();

    /* Testing preset */
    xlog_builder *test = xlog_preset_testing("/tmp/xlog_builder_test");
    TEST_ASSERT(test != NULL, "Testing preset failed");
    TEST_PASS("Testing preset created");

    xlog_builder_apply(test);
    XLOG_TRACE("Testing mode logging");
    xlog_flush();
    xlog_builder_free(test);
    xlog_shutdown();
}

/* ============================================================================
 * Test: Configuration Dump
 * ============================================================================ */
static void test_config_dump(void) {
    printf("\n=== Test: Configuration Dump ===\n");

    xlog_builder *cfg = xlog_builder_new();
    TEST_ASSERT(cfg != NULL, "Builder creation failed");

    xlog_builder_set_name(cfg, "dump_test");
    xlog_builder_set_level(cfg, LOG_LEVEL_INFO);
    xlog_builder_enable_console(cfg, true);
    xlog_builder_enable_file(cfg, true);
    xlog_builder_file_directory(cfg, "/var/log");
    xlog_builder_file_name(cfg, "myapp");
    xlog_builder_file_max_size(cfg, 100 * XLOG_1MB);

    char dump[4096];
    int len = xlog_builder_dump(cfg, dump, sizeof(dump));
    TEST_ASSERT(len > 0, "Dump failed");

    printf("Configuration dump:\n%s\n", dump);
    TEST_PASS("Configuration dump successful");

    xlog_builder_free(cfg);
}

/* ============================================================================
 * Test: File Sink Configuration
 * ============================================================================ */
static void test_file_config(void) {
    printf("\n=== Test: File Sink Configuration ===\n");

    xlog_builder *cfg = xlog_builder_new();
    TEST_ASSERT(cfg != NULL, "Builder creation failed");

    /* Configure file sink */
    xlog_builder_set_name(cfg, "file_test");
    xlog_builder_enable_console(cfg, true);
    xlog_builder_enable_file(cfg, true);
    xlog_builder_file_directory(cfg, "/tmp/xlog_builder_test");
    xlog_builder_file_name(cfg, "rotation_test");
    xlog_builder_file_max_size(cfg, 1 * XLOG_1KB);  /* Small for testing */
    xlog_builder_file_max_files(cfg, 3);
    xlog_builder_file_rotate_on_start(cfg, true);

    TEST_ASSERT(xlog_builder_apply(cfg), "Apply failed");
    TEST_PASS("File configuration applied");

    /* Generate some logs to trigger rotation */
    for (int i = 0; i < 50; i++) {
        XLOG_INFO("Test message %d - some padding text to fill the log", i);
    }

    xlog_flush();
    TEST_PASS("File logging with rotation successful");

    xlog_builder_free(cfg);
    xlog_shutdown();

    printf("  Check /tmp/xlog_builder_test/ for rotated files\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */
int main(void) {
    printf("============================================\n");
    printf("  xlog Builder Configuration Test Suite\n");
    printf("============================================\n");

    test_builder_creation();
    test_chain_style();
    test_quick_setup();
    test_presets();
    test_config_dump();
    test_file_config();

    printf("\n============================================\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("============================================\n");

    return tests_failed > 0 ? 1 : 0;
}

