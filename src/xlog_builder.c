/* =====================================================================================
 *       Filename:  xlog_builder.c
 *    Description:  Unified configuration API implementation
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#include "xlog_builder.h"
#include "xlog_core.h"
#include "console_sink.h"
#include "file_sink.h"
#include "syslog_sink.h"
#include "color.h"
#include "platform.h"  /* includes unistd.h on POSIX, provides compat on Windows */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Default Configuration
 * ============================================================================ */

#ifdef _MSC_VER
/* MSVC doesn't support designated initializers in C mode - use init function */
static xlog_builder g_default_config;
static atomic_int g_default_config_initialized = 0;

static void init_default_config(void)
{
    if (atomic_load(&g_default_config_initialized)) return;
    memset(&g_default_config, 0, sizeof(g_default_config));
    g_default_config.app_name = "xlog";
    g_default_config.global_level = XLOG_LEVEL_DEBUG;
    g_default_config.mode = XLOG_MODE_ASYNC;
    g_default_config.ring_buffer_size = 8192;
    /* format */
    g_default_config.format.style = XLOG_FORMAT_DEFAULT;
    g_default_config.format.show_timestamp = true;
    g_default_config.format.show_level = true;
    g_default_config.format.show_thread_id = true;
    g_default_config.format.show_file_line = true;
    g_default_config.format.show_function = false;
    g_default_config.format.show_module = false;
    g_default_config.format.show_tag = false;
    g_default_config.format.show_trace_id = false;
    g_default_config.format.timestamp_format = NULL;
    g_default_config.format.custom_pattern = NULL;
    /* console */
    g_default_config.console.enabled = true;
    g_default_config.console.level = XLOG_LEVEL_DEBUG;
    g_default_config.console.target = XLOG_CONSOLE_STDOUT;
    g_default_config.console.color_mode = XLOG_COLOR_AUTO;
    g_default_config.console.flush_on_write = true;
    /* file */
    g_default_config.file.enabled = false;
    g_default_config.file.level = XLOG_LEVEL_DEBUG;
    g_default_config.file.directory = "./logs";
    g_default_config.file.base_name = "app";
    g_default_config.file.extension = ".log";
    g_default_config.file.max_file_size = 50 * XLOG_1MB;
    g_default_config.file.max_dir_size = 500 * XLOG_1MB;
    g_default_config.file.max_files = 100;
    g_default_config.file.rotate_on_start = true;
    g_default_config.file.flush_on_write = false;
    g_default_config.file.compress_old = false;
    /* syslog */
    g_default_config.syslog.enabled = false;
    g_default_config.syslog.level = XLOG_LEVEL_INFO;
    g_default_config.syslog.ident = NULL;
    g_default_config.syslog.facility = XLOG_SYSLOG_USER;
    g_default_config.syslog.include_pid = true;
    g_default_config._initialized = false;
    atomic_store(&g_default_config_initialized, 1);
}
#define ENSURE_DEFAULT_CONFIG() do { if (!atomic_load(&g_default_config_initialized)) init_default_config(); } while(0)
#else
/* GCC/Clang - use designated initializers */
static xlog_builder g_default_config =
		{
				.app_name = "xlog",
				.global_level = XLOG_LEVEL_DEBUG,
				.mode = XLOG_MODE_ASYNC,
				.ring_buffer_size = 8192,
				.format =
						{
								.style = XLOG_FORMAT_DEFAULT,
								.show_timestamp = true,
								.show_level = true,
								.show_thread_id = true,
								.show_file_line = true,
								.show_function = false,
								.show_module = false,
								.show_tag = false,
								.show_trace_id = false,
								.timestamp_format = NULL,
								.custom_pattern = NULL
						},
				.console =
						{
								.enabled = true,
								.level = XLOG_LEVEL_DEBUG,
								.target = XLOG_CONSOLE_STDOUT,
								.color_mode = XLOG_COLOR_AUTO,
								.flush_on_write = true
						},
				.file =
						{
								.enabled = false,
								.level = XLOG_LEVEL_DEBUG,
								.directory = "./logs",
								.base_name = "app",
								.extension = ".log",
								.max_file_size = 50 * XLOG_1MB,
								.max_dir_size = 500 * XLOG_1MB,
								.max_files = 100,
								.rotate_on_start = true,
								.flush_on_write = false,
								.compress_old = false
						},
				.syslog =
						{
								.enabled = false,
								.level = XLOG_LEVEL_INFO,
								.ident = NULL,
								.facility = XLOG_SYSLOG_USER,
								.include_pid = true
						},
				._initialized = false
		};
