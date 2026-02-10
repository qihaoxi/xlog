/* =====================================================================================
 *       Filename:  example_simple.c
 *    Description:  Simple example showing how to use xlog
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc myapp.c -lxlog -lpthread
 * =====================================================================================
 *
 * This example demonstrates the simplest way to use xlog.
 * Only include/xlog.h and libxlog.a are needed.
 *
 * Compile:
 *   gcc -I/path/to/xlog/include -L/path/to/xlog/lib example_simple.c -lxlog -lpthread -o example
 *
 */

#include <xlog.h>

int main(void) {
    /* Initialize with console output, DEBUG level */
    xlog_init_console(LOG_LEVEL_DEBUG);

    /* Use LOG_* macros (legacy compatible) */
    LOG_DEBUG("Application starting...");
    LOG_INFO("Processing %d items", 42);
    LOG_WARN("This is a warning");
    LOG_ERROR("Error code: %d", -1);

    /* Or use XLOG_* macros (new style) */
    XLOG_INFO("Using XLOG_INFO macro");

    /* Conditional logging */
    int verbose = 1;
    LOG_DEBUG_IF(verbose, "Verbose mode enabled");

    /* Cleanup */
    xlog_shutdown();

    return 0;
}

