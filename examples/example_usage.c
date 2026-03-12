/* =====================================================================================
 *       Filename:  example_usage.c
 *    Description:  Complete usage examples for xlog configuration
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include <stdio.h>

/* Use public API only - this is how external users would use xlog */
#include <xlog.h>

/* ============================================================================
 * Example 1: Simplest Usage (One-liner)
 * ============================================================================ */

static void example_simple(void)
{
	printf("\n========================================\n");
	printf("Example 1: Simple Console Logging\n");
	printf("========================================\n");

	/* One-liner: Console only, DEBUG level */
	xlog_init_console(XLOG_LEVEL_DEBUG);

	XLOG_DEBUG("This is a debug message");
	XLOG_INFO("Application started, version %s", "1.0.0");
	XLOG_WARN("This is a warning");
	XLOG_ERROR("Something went wrong: error_code=%d", 42);

	xlog_shutdown();
}

/* ============================================================================
 * Example 2: Console + File Logging
 * ============================================================================ */

static void example_with_file(void)
{
	printf("\n========================================\n");
	printf("Example 2: Console + File Logging\n");
	printf("========================================\n");

	/* One-liner: Console + File */
	xlog_init_file("/tmp/xlog_example", "myapp", XLOG_LEVEL_INFO);

	XLOG_INFO("Logging to console and file");
	XLOG_WARN("This will appear in both outputs");
	XLOG_ERROR("Errors are important!");

	xlog_shutdown();

	printf("  Check /tmp/xlog_example/myapp.log\n");
}

/* ============================================================================
 * Example 3: Builder Pattern (Chain Style)
 * ============================================================================ */

static void example_builder_pattern(void)
{
	printf("\n========================================\n");
	printf("Example 3: Builder Pattern (Chain Style)\n");
	printf("========================================\n");

	/* Create and configure using builder pattern */
	xlog_builder *cfg = xlog_builder_new();

	/* Chain style configuration */
	xlog_builder_set_name(cfg, "my_application");
	xlog_builder_set_level(cfg, XLOG_LEVEL_DEBUG);
	xlog_builder_set_mode(cfg, XLOG_MODE_ASYNC);

	/* Console settings */
	xlog_builder_enable_console(cfg, true);
	xlog_builder_console_level(cfg, XLOG_LEVEL_DEBUG);
	xlog_builder_console_color(cfg, XLOG_COLOR_ALWAYS);
	xlog_builder_console_target(cfg, XLOG_CONSOLE_STDOUT);

	/* File settings */
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_directory(cfg, "/tmp/xlog_example");
	xlog_builder_file_name(cfg, "builder_example");
	xlog_builder_file_level(cfg, XLOG_LEVEL_INFO);
	xlog_builder_file_max_size(cfg, 10 * XLOG_1MB);
	xlog_builder_file_max_files(cfg, 5);

	/* Apply configuration */
	xlog_builder_apply(cfg);

	/* Print configuration summary */
	char dump[2048];
	xlog_builder_dump(cfg, dump, sizeof(dump));
	printf("%s\n", dump);

	/* Use logging */
	XLOG_DEBUG("Debug only on console (file level is INFO)");
	XLOG_INFO("Info on both console and file");
	XLOG_ERROR("Error on both outputs");

	xlog_builder_free(cfg);
	xlog_shutdown();
}

/* ============================================================================
 * Example 4: Full Chain Style (Fluent API)
 * ============================================================================ */

static void example_fluent_chain(void)
{
	printf("\n========================================\n");
	printf("Example 4: Fluent Chain Style\n");
	printf("========================================\n");

	/* Everything in one fluent chain */
	xlog_builder *cfg = xlog_builder_new();

	/* Notice: each function returns cfg, enabling chaining */
	xlog_builder_set_name(
			xlog_builder_set_level(
					xlog_builder_set_mode(cfg, XLOG_MODE_ASYNC),
					XLOG_LEVEL_DEBUG),
			"fluent_app");

	/* Console chain */
	xlog_builder_console_color(
			xlog_builder_console_level(
					xlog_builder_enable_console(cfg, true),
					XLOG_LEVEL_DEBUG),
			XLOG_COLOR_ALWAYS);

	/* File chain */
	xlog_builder_file_max_size(
			xlog_builder_file_name(
					xlog_builder_file_directory(
							xlog_builder_file_level(
									xlog_builder_enable_file(cfg, true),
									XLOG_LEVEL_INFO),
							"/tmp/xlog_example"),
					"fluent"),
			5 * XLOG_1MB);

	xlog_builder_apply(cfg);

	XLOG_INFO("Using fluent chain configuration");

	xlog_builder_free(cfg);
	xlog_shutdown();
}

/* ============================================================================
 * Example 5: Preset Configurations
 * ============================================================================ */