#define ENSURE_DEFAULT_CONFIG() ((void)0)
#endif

/* ============================================================================
 * Builder API Implementation
 * ============================================================================ */

xlog_builder *xlog_builder_new(void)
{
	ENSURE_DEFAULT_CONFIG();
	xlog_builder *cfg = malloc(sizeof(xlog_builder));
	if (cfg)
	{
		*cfg = g_default_config;
	}
	return cfg;
}

void xlog_builder_free(xlog_builder *cfg)
{
	if (cfg)
	{
		free(cfg);
	}
}

/* --- Global Settings --- */

xlog_builder *xlog_builder_set_name(xlog_builder *cfg, const char *name)
{
	if (cfg)
	{
		cfg->app_name = name;
	}
	return cfg;
}

xlog_builder *xlog_builder_set_level(xlog_builder *cfg, xlog_level level)
{
	if (cfg)
	{
		cfg->global_level = level;
	}
	return cfg;
}

xlog_builder *xlog_builder_set_mode(xlog_builder *cfg, xlog_mode mode)
{
	if (cfg)
	{
		cfg->mode = mode;
	}
	return cfg;
}

xlog_builder *xlog_builder_set_buffer_size(xlog_builder *cfg, uint32_t size)
{
	if (cfg)
	{
		cfg->ring_buffer_size = size;
	}
	return cfg;
}

/* --- Format Settings --- */

xlog_builder *xlog_builder_set_format(xlog_builder *cfg, xlog_format_style style)
{
	if (cfg)
	{
		cfg->format.style = style;
	}
	return cfg;
}

xlog_builder *xlog_builder_show_timestamp(xlog_builder *cfg, bool show)
{
	if (cfg)
	{
		cfg->format.show_timestamp = show;
	}
	return cfg;
}

xlog_builder *xlog_builder_show_level(xlog_builder *cfg, bool show)
{
	if (cfg)
	{
		cfg->format.show_level = show;
	}
	return cfg;
}

xlog_builder *xlog_builder_show_thread_id(xlog_builder *cfg, bool show)
{
	if (cfg)
	{
		cfg->format.show_thread_id = show;
	}
	return cfg;
}

xlog_builder *xlog_builder_show_file_line(xlog_builder *cfg, bool show)
{
	if (cfg)
	{
		cfg->format.show_file_line = show;
	}
	return cfg;
}

xlog_builder *xlog_builder_show_function(xlog_builder *cfg, bool show)
{
	if (cfg)
	{
		cfg->format.show_function = show;
	}
	return cfg;
}

xlog_builder *xlog_builder_show_module(xlog_builder *cfg, bool show)
{
	if (cfg)
	{
		cfg->format.show_module = show;
	}
	return cfg;
}

xlog_builder *xlog_builder_show_tag(xlog_builder *cfg, bool show)
{
	if (cfg)
	{
		cfg->format.show_tag = show;
	}
	return cfg;
}

/* --- Console Sink Settings --- */

xlog_builder *xlog_builder_enable_console(xlog_builder *cfg, bool enable)
{
	if (cfg)
	{
		cfg->console.enabled = enable;
	}
	return cfg;
}

xlog_builder *xlog_builder_console_level(xlog_builder *cfg, xlog_level level)
{
	if (cfg)
	{
		cfg->console.level = level;
	}
	return cfg;
}

xlog_builder *xlog_builder_console_target(xlog_builder *cfg, xlog_console_target target)
{
	if (cfg)
	{
		cfg->console.target = target;
	}
	return cfg;
}

xlog_builder *xlog_builder_console_color(xlog_builder *cfg, xlog_color_mode mode)
{
	if (cfg)
	{
		cfg->console.color_mode = mode;
	}
	return cfg;
}

xlog_builder *xlog_builder_console_flush(xlog_builder *cfg, bool flush)
{
	if (cfg)
	{
		cfg->console.flush_on_write = flush;
	}
	return cfg;
}

/* --- File Sink Settings --- */

