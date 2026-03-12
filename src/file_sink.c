/* =====================================================================================
 *       Filename:  file_sink.c
 *    Description:  File sink implementation using rotate module
 *        Version:  2.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include <stdlib.h>
#include <string.h>
#include "file_sink.h"
#include "platform.h"

/* ============================================================================
 * File Sink Context
 * ============================================================================ */

typedef struct file_sink_ctx
{
	rotate_state rotate;         /* Rotation state */
	bool flush_on_write; /* Flush after each write */
	bool initialized;    /* Initialization flag */
} file_sink_ctx;

/* ============================================================================
 * Sink Interface Implementation
 * ============================================================================ */

static void file_sink_write(sink_t *sink, const char *data, size_t len)
{
	if (!sink || !sink->ctx || !data || len == 0)
	{
		return;
	}

	file_sink_ctx *ctx = (file_sink_ctx *) sink->ctx;
	if (!ctx->initialized)
	{
		return;
	}

	/* Write through rotate module (handles rotation automatically) */
	rotate_write(&ctx->rotate, data, len);

	if (ctx->flush_on_write)
	{
		rotate_flush(&ctx->rotate);
	}
}

static void file_sink_flush(sink_t *sink)
{
	if (!sink || !sink->ctx)
	{
		return;
	}

	file_sink_ctx *ctx = (file_sink_ctx *) sink->ctx;
	if (ctx->initialized)
	{
		rotate_flush(&ctx->rotate);
	}
}

static void file_sink_close(sink_t *sink)
{
	if (!sink || !sink->ctx)
	{
		return;
	}

	file_sink_ctx *ctx = (file_sink_ctx *) sink->ctx;

	if (ctx->initialized)
	{
		rotate_cleanup(&ctx->rotate);
		ctx->initialized = false;
	}

	free(ctx);
	sink->ctx = NULL;
}

/* ============================================================================
 * Helper: Parse path into directory and base name
 * ============================================================================ */

static bool parse_path(const char *path, char *dir_out, size_t dir_size,
                       char *base_out, size_t base_size,
                       char *ext_out, size_t ext_size)
{
	if (!path || !dir_out || !base_out || !ext_out)
	{
		return false;
	}

	/* Find last path separator */
	const char *last_sep = strrchr(path, XLOG_PATH_SEP);
#ifdef XLOG_PLATFORM_WINDOWS
	/* Also check for forward slash on Windows */
	const char *last_fwd = strrchr(path, '/');
	if (last_fwd && (!last_sep || last_fwd > last_sep))
	{
		last_sep = last_fwd;
	}
#endif

	/* Extract directory */
	if (last_sep)
	{
		size_t dir_len = last_sep - path;
		if (dir_len >= dir_size)
		{
			dir_len = dir_size - 1;
		}
		memcpy(dir_out, path, dir_len);
		dir_out[dir_len] = '\0';
	}
	else
	{
		xlog_strncpy(dir_out, ".", dir_size);
	}

	/* Extract filename */
	const char *filename = last_sep ? (last_sep + 1) : path;

	/* Find extension */
	const char *dot = strrchr(filename, '.');

	if (dot && dot != filename)
	{
		/* Has extension */
		size_t base_len = dot - filename;
		if (base_len >= base_size)
		{
			base_len = base_size - 1;
		}
		memcpy(base_out, filename, base_len);
		base_out[base_len] = '\0';

		xlog_strncpy(ext_out, dot, ext_size);
	}
	else
	{
		/* No extension */
		xlog_strncpy(base_out, filename, base_size);
		xlog_strncpy(ext_out, ".log", ext_size);
	}

	return true;
}

/* ============================================================================
 * V2 API Implementation
 * ============================================================================ */

sink_t *file_sink_create(const file_sink_config *config, xlog_level level)
{
	if (!config || !config->directory || !config->base_name)
	{
		return NULL;
	}

	/* Allocate context */
	file_sink_ctx *ctx = calloc(1, sizeof(file_sink_ctx));
	if (!ctx)
	{
		return NULL;
	}

	/* Build rotate config */
	rotate_config rot_config =
			{
					.base_name = config->base_name,
					.extension = config->extension ? config->extension : ".log",
					.directory = config->directory,
					.max_file_size = config->max_file_size > 0 ? config->max_file_size
					                                           : FILE_SINK_V2_DEFAULT_MAX_FILE_SIZE,
					.max_dir_size = config->max_dir_size > 0 ? config->max_dir_size : FILE_SINK_V2_DEFAULT_MAX_DIR_SIZE,
					.max_files = config->max_files > 0 ? config->max_files : FILE_SINK_V2_DEFAULT_MAX_FILES,
					.rotate_on_start = config->rotate_on_start,
					.compress_old = config->compress_old
			};

	/* Initialize rotation */
	if (!rotate_init(&ctx->rotate, &rot_config))
	{
		free(ctx);
		return NULL;
	}

	ctx->flush_on_write = config->flush_on_write;
	ctx->initialized = true;

	/* Create sink */
	sink_t *sink = sink_create(ctx, file_sink_write, file_sink_flush,
	                           file_sink_close, level, SINK_TYPE_FILE);
	if (!sink)
	{
		rotate_cleanup(&ctx->rotate);
		free(ctx);
		return NULL;
	}

	return sink;
}

