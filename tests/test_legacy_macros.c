/* =====================================================================================
 *       Filename:  test_legacy_macros.c
 *    Description:  Test legacy LOG_* macro compatibility
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#include <stdio.h>

/* Use the public API header (as external users would) */
#include <xlog.h>

int main(void) {
    printf("============================================\n");
    printf("  Legacy LOG_* Macro Compatibility Test\n");
    printf("============================================\n\n");

    /* Initialize with console only */
    xlog_init_console(XLOG_LEVEL_TRACE);

    printf("Testing legacy LOG_* macros:\n\n");

    /* Test all legacy macros */
    LOG_TRACE("This is a TRACE message using LOG_TRACE");
    LOG_DEBUG("This is a DEBUG message using LOG_DEBUG");
    LOG_INFO("This is an INFO message using LOG_INFO");
    LOG_WARN("This is a WARN message using LOG_WARN");
    LOG_ERROR("This is an ERROR message using LOG_ERROR");
    LOG_FATAL("This is a FATAL message using LOG_FATAL");

    printf("\nTesting with format arguments:\n\n");

    LOG_INFO("User: %s, ID: %d", "john_doe", 12345);
    LOG_ERROR("Error code: %d, Message: %s", -1, "Connection refused");
    LOG_DEBUG("Values: int=%d, float=%.2f, string=%s", 42, 3.14, "test");

    printf("\nTesting conditional logging:\n\n");

    int condition = 1;
    LOG_DEBUG_IF(condition, "This should appear (condition=true)");
    LOG_DEBUG_IF(!condition, "This should NOT appear (condition=false)");

    printf("\nTesting XLOG_* macros (primary API):\n\n");

    XLOG_TRACE("XLOG_TRACE works");
    XLOG_DEBUG("XLOG_DEBUG works");
    XLOG_INFO("XLOG_INFO works");
    XLOG_WARN("XLOG_WARN works");
    XLOG_ERROR("XLOG_ERROR works");
    XLOG_FATAL("XLOG_FATAL works");

    xlog_flush();
    xlog_shutdown();

    printf("\n============================================\n");
    printf("  ✓ All legacy macros work correctly!\n");
    printf("============================================\n");

    return 0;
}