xlog_builder *xlog_builder_enable_file(xlog_builder *cfg, bool enable)
{
	if (cfg)
	{
		cfg->file.enabled = enable;
	}
	return cfg;
}

xlog_builder *xlog_builder_file_level(xlog_builder *cfg, xlog_level level)
{
	if (cfg)
	{
		cfg->file.level = level;
	}
	return cfg;
}

xlog_builder *xlog_builder_file_directory(xlog_builder *cfg, const char *dir)
{
	if (cfg)
	{
		cfg->file.directory = dir;
	}
	return cfg;
}

xlog_builder *xlog_builder_file_name(xlog_builder *cfg, const char *name)
{
	if (cfg)
	{
		cfg->file.base_name = name;
	}
	return cfg;
}

xlog_builder *xlog_builder_file_extension(xlog_builder *cfg, const char *ext)
{
	if (cfg)
	{
		cfg->file.extension = ext;
	}
	return cfg;
}

xlog_builder *xlog_builder_file_max_size(xlog_builder *cfg, uint64_t size)
{
	if (cfg)
	{
		cfg->file.max_file_size = size;
	}
	return cfg;
}

xlog_builder *xlog_builder_file_max_dir_size(xlog_builder *cfg, uint64_t size)
{
	if (cfg)
	{
		cfg->file.max_dir_size = size;
	}
	return cfg;
}

xlog_builder *xlog_builder_file_max_files(xlog_builder *cfg, uint32_t count)
{
	if (cfg)
	{
		cfg->file.max_files = count;
	}
	return cfg;
}

xlog_builder *xlog_builder_file_rotate_on_start(xlog_builder *cfg, bool rotate)
{
	if (cfg)
	{
		cfg->file.rotate_on_start = rotate;
	}
	return cfg;
}

xlog_builder *xlog_builder_file_flush(xlog_builder *cfg, bool flush)
{
	if (cfg)
	{
		cfg->file.flush_on_write = flush;
	}
	return cfg;
}

xlog_builder *xlog_builder_file_compress(xlog_builder *cfg, bool compress)
{
	if (cfg)
	{
		cfg->file.compress_old = compress;
	}
	return cfg;
}

/* --- Syslog Sink Settings --- */

xlog_builder *xlog_builder_enable_syslog(xlog_builder *cfg, bool enable)
{
	if (cfg)
	{
		cfg->syslog.enabled = enable;
	}
	return cfg;
}

xlog_builder *xlog_builder_syslog_level(xlog_builder *cfg, xlog_level level)
{
	if (cfg)
	{
		cfg->syslog.level = level;
	}
	return cfg;
}

xlog_builder *xlog_builder_syslog_ident(xlog_builder *cfg, const char *ident)
{
	if (cfg)
	{
		cfg->syslog.ident = ident;
	}
	return cfg;
}

xlog_builder *xlog_builder_syslog_facility(xlog_builder *cfg, xlog_syslog_facility facility)
{
	if (cfg)
	{
		cfg->syslog.facility = facility;
	}
	return cfg;
}

xlog_builder *xlog_builder_syslog_pid(xlog_builder *cfg, bool include)
{
	if (cfg)
	{
		cfg->syslog.include_pid = include;
	}
	return cfg;
}

/* ============================================================================
 * Apply Configuration
 * ============================================================================ */

