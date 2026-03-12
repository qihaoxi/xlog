/* =====================================================================================
 *       Filename:  example_full.c
 *    Description:  Full example showing all xlog features
 *        Version:  1.0
 *        Created:  2026-02-09
 * =====================================================================================
 */

#include <xlog.h>
#include <stdio.h>

int main(void) {
    printf("=== xlog Full Example ===\n\n");

    /* ========================================
     * Method 1: Quick initialization
     * ======================================== */
    printf("--- Method 1: Quick Init ---\n");

    xlog_init_console(XLOG_LEVEL_DEBUG);
    LOG_INFO("Quick console init");
    xlog_shutdown();

    /* ========================================
     * Method 2: Console + File
     * ======================================== */
    printf("\n--- Method 2: Console + File ---\n");

    xlog_init_file("/tmp/xlog_example", "myapp", XLOG_LEVEL_INFO);
    LOG_DEBUG("This won't appear (level is INFO)");
    LOG_INFO("This goes to console and file");
    LOG_ERROR("Errors are important!");
    xlog_shutdown();

    printf("  Check: /tmp/xlog_example/myapp.log\n");

    /* ========================================
     * Method 3: Builder Pattern
     * ======================================== */
    printf("\n--- Method 3: Builder Pattern ---\n");

    xlog_builder *cfg = xlog_builder_new();

    /* Global settings */
    xlog_builder_set_name(cfg, "full_example");
    xlog_builder_set_level(cfg, XLOG_LEVEL_DEBUG);

    /* Console: colorful, to stdout */
    xlog_builder_enable_console(cfg, true);
    xlog_builder_console_color(cfg, XLOG_COLOR_ALWAYS);

    /* File: with rotation */
    xlog_builder_enable_file(cfg, true);
    xlog_builder_file_directory(cfg, "/tmp/xlog_example");
    xlog_builder_file_name(cfg, "builder_test");
    xlog_builder_file_max_size(cfg, 10 * XLOG_1MB);
    xlog_builder_file_max_files(cfg, 5);

    /* Apply and use */
    xlog_builder_apply(cfg);

    LOG_DEBUG("Debug via builder config");
    LOG_INFO("Info via builder config");

    /* Dump config for debugging */
    char dump[2048];
    xlog_builder_dump(cfg, dump, sizeof(dump));
    printf("\nConfiguration:\n%s\n", dump);

    xlog_builder_free(cfg);
    xlog_shutdown();

    /* ========================================
     * Method 4: Presets
     * ======================================== */
    printf("--- Method 4: Presets ---\n");

    /* Development preset */
    xlog_builder *dev = xlog_preset_development();
    xlog_builder_apply(dev);
    LOG_DEBUG("Development mode - verbose");
    LOG_WARN("This is a warning in development mode");
    LOG_ERROR("This is an error in development mode");
    xlog_builder_free(dev);
    xlog_shutdown();

    printf("\n=== Example Complete ===\n");
    return 0;
}

