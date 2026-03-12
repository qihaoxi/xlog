/* =====================================================================================
 *       Filename:  test_syslog_sink.c
 *    Description:  Test cases for syslog sink
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "syslog_sink.h"
#include "xlog_core.h"

#if XLOG_HAS_SYSLOG

static void test_syslog_creation(void) {
    printf("\n=== Test: Syslog Sink Creation ===\n");

    /* Test default creation */
    sink_t *sink = syslog_sink_create_default("xlog_test", XLOG_LEVEL_DEBUG);
    if (sink) {
        printf("  ✓ Created default syslog sink\n");
        sink_destroy(sink);
    } else {
        printf("  ✗ Failed to create default syslog sink\n");
    }

    /* Test daemon creation */
    sink = syslog_sink_create_daemon("xlog_daemon", XLOG_LEVEL_INFO);
    if (sink) {
        printf("  ✓ Created daemon syslog sink\n");
        sink_destroy(sink);
    } else {
        printf("  ✗ Failed to create daemon syslog sink\n");
    }

    /* Test with custom facility */
    sink = syslog_sink_create_with_facility("xlog_local", SYSLOG_FACILITY_LOCAL0, XLOG_LEVEL_DEBUG);
    if (sink) {
        printf("  ✓ Created syslog sink with LOCAL0 facility\n");
        sink_destroy(sink);
    } else {
        printf("  ✗ Failed to create syslog sink with custom facility\n");
    }

    /* Test with full config */
    syslog_sink_config config = {
        .ident = "xlog_full",
        .facility = SYSLOG_FACILITY_LOCAL1,
        .include_pid = true,
        .log_to_stderr = false,
        .log_perror = false
    };
    sink = syslog_sink_create(&config, XLOG_LEVEL_DEBUG);
    if (sink) {
        printf("  ✓ Created syslog sink with full config\n");
        sink_destroy(sink);
    } else {
        printf("  ✗ Failed to create syslog sink with full config\n");
    }
}

static void test_syslog_logging(void) {
    printf("\n=== Test: Syslog Logging ===\n");

    if (!xlog_init()) {
        printf("  ✗ Failed to initialize xlog\n");
        return;
    }

    /* Create syslog sink */
    sink_t *syslog = syslog_sink_create_default("xlog_test", XLOG_LEVEL_DEBUG);
    if (!syslog) {
        printf("  ✗ Failed to create syslog sink\n");
        xlog_shutdown();
        return;
    }

    if (!xlog_add_sink(syslog)) {
        printf("  ✗ Failed to add syslog sink\n");
        sink_destroy(syslog);
        xlog_shutdown();
        return;
    }

    printf("  ✓ Syslog sink added to xlog\n");

    /* Log some messages */
    printf("  Logging messages to syslog (check /var/log/syslog or journalctl)...\n");

    XLOG_DEBUG("xlog test: DEBUG message");
    XLOG_INFO("xlog test: INFO message with value=%d", 42);
    XLOG_WARN("xlog test: WARNING message");
    XLOG_ERROR("xlog test: ERROR message");

    xlog_flush();

    printf("  ✓ Messages logged to syslog\n");
    printf("  Verify with: journalctl -t xlog_test --since '1 minute ago'\n");
    printf("  Or: grep xlog_test /var/log/syslog | tail -5\n");

    xlog_shutdown();
    printf("  ✓ Test completed\n");
}

static void test_level_mapping(void) {
    printf("\n=== Test: Log Level Mapping ===\n");

    struct {
        xlog_level level;
        const char *name;
        int expected_priority;
    } test_cases[] = {
        {XLOG_LEVEL_TRACE,   "TRACE",   7 /* LOG_DEBUG */ },
        {XLOG_LEVEL_DEBUG,   "DEBUG",   7 /* LOG_DEBUG */ },
        {XLOG_LEVEL_INFO,    "INFO",    6 /* LOG_INFO */ },
        {XLOG_LEVEL_WARNING, "WARNING", 4 /* LOG_WARNING */ },
        {XLOG_LEVEL_ERROR,   "ERROR",   3 /* LOG_ERR */ },
        {XLOG_LEVEL_FATAL,   "FATAL",   2 /* LOG_CRIT */ },
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        int priority = syslog_priority_from_level(test_cases[i].level);
        if (priority == test_cases[i].expected_priority) {
            printf("  ✓ %s -> priority %d\n", test_cases[i].name, priority);
        } else {
            printf("  ✗ %s -> got %d, expected %d\n",
                   test_cases[i].name, priority, test_cases[i].expected_priority);
        }
    }
}

int main(void) {
    printf("============================================\n");
    printf("  Syslog Sink Test Suite\n");
    printf("============================================\n");

    test_syslog_creation();
    test_level_mapping();
    test_syslog_logging();

    printf("\n============================================\n");
    printf("  All tests completed!\n");
    printf("============================================\n");

    return 0;
}

#else /* !XLOG_HAS_SYSLOG */

int main(void) {
    printf("============================================\n");
    printf("  Syslog Sink Test Suite\n");
    printf("============================================\n");
    printf("\n  Syslog is not available on this platform.\n");
    printf("  Skipping all tests.\n");
    printf("\n============================================\n");
    return 0;
}

#endif /* XLOG_HAS_SYSLOG */

