/* =====================================================================================
 *       Filename:  compress.c
 *    Description:  Log file compression implementation using miniz
 *        Version:  1.0
 *        Created:  2026-03-01
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#include "compress.h"

/* Only compile if compression is enabled */
#ifndef XLOG_NO_COMPRESS

#include "platform.h"

/* Configure miniz for deflate-only (reduces size by ~60%) */
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_INFLATE_APIS
#define MINIZ_NO_ARCHIVE_APIS

#ifdef XLOG_SINGLE_HEADER_H
/* In single-header mode, miniz is already included */
#else

#include "miniz.h"

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * gzip Header/Footer Constants
 * ============================================================================
 * gzip format (RFC 1952):
 *   - 10-byte header
 *   - compressed data (DEFLATE)
 *   - 8-byte footer (CRC32 + original size)
 */

#define GZIP_MAGIC1     0x1f
#define GZIP_MAGIC2     0x8b
#define GZIP_METHOD     8       /* DEFLATE */
#define GZIP_FLAGS      0       /* No extra fields */
#define GZIP_OS_UNIX    3
#define GZIP_OS_WIN     11
#define GZIP_OS_MACOS   7

#ifdef XLOG_PLATFORM_WINDOWS
#define GZIP_OS GZIP_OS_WIN
#elif defined(XLOG_PLATFORM_MACOS)
#define GZIP_OS GZIP_OS_MACOS
#else
#define GZIP_OS GZIP_OS_UNIX
#endif

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/* Async compression task */
struct xlog_compress_task
{
	char src_path[512];
	char dst_path[512];
	int level;
	bool delete_src;

	/* Thread control */
	xlog_thread_t thread;
	volatile int done;
	volatile int cancelled;

	/* Result */
	xlog_compress_error result;
	xlog_compress_stats stats;
};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Write gzip header */
static bool write_gzip_header(FILE *fp, time_t mtime)
{
	uint8_t header[10];

	header[0] = GZIP_MAGIC1;
	header[1] = GZIP_MAGIC2;
	header[2] = GZIP_METHOD;
	header[3] = GZIP_FLAGS;

	/* Modification time (little-endian) */
	header[4] = (uint8_t) (mtime & 0xFF);
	header[5] = (uint8_t) ((mtime >> 8) & 0xFF);
	header[6] = (uint8_t) ((mtime >> 16) & 0xFF);
	header[7] = (uint8_t) ((mtime >> 24) & 0xFF);

	header[8] = 0;          /* Extra flags */
	header[9] = GZIP_OS;    /* Operating system */

	return fwrite(header, 1, 10, fp) == 10;
}

/* Write gzip footer */
static bool write_gzip_footer(FILE *fp, uint32_t crc, uint32_t orig_size)
{
	uint8_t footer[8];

	/* CRC32 (little-endian) */
	footer[0] = (uint8_t) (crc & 0xFF);
	footer[1] = (uint8_t) ((crc >> 8) & 0xFF);
	footer[2] = (uint8_t) ((crc >> 16) & 0xFF);
	footer[3] = (uint8_t) ((crc >> 24) & 0xFF);

	/* Original size mod 2^32 (little-endian) */
	footer[4] = (uint8_t) (orig_size & 0xFF);
	footer[5] = (uint8_t) ((orig_size >> 8) & 0xFF);
	footer[6] = (uint8_t) ((orig_size >> 16) & 0xFF);
	footer[7] = (uint8_t) ((orig_size >> 24) & 0xFF);

	return fwrite(footer, 1, 8, fp) == 8;
}

/* Get current time in microseconds */
static uint64_t get_time_us(void)
{
	return xlog_get_timestamp_ns() / 1000;
}

/* ============================================================================
 * Core Compression Function
 * ============================================================================ */