static void example_presets(void)
{
	printf("\n========================================\n");
	printf("Example 5: Preset Configurations\n");
	printf("========================================\n");

	/* Development preset */
	printf("\n--- Development Preset ---\n");
	xlog_builder *dev_cfg = xlog_preset_development();
	xlog_builder_apply(dev_cfg);

	XLOG_DEBUG("Development mode: verbose, colorful");
	XLOG_INFO("All debug info visible");

	xlog_builder_free(dev_cfg);
	xlog_shutdown();

	/* Production preset */
	printf("\n--- Production Preset ---\n");
	xlog_builder *prod_cfg = xlog_preset_production("/tmp/xlog_example", "production_app");
	xlog_builder_apply(prod_cfg);

	XLOG_DEBUG("This won't appear (level is INFO)");
	XLOG_INFO("Production mode: file only");
	XLOG_ERROR("Errors go to file and syslog");

	xlog_builder_free(prod_cfg);
	xlog_shutdown();

	/* Testing preset */
	printf("\n--- Testing Preset ---\n");
	xlog_builder *test_cfg = xlog_preset_testing("/tmp/xlog_test");
	xlog_builder_apply(test_cfg);

	XLOG_TRACE("Testing mode: everything logged");
	XLOG_DEBUG("Small file sizes for quick rotation");

	xlog_builder_free(test_cfg);
	xlog_shutdown();
}

/* ============================================================================
 * Example 6: Daemon/Service Configuration
 * ============================================================================ */

static void example_daemon(void)
{
	printf("\n========================================\n");
	printf("Example 6: Daemon Configuration\n");
	printf("========================================\n");

	/* Daemon mode: no console, file + syslog */
	xlog_init_daemon("/tmp/xlog_daemon", "my_daemon", XLOG_LEVEL_INFO);

	XLOG_INFO("Daemon started");
	XLOG_WARN("This goes to file and syslog only");
	XLOG_ERROR("Critical error, check syslog too");

	xlog_shutdown();

	printf("  Check /tmp/xlog_daemon/my_daemon.log\n");
	printf("  And: journalctl -t my_daemon --since '1 minute ago'\n");
}

/* ============================================================================
 * Example 7: Advanced Configuration
 * ============================================================================ */

static void example_advanced(void)
{
	printf("\n========================================\n");
	printf("Example 7: Advanced Configuration\n");
	printf("========================================\n");

	xlog_builder *cfg = xlog_builder_new();

	/* Global settings */
	xlog_builder_set_name(cfg, "advanced_app");
	xlog_builder_set_level(cfg, XLOG_LEVEL_TRACE);
	xlog_builder_set_mode(cfg, XLOG_MODE_ASYNC);
	xlog_builder_set_buffer_size(cfg, 16384);  /* Larger ring buffer */


	/* Console: errors only, to stderr */
	xlog_builder_enable_console(cfg, true);
	xlog_builder_console_level(cfg, XLOG_LEVEL_ERROR);
	xlog_builder_console_target(cfg, XLOG_CONSOLE_STDERR);
	xlog_builder_console_color(cfg, XLOG_COLOR_ALWAYS);

	/* File: all levels, custom rotation */
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_level(cfg, XLOG_LEVEL_TRACE);
	xlog_builder_file_directory(cfg, "/tmp/xlog_advanced");
	xlog_builder_file_name(cfg, "detailed");
	xlog_builder_file_extension(cfg, ".log");
	xlog_builder_file_max_size(cfg, 100 * XLOG_1MB);
	xlog_builder_file_max_dir_size(cfg, 1 * XLOG_1GB);
	xlog_builder_file_max_files(cfg, 50);
	xlog_builder_file_rotate_on_start(cfg, true);
	xlog_builder_file_flush(cfg, false);

	/* Syslog: warnings and above */
	xlog_builder_enable_syslog(cfg, true);
	xlog_builder_syslog_level(cfg, XLOG_LEVEL_WARNING);
	xlog_builder_syslog_ident(cfg, "advanced_app");
	xlog_builder_syslog_facility(cfg, XLOG_SYSLOG_LOCAL0);
	xlog_builder_syslog_pid(cfg, true);

	/* Dump configuration */
	char dump[4096];
	xlog_builder_dump(cfg, dump, sizeof(dump));
	printf("%s\n", dump);

	/* Apply and use */
	xlog_builder_apply(cfg);

	XLOG_TRACE("Trace: file only");
	XLOG_DEBUG("Debug: file only");
	XLOG_INFO("Info: file only");
	XLOG_WARN("Warn: file + syslog");
	XLOG_ERROR("Error: console + file + syslog");
	XLOG_FATAL("Fatal: all outputs");

	xlog_builder_free(cfg);
	xlog_shutdown();
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	printf("================================================\n");
	printf("       xlog Configuration Examples\n");
	printf("================================================\n");

	example_simple();
	example_with_file();
	example_builder_pattern();
	example_fluent_chain();
	example_presets();
	example_daemon();
	example_advanced();

	printf("\n================================================\n");
	printf("       All examples completed!\n");
	printf("================================================\n");

	return 0;
}