bool xlog_builder_apply(xlog_builder *cfg)
{
	if (!cfg)
	{
		return false;
	}

	/* Initialize xlog core */
	if (!xlog_init())
	{
		return false;
	}

	/* Set global level */
	xlog_set_level(cfg->global_level);

	/* Note: sync mode is handled by the async parameter in xlog_config */
	/* TODO: Add xlog_set_sync_mode() API if needed */

	/* Initialize color system */
	xlog_color_init();

	/* Determine if console colors should be used */
	bool use_console_colors = false;

	/* Add console sink if enabled */
	if (cfg->console.enabled)
	{
		console_sink_config console_cfg =
				{
						.target = (console_target) cfg->console.target,
						.use_colors = (cfg->console.color_mode != XLOG_COLOR_NEVER),
						.flush_on_write = cfg->console.flush_on_write
				};

		sink_t *console = console_sink_create(&console_cfg, cfg->console.level);
		if (console)
		{
			xlog_add_sink(console);

			/* Set color mode */
			if (cfg->console.color_mode == XLOG_COLOR_ALWAYS)
			{
				xlog_color_set_mode(XLOG_COLOR_ALWAYS);
				use_console_colors = true;
			}
			else if (cfg->console.color_mode == XLOG_COLOR_NEVER)
			{
				xlog_color_set_mode(XLOG_COLOR_NEVER);
				use_console_colors = false;
			}
			else
			{
				/* AUTO mode - check if stdout is a TTY */
				use_console_colors = xlog_color_supported(STDOUT_FILENO);
			}
		}
	}

	/* Set console colors flag for formatting */
	xlog_set_console_colors(use_console_colors);


	/* Add file sink if enabled */
	bool has_file = false;
	if (cfg->file.enabled)
	{
		/* Create directory if needed */
		xlog_mkdir_p(cfg->file.directory);

		file_sink_config file_cfg =
				{
						.directory = cfg->file.directory,
						.base_name = cfg->file.base_name,
						.extension = cfg->file.extension,
						.max_file_size = cfg->file.max_file_size,
						.max_dir_size = cfg->file.max_dir_size,
						.max_files = cfg->file.max_files,
						.rotate_on_start = cfg->file.rotate_on_start,
						.flush_on_write = cfg->file.flush_on_write,
						.compress_old = cfg->file.compress_old
				};

		sink_t *file = file_sink_create(&file_cfg, cfg->file.level);
		if (file)
		{
			xlog_add_sink(file);
			has_file = true;
		}
	}

	/* Set file sink flag for split formatting */
	xlog_set_has_file_sink(has_file);

	/* Set format style */
	xlog_output_format internal_style;
	switch (cfg->format.style)
	{
		case XLOG_FORMAT_JSON:
			internal_style = XLOG_OUTPUT_JSON;
			break;
		case XLOG_FORMAT_RAW:
			internal_style = XLOG_OUTPUT_RAW;
			break;
		case XLOG_FORMAT_SIMPLE:
			internal_style = XLOG_OUTPUT_SIMPLE;
			break;
		case XLOG_FORMAT_DETAILED:
			internal_style = XLOG_OUTPUT_DETAILED;
			break;
		case XLOG_FORMAT_DEFAULT:
		default:
			internal_style = XLOG_OUTPUT_DEFAULT;
			break;
	}
	xlog_set_format_style(internal_style);

	/* Add syslog sink if enabled */
#if XLOG_HAS_SYSLOG
	if (cfg->syslog.enabled)
	{
		syslog_sink_config syslog_cfg =
				{
						.ident = cfg->syslog.ident ? cfg->syslog.ident : cfg->app_name,
						.facility = (syslog_facility) cfg->syslog.facility,
						.include_pid = cfg->syslog.include_pid,
						.log_to_stderr = false,
						.log_perror = false
				};

		sink_t *syslog = syslog_sink_create(&syslog_cfg, cfg->syslog.level);
		if (syslog)
		{
			xlog_add_sink(syslog);
		}
	}
#endif

	cfg->_initialized = true;
	return true;
}

/* ============================================================================
 * Configuration Dump
 * ============================================================================ */