xlog_compress_error xlog_compress_file(const char *src_path,
                                       const char *dst_path,
                                       int level,
                                       bool delete_src,
                                       xlog_compress_stats *stats)
{
	FILE *fin = NULL;
	FILE *fout = NULL;
	uint8_t *in_buf = NULL;
	uint8_t *out_buf = NULL;
	z_stream stream;
	xlog_compress_error result = XLOG_COMPRESS_OK;
	char dst_buf[512];
	uint32_t crc = MZ_CRC32_INIT;
	uint64_t total_in = 0;
	uint64_t total_out = 0;
	uint64_t start_time;
	int flush;
	int ret;

	/* Validate input */
	if (!src_path)
	{
		return XLOG_COMPRESS_ERR_INVALID;
	}

	/* Generate destination path if not provided */
	if (!dst_path)
	{
		xlog_compress_gen_path(dst_buf, sizeof(dst_buf), src_path);
		dst_path = dst_buf;
	}

	/* Clamp compression level */
	if (level < 0)
	{
		level = 0;
	}
	if (level > 9)
	{
		level = 9;
	}

	/* Start timing */
	start_time = get_time_us();

	/* Allocate buffers */
	in_buf = (uint8_t *) malloc(XLOG_COMPRESS_BUFFER_SIZE);
	out_buf = (uint8_t *) malloc(XLOG_COMPRESS_BUFFER_SIZE);
	if (!in_buf || !out_buf)
	{
		result = XLOG_COMPRESS_ERR_MEMORY;
		goto cleanup;
	}

	/* Open input file */
	fin = fopen(src_path, "rb");
	if (!fin)
	{
		result = XLOG_COMPRESS_ERR_INPUT;
		goto cleanup;
	}

	/* Open output file */
	fout = fopen(dst_path, "wb");
	if (!fout)
	{
		result = XLOG_COMPRESS_ERR_OUTPUT;
		goto cleanup;
	}

	/* Get file modification time */
	time_t mtime = time(NULL);

	/* Write gzip header */
	if (!write_gzip_header(fout, mtime))
	{
		result = XLOG_COMPRESS_ERR_WRITE;
		goto cleanup;
	}
	total_out += 10;

	/* Initialize deflate stream for raw deflate (no zlib header) */
	memset(&stream, 0, sizeof(stream));
	ret = deflateInit2(&stream, level, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS,
	                   9, MZ_DEFAULT_STRATEGY);
	if (ret != MZ_OK)
	{
		result = XLOG_COMPRESS_ERR_COMPRESS;
		goto cleanup;
	}

	/* Compress file in chunks */
	do
	{
		size_t bytes_read = fread(in_buf, 1, XLOG_COMPRESS_BUFFER_SIZE, fin);
		if (ferror(fin))
		{
			deflateEnd(&stream);
			result = XLOG_COMPRESS_ERR_READ;
			goto cleanup;
		}

		/* Update CRC */
		crc = (uint32_t) mz_crc32(crc, in_buf, bytes_read);
		total_in += bytes_read;

		stream.next_in = in_buf;
		stream.avail_in = (mz_uint32) bytes_read;
		flush = feof(fin) ? MZ_FINISH : MZ_NO_FLUSH;

		/* Compress until all input consumed */
		do
		{
			stream.next_out = out_buf;
			stream.avail_out = XLOG_COMPRESS_BUFFER_SIZE;

			ret = deflate(&stream, flush);
			if (ret == MZ_STREAM_ERROR)
			{
				deflateEnd(&stream);
				result = XLOG_COMPRESS_ERR_COMPRESS;
				goto cleanup;
			}

			size_t have = XLOG_COMPRESS_BUFFER_SIZE - stream.avail_out;
			if (have > 0)
			{
				if (fwrite(out_buf, 1, have, fout) != have)
				{
					deflateEnd(&stream);
					result = XLOG_COMPRESS_ERR_WRITE;
					goto cleanup;
				}
				total_out += have;
			}
		}
		while (stream.avail_out == 0);

	}
	while (flush != MZ_FINISH);

	deflateEnd(&stream);

	/* Write gzip footer */
	if (!write_gzip_footer(fout, crc, (uint32_t) (total_in & 0xFFFFFFFF)))
	{
		result = XLOG_COMPRESS_ERR_WRITE;
		goto cleanup;
	}
	total_out += 8;

	/* Close files before potentially deleting source */
	fclose(fout);
	fout = NULL;
	fclose(fin);
	fin = NULL;

	/* Fill statistics */
	if (stats)
	{
		stats->original_size = total_in;
		stats->compressed_size = total_out;
		stats->ratio = (total_in > 0) ? (double) total_out / (double) total_in : 1.0;
		stats->time_us = get_time_us() - start_time;
	}

	/* Delete source file if requested */
	if (delete_src)
	{
		xlog_remove(src_path);
	}

cleanup:
	if (fin)
	{
		fclose(fin);
	}
	if (fout)
	{
		fclose(fout);
		/* Remove incomplete output file on error */
		if (result != XLOG_COMPRESS_OK)
		{
			xlog_remove(dst_path);
		}
	}
	free(in_buf);
	free(out_buf);

	return result;
}

