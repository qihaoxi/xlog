/* =====================================================================================
 *       Filename:  test_single_header.c
 *    Description:  Minimal verification for single-header build
 * =====================================================================================
 */
#define XLOG_IMPLEMENTATION
#include "xlog.h"

#include <stdio.h>

int main(void) {
    printf("=== xlog Single Header Verification ===\n\n");

    printf("Test 1: xlog_init_console... ");
    if (!xlog_init_console(LOG_LEVEL_DEBUG)) {
        printf("FAILED\n");
        return 1;
    }
    printf("OK\n");

    printf("Test 2: XLOG_* macros... ");
    XLOG_DEBUG("Debug message");
    XLOG_INFO("Info message: %d", 42);
    XLOG_WARN("Warning message");
    XLOG_ERROR("Error message");
    printf("OK\n");

    xlog_flush();
    xlog_shutdown();

    printf("Test 3: Builder API... ");
    xlog_builder *cfg = xlog_builder_new();
    if (!cfg) {
        printf("FAILED (builder_new)\n");
        return 1;
    }
    xlog_builder_set_name(cfg, "verify_test");
    xlog_builder_enable_console(cfg, true);
    xlog_builder_set_format(cfg, XLOG_FORMAT_JSON);
    if (!xlog_builder_apply(cfg)) {
        printf("FAILED (apply)\n");
        return 1;
    }
    XLOG_INFO("Builder API works!");
    xlog_flush();
    xlog_builder_free(cfg);
    xlog_shutdown();
    printf("OK\n");

    printf("\n=== All Tests Passed! ===\n");
    printf("Single header version works correctly.\n");
    printf("\nNote: Use XLOG_* macros (not LOG_*) to avoid syslog.h conflicts.\n");
    return 0;
}