int xlog_builder_dump(const xlog_builder *cfg, char *buffer, size_t size)
{
	if (!cfg || !buffer || size == 0)
	{
		return -1;
	}

	static const char *level_names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
	static const char *mode_names[] = {"ASYNC", "SYNC"};
	static const char *color_names[] = {"AUTO", "ON", "OFF"};
	static const char *target_names[] = {"stdout", "stderr"};

	int n = snprintf(buffer, size,
	                 "xlog Configuration:\n"
	                 "  App Name:      %s\n"
	                 "  Global Level:  %s\n"
	                 "  Mode:          %s\n"
	                 "  Buffer Size:   %u slots\n"
	                 "\n"
	                 "  Console Sink:\n"
	                 "    Enabled:     %s\n"
	                 "    Level:       %s\n"
	                 "    Target:      %s\n"
	                 "    Colors:      %s\n"
	                 "    Flush:       %s\n"
	                 "\n"
	                 "  File Sink:\n"
	                 "    Enabled:     %s\n"
	                 "    Level:       %s\n"
	                 "    Directory:   %s\n"
	                 "    Base Name:   %s\n"
	                 "    Extension:   %s\n"
	                 "    Max Size:    %lu MB\n"
	                 "    Max Dir:     %lu MB\n"
	                 "    Max Files:   %u\n"
	                 "    Rotate:      %s\n"
	                 "\n"
	                 "  Syslog Sink:\n"
	                 "    Enabled:     %s\n"
	                 "    Level:       %s\n"
	                 "    Ident:       %s\n"
	                 "    Facility:    %d\n"
	                 "    Include PID: %s\n",
	                 cfg->app_name ? cfg->app_name : "(null)",
	                 level_names[cfg->global_level],
	                 mode_names[cfg->mode],
	                 cfg->ring_buffer_size,
	                 cfg->console.enabled ? "yes" : "no",
	                 level_names[cfg->console.level],
	                 target_names[cfg->console.target],
	                 color_names[cfg->console.color_mode],
	                 cfg->console.flush_on_write ? "yes" : "no",
	                 cfg->file.enabled ? "yes" : "no",
	                 level_names[cfg->file.level],
	                 cfg->file.directory ? cfg->file.directory : "(null)",
	                 cfg->file.base_name ? cfg->file.base_name : "(null)",
	                 cfg->file.extension ? cfg->file.extension : "(null)",
	                 (unsigned long) (cfg->file.max_file_size / XLOG_1MB),
	                 (unsigned long) (cfg->file.max_dir_size / XLOG_1MB),
	                 cfg->file.max_files,
	                 cfg->file.rotate_on_start ? "yes" : "no",
	                 cfg->syslog.enabled ? "yes" : "no",
	                 level_names[cfg->syslog.level],
	                 cfg->syslog.ident ? cfg->syslog.ident : "(null)",
	                 cfg->syslog.facility,
	                 cfg->syslog.include_pid ? "yes" : "no"
	);

	return n;
}

/* ============================================================================
 * Quick Setup API
 * ============================================================================ */

bool xlog_init_console(xlog_level level)
{
	xlog_builder *cfg = xlog_builder_new();
	if (!cfg)
	{
		return false;
	}

	xlog_builder_set_level(cfg, level);
	xlog_builder_enable_console(cfg, true);
	xlog_builder_console_level(cfg, level);
	xlog_builder_enable_file(cfg, false);
	xlog_builder_enable_syslog(cfg, false);

	bool result = xlog_builder_apply(cfg);
	xlog_builder_free(cfg);
	return result;
}

bool xlog_init_file(const char *directory, const char *name, xlog_level level)
{
	xlog_builder *cfg = xlog_builder_new();
	if (!cfg)
	{
		return false;
	}

	xlog_builder_set_name(cfg, name);
	xlog_builder_set_level(cfg, level);
	xlog_builder_enable_console(cfg, true);
	xlog_builder_console_level(cfg, level);
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_level(cfg, level);
	xlog_builder_file_directory(cfg, directory);
	xlog_builder_file_name(cfg, name);
	xlog_builder_enable_syslog(cfg, false);

	bool result = xlog_builder_apply(cfg);
	xlog_builder_free(cfg);
	return result;
}

bool xlog_init_full(const char *directory, const char *name, xlog_level level)
{
	xlog_builder *cfg = xlog_builder_new();
	if (!cfg)
	{
		return false;
	}

	xlog_builder_set_name(cfg, name);
	xlog_builder_set_level(cfg, level);
	xlog_builder_enable_console(cfg, true);
	xlog_builder_console_level(cfg, level);
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_level(cfg, level);
	xlog_builder_file_directory(cfg, directory);
	xlog_builder_file_name(cfg, name);
	xlog_builder_enable_syslog(cfg, true);
	xlog_builder_syslog_ident(cfg, name);
	xlog_builder_syslog_level(cfg, level);

	bool result = xlog_builder_apply(cfg);
	xlog_builder_free(cfg);
	return result;
}

bool xlog_init_daemon(const char *directory, const char *name, xlog_level level)
{
	xlog_builder *cfg = xlog_builder_new();
	if (!cfg)
	{
		return false;
	}

	xlog_builder_set_name(cfg, name);
	xlog_builder_set_level(cfg, level);
	xlog_builder_enable_console(cfg, false);  /* No console for daemons */
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_level(cfg, level);
	xlog_builder_file_directory(cfg, directory);
	xlog_builder_file_name(cfg, name);
	xlog_builder_enable_syslog(cfg, true);
	xlog_builder_syslog_ident(cfg, name);
	xlog_builder_syslog_level(cfg, level);
	xlog_builder_syslog_facility(cfg, XLOG_SYSLOG_DAEMON);

	bool result = xlog_builder_apply(cfg);
	xlog_builder_free(cfg);
	return result;
}