/* ============================================================================
 * Convenience Functions
 * ============================================================================ */

xlog_compress_error xlog_compress_file_default(const char *src_path)
{
	return xlog_compress_file(src_path, NULL, XLOG_COMPRESS_LEVEL_DEFAULT,
	                          true, NULL);
}

bool xlog_is_compressed(const char *path)
{
	if (!path)
	{
		return false;
	}

	/* Check extension */
	size_t len = strlen(path);
	if (len >= 3 && strcmp(path + len - 3, ".gz") == 0)
	{
		return true;
	}

	/* Check gzip magic bytes */
	FILE *fp = fopen(path, "rb");
	if (fp)
	{
		uint8_t magic[2];
		bool is_gz = false;
		if (fread(magic, 1, 2, fp) == 2)
		{
			is_gz = (magic[0] == GZIP_MAGIC1 && magic[1] == GZIP_MAGIC2);
		}
		fclose(fp);
		return is_gz;
	}

	return false;
}

void xlog_compress_gen_path(char *out, size_t out_size, const char *src_path)
{
	if (!out || out_size == 0)
	{
		return;
	}

	if (src_path)
	{
		snprintf(out, out_size, "%s.gz", src_path);
	}
	else
	{
		out[0] = '\0';
	}
}

const char *xlog_compress_strerror(xlog_compress_error err)
{
	switch (err)
	{
		case XLOG_COMPRESS_OK:
			return "Success";
		case XLOG_COMPRESS_ERR_MEMORY:
			return "Memory allocation failed";
		case XLOG_COMPRESS_ERR_INPUT:
			return "Cannot open input file";
		case XLOG_COMPRESS_ERR_OUTPUT:
			return "Cannot create output file";
		case XLOG_COMPRESS_ERR_READ:
			return "Read error";
		case XLOG_COMPRESS_ERR_WRITE:
			return "Write error";
		case XLOG_COMPRESS_ERR_COMPRESS:
			return "Compression failed";
		case XLOG_COMPRESS_ERR_INVALID:
			return "Invalid argument";
		default:
			return "Unknown error";
	}
}

/* ============================================================================
 * Async Compression Implementation
 * ============================================================================ */

static void *compress_thread_func(void *arg)
{
	xlog_compress_task *task = (xlog_compress_task *) arg;

	if (!task->cancelled)
	{
		task->result = xlog_compress_file(task->src_path,
		                                  task->dst_path[0] ? task->dst_path : NULL,
		                                  task->level,
		                                  task->delete_src,
		                                  &task->stats);
	}
	else
	{
		task->result = XLOG_COMPRESS_ERR_INVALID;
	}

	task->done = 1;
	return NULL;
}

xlog_compress_task *xlog_compress_async(const char *src_path,
                                        const char *dst_path,
                                        int level,
                                        bool delete_src)
{
	if (!src_path)
	{
		return NULL;
	}

	xlog_compress_task *task = (xlog_compress_task *) calloc(1, sizeof(xlog_compress_task));
	if (!task)
	{
		return NULL;
	}

	strncpy(task->src_path, src_path, sizeof(task->src_path) - 1);
	if (dst_path)
	{
		strncpy(task->dst_path, dst_path, sizeof(task->dst_path) - 1);
	}
	task->level = level;
	task->delete_src = delete_src;
	task->done = 0;
	task->cancelled = 0;

	if (xlog_thread_create(&task->thread, compress_thread_func, task) != 0)
	{
		free(task);
		return NULL;
	}

	return task;
}

bool xlog_compress_is_done(xlog_compress_task *task)
{
	return task ? (task->done != 0) : true;
}

xlog_compress_error xlog_compress_wait(xlog_compress_task *task,
                                       xlog_compress_stats *stats)
{
	if (!task)
	{
		return XLOG_COMPRESS_ERR_INVALID;
	}

	xlog_thread_join(task->thread, NULL);

	xlog_compress_error result = task->result;
	if (stats)
	{
		*stats = task->stats;
	}

	free(task);
	return result;
}

void xlog_compress_cancel(xlog_compress_task *task)
{
	if (!task)
	{
		return;
	}

	task->cancelled = 1;
	xlog_thread_join(task->thread, NULL);
	free(task);
}

#endif /* XLOG_NO_COMPRESS */