sink_t *file_sink_create_default(const char *directory, const char *base_name, xlog_level level)
{
	file_sink_config config =
			{
					.directory = directory,
					.base_name = base_name,
					.extension = ".log",
					.max_file_size = FILE_SINK_V2_DEFAULT_MAX_FILE_SIZE,
					.max_dir_size = FILE_SINK_V2_DEFAULT_MAX_DIR_SIZE,
					.max_files = FILE_SINK_V2_DEFAULT_MAX_FILES,
					.rotate_on_start = true,
					.flush_on_write = false
			};
	return file_sink_create(&config, level);
}

sink_t *file_sink_create_with_limits(const char *directory, const char *base_name,
                                     uint64_t max_file_size, uint64_t max_dir_size,
                                     xlog_level level)
{
	file_sink_config config =
			{
					.directory = directory,
					.base_name = base_name,
					.extension = ".log",
					.max_file_size = max_file_size,
					.max_dir_size = max_dir_size,
					.max_files = FILE_SINK_V2_DEFAULT_MAX_FILES,
					.rotate_on_start = true,
					.flush_on_write = false
			};
	return file_sink_create(&config, level);
}

bool file_sink_force_rotate(sink_t *sink)
{
	if (!sink || !sink->ctx)
	{
		return false;
	}

	file_sink_ctx *ctx = (file_sink_ctx *) sink->ctx;
	if (!ctx->initialized)
	{
		return false;
	}

	return rotate_force(&ctx->rotate);
}

const char *file_sink_get_path(sink_t *sink)
{
	if (!sink || !sink->ctx)
	{
		return NULL;
	}

	file_sink_ctx *ctx = (file_sink_ctx *) sink->ctx;
	if (!ctx->initialized)
	{
		return NULL;
	}

	return rotate_get_current_path(&ctx->rotate);
}

uint64_t file_sink_get_size(sink_t *sink)
{
	if (!sink || !sink->ctx)
	{
		return 0;
	}

	file_sink_ctx *ctx = (file_sink_ctx *) sink->ctx;
	if (!ctx->initialized)
	{
		return 0;
	}

	return rotate_get_current_size(&ctx->rotate);
}

void file_sink_get_stats(sink_t *sink, uint64_t *total_rotations,
                         uint64_t *total_bytes, uint64_t *files_deleted)
{
	if (!sink || !sink->ctx)
	{
		return;
	}

	file_sink_ctx *ctx = (file_sink_ctx *) sink->ctx;
	if (!ctx->initialized)
	{
		return;
	}

	if (total_rotations)
	{
		*total_rotations = ctx->rotate.total_rotations;
	}
	if (total_bytes)
	{
		*total_bytes = ctx->rotate.total_bytes_written;
	}
	if (files_deleted)
	{
		*files_deleted = ctx->rotate.files_deleted;
	}
}

/* ============================================================================
 * Convenience API Implementation
 * ============================================================================ */

sink_t *file_sink_create_simple(const char *path, xlog_level level)
{
	if (!path)
	{
		return NULL;
	}

	char directory[256], base_name[64], extension[16];
	if (!parse_path(path, directory, sizeof(directory),
	                base_name, sizeof(base_name), extension, sizeof(extension)))
	{
		return NULL;
	}

	file_sink_config config =
			{
					.directory = directory,
					.base_name = base_name,
					.extension = extension,
					.max_file_size = FILE_SINK_V2_DEFAULT_MAX_FILE_SIZE,
					.max_dir_size = FILE_SINK_V2_DEFAULT_MAX_DIR_SIZE,
					.max_files = FILE_SINK_V2_DEFAULT_MAX_FILES,
					.rotate_on_start = true,
					.flush_on_write = false
			};

	return file_sink_create(&config, level);
}