/* ============================================================================
 * Preset Configurations
 * ============================================================================ */

xlog_builder *xlog_preset_development(void)
{
	xlog_builder *cfg = xlog_builder_new();
	if (!cfg)
	{
		return NULL;
	}

	/* Development: verbose console, no file */
	xlog_builder_set_name(cfg, "dev");
	xlog_builder_set_level(cfg, XLOG_LEVEL_DEBUG);
	xlog_builder_set_mode(cfg, XLOG_MODE_ASYNC);

	/* Console: colorful, detailed */
	xlog_builder_enable_console(cfg, true);
	xlog_builder_console_level(cfg, XLOG_LEVEL_DEBUG);
	xlog_builder_console_color(cfg, XLOG_COLOR_ALWAYS);
	xlog_builder_console_target(cfg, XLOG_CONSOLE_STDOUT);

	/* Format: show everything for debugging */
	xlog_builder_set_format(cfg, XLOG_FORMAT_DETAILED);
	xlog_builder_show_file_line(cfg, true);
	xlog_builder_show_function(cfg, true);

	/* No file or syslog */
	xlog_builder_enable_file(cfg, false);
	xlog_builder_enable_syslog(cfg, false);

	return cfg;
}

xlog_builder *xlog_preset_production(const char *log_dir, const char *app_name)
{
	xlog_builder *cfg = xlog_builder_new();
	if (!cfg)
	{
		return NULL;
	}

	/* Production: file only, INFO level */
	xlog_builder_set_name(cfg, app_name);
	xlog_builder_set_level(cfg, XLOG_LEVEL_INFO);
	xlog_builder_set_mode(cfg, XLOG_MODE_ASYNC);

	/* No console in production */
	xlog_builder_enable_console(cfg, false);

	/* File: standard sizes */
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_level(cfg, XLOG_LEVEL_INFO);
	xlog_builder_file_directory(cfg, log_dir);
	xlog_builder_file_name(cfg, app_name);
	xlog_builder_file_max_size(cfg, 50 * XLOG_1MB);
	xlog_builder_file_max_dir_size(cfg, 500 * XLOG_1MB);
	xlog_builder_file_max_files(cfg, 100);

	/* Syslog for critical errors */
	xlog_builder_enable_syslog(cfg, true);
	xlog_builder_syslog_level(cfg, XLOG_LEVEL_ERROR);
	xlog_builder_syslog_ident(cfg, app_name);

	/* Format: simple for production */
	xlog_builder_set_format(cfg, XLOG_FORMAT_DEFAULT);
	xlog_builder_show_file_line(cfg, false);

	return cfg;
}

xlog_builder *xlog_preset_testing(const char *log_dir)
{
	xlog_builder *cfg = xlog_builder_new();
	if (!cfg)
	{
		return NULL;
	}

	/* Testing: verbose, small files */
	xlog_builder_set_name(cfg, "test");
	xlog_builder_set_level(cfg, XLOG_LEVEL_TRACE);
	xlog_builder_set_mode(cfg, XLOG_MODE_SYNC);  /* Sync for predictable testing */

	/* Console: all levels */
	xlog_builder_enable_console(cfg, true);
	xlog_builder_console_level(cfg, XLOG_LEVEL_TRACE);
	xlog_builder_console_color(cfg, XLOG_COLOR_ALWAYS);

	/* File: small sizes for quick testing */
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_level(cfg, XLOG_LEVEL_TRACE);
	xlog_builder_file_directory(cfg, log_dir);
	xlog_builder_file_name(cfg, "test");
	xlog_builder_file_max_size(cfg, 1 * XLOG_1MB);
	xlog_builder_file_max_dir_size(cfg, 10 * XLOG_1MB);
	xlog_builder_file_max_files(cfg, 5);

	/* No syslog in testing */
	xlog_builder_enable_syslog(cfg, false);

	/* Format: detailed */
	xlog_builder_set_format(cfg, XLOG_FORMAT_DETAILED);

	return cfg;
}

