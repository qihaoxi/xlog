/* =====================================================================================
 *       Filename:  batch_writer.c
 *    Description:  High-performance batch writer implementation
 *                  Cross-platform support: Linux, macOS, Windows
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#include "batch_writer.h"
#include "simd.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef XLOG_PLATFORM_LINUX

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
/* O_DIRECT may not be available on all Linux systems */
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#elif defined(XLOG_PLATFORM_MACOS)
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
/* macOS doesn't have O_DIRECT, use F_NOCACHE instead */
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#elif defined(XLOG_PLATFORM_WINDOWS)
#include <io.h>
#include <fcntl.h>
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#endif

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

typedef enum batch_writer_mode
{
	BATCH_MODE_STDIO,       /* Standard FILE* based I/O */
	BATCH_MODE_DIRECT,      /* Direct I/O (O_DIRECT on Linux) */
	BATCH_MODE_MMAP         /* Memory-mapped I/O */
} batch_writer_mode;

struct batch_writer
{
	/* Buffer management */
	char *buffer;         /* Batch buffer */
	size_t capacity;       /* Buffer capacity */
	size_t used;           /* Bytes currently in buffer */
	size_t reserved;       /* Reserved space (for reserve/commit) */

	/* I/O backend */
	batch_writer_mode mode;           /* I/O mode */
	FILE *fp;             /* File pointer (STDIO mode) */
	int fd;             /* File descriptor (DIRECT/MMAP mode) */

	/* Memory-mapped I/O state */
	char *mmap_base;      /* mmap base address */
	size_t mmap_size;      /* mmap region size */
	size_t mmap_offset;    /* Current write offset */

	/* Configuration */
	batch_writer_config config;

	/* Statistics */
	batch_writer_stats stats;

	/* Entry counting */
	uint32_t pending_entries;

	/* Timing (for auto-flush) */
	uint64_t last_flush_time;
};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static inline uint64_t get_time_ns(void)
{
	return xlog_get_timestamp_ns();
}

static inline size_t align_up(size_t n, size_t alignment)
{
	return (n + alignment - 1) & ~(alignment - 1);
}

/* Flush buffer to FILE* */
static bool flush_stdio(batch_writer *writer)
{
	if (!writer->fp || writer->used == 0)
	{
		return true;
	}

	size_t written = fwrite(writer->buffer, 1, writer->used, writer->fp);
	if (written != writer->used)
	{
		writer->stats.write_errors++;
		return false;
	}

	writer->stats.bytes_flushed += written;
	writer->stats.flush_count++;
	writer->used = 0;
	writer->pending_entries = 0;
	writer->last_flush_time = get_time_ns();

	return true;
}

#ifdef XLOG_PLATFORM_POSIX

/* Flush buffer with direct I/O */
static bool flush_direct(batch_writer *writer)
{
	if (writer->fd < 0 || writer->used == 0)
	{
		return true;
	}

	/* Direct I/O requires aligned writes */
	size_t aligned_size = align_up(writer->used, 512);

	/* Pad with zeros if needed */
	if (aligned_size > writer->used)
	{
		memset(writer->buffer + writer->used, 0, aligned_size - writer->used);
	}

	ssize_t written = write(writer->fd, writer->buffer, aligned_size);
	if (written < 0)
	{
		writer->stats.write_errors++;
		return false;
	}

	writer->stats.bytes_flushed += writer->used;
	writer->stats.flush_count++;
	writer->used = 0;
	writer->pending_entries = 0;
	writer->last_flush_time = get_time_ns();

	return true;
}

/* Sync mmap region */
static bool flush_mmap(batch_writer *writer)
{
	if (!writer->mmap_base)
	{
		return true;
	}

	/* msync the written region */
	if (msync(writer->mmap_base, writer->mmap_offset, MS_ASYNC) != 0)
	{
		writer->stats.write_errors++;
		return false;
	}

	writer->stats.flush_count++;
	writer->last_flush_time = get_time_ns();

	return true;
}

#endif /* XLOG_PLATFORM_POSIX */

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

batch_writer *batch_writer_create(FILE *fp, const batch_writer_config *config)
{
	if (!fp)
	{
		return NULL;
	}

	batch_writer *writer = calloc(1, sizeof(batch_writer));
	if (!writer)
	{
		return NULL;
	}

	/* Apply configuration */
	if (config)
	{
		writer->config = *config;
	}
	else
	{
		batch_writer_config defaults = BATCH_WRITER_CONFIG_DEFAULT;
		writer->config = defaults;
	}

	/* Validate and adjust buffer size */
	if (writer->config.buffer_size < XLOG_BATCH_MIN_SIZE)
	{
		writer->config.buffer_size = XLOG_BATCH_MIN_SIZE;
	}
	if (writer->config.buffer_size > XLOG_BATCH_MAX_SIZE)
	{
		writer->config.buffer_size = XLOG_BATCH_MAX_SIZE;
	}

	/* Allocate buffer (aligned for potential SIMD operations) */
	writer->capacity = writer->config.buffer_size;

#if defined(XLOG_PLATFORM_WINDOWS)
	writer->buffer = _aligned_malloc(writer->capacity, 64);
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
	if (posix_memalign((void **) &writer->buffer, 64, writer->capacity) != 0)
	{
		writer->buffer = NULL;
	}
#else
	writer->buffer = malloc(writer->capacity);
#endif

	if (!writer->buffer)
	{
		free(writer);
		return NULL;
	}

	writer->mode = BATCH_MODE_STDIO;
	writer->fp = fp;
	writer->fd = -1;
	writer->last_flush_time = get_time_ns();

	return writer;
}

batch_writer *batch_writer_create_default(FILE *fp)
{
	return batch_writer_create(fp, NULL);
}

void batch_writer_destroy(batch_writer *writer)
{
	if (!writer)
	{
		return;
	}

	/* Flush remaining data */
	batch_writer_flush(writer);

	/* Cleanup based on mode */
	switch (writer->mode)
	{
		case BATCH_MODE_STDIO:
			/* Don't close FILE*, caller owns it */
			break;

#ifdef XLOG_PLATFORM_POSIX
		case BATCH_MODE_DIRECT:
			if (writer->fd >= 0)
			{
				close(writer->fd);
			}
			break;

		case BATCH_MODE_MMAP:
			if (writer->mmap_base)
			{
				munmap(writer->mmap_base, writer->mmap_size);
			}
			if (writer->fd >= 0)
			{
				close(writer->fd);
			}
			break;
#endif
	}

	/* Free buffer */
	if (writer->buffer)
	{
#if defined(XLOG_PLATFORM_WINDOWS)
		_aligned_free(writer->buffer);
#else
		free(writer->buffer);
#endif
	}

	free(writer);
}

ssize_t batch_writer_write(batch_writer *writer, const char *data, size_t len)
{
	if (!writer || !data || len == 0)
	{
		return -1;
	}

	/* Handle large writes that exceed buffer */
	if (len > writer->capacity)
	{
		/* Flush current buffer first */
		if (writer->used > 0)
		{
			if (!batch_writer_flush(writer))
			{
				return -1;
			}
		}

		/* Write directly */
		if (writer->mode == BATCH_MODE_STDIO)
		{
			size_t written = fwrite(data, 1, len, writer->fp);
			if (written != len)
			{
				writer->stats.write_errors++;
				return -1;
			}
			writer->stats.bytes_written += written;
			writer->stats.bytes_flushed += written;
			writer->stats.entries_written++;
			return (ssize_t) written;
		}
#ifdef XLOG_PLATFORM_POSIX
		else if (writer->mode == BATCH_MODE_DIRECT)
		{
			ssize_t written = write(writer->fd, data, len);
			if (written < 0)
			{
				writer->stats.write_errors++;
				return -1;
			}
			writer->stats.bytes_written += written;
			writer->stats.bytes_flushed += written;
			writer->stats.entries_written++;
			return written;
		}
#endif
	}

	/* Check if we need to flush first */
	if (writer->used + len > writer->capacity)
	{
		if (!batch_writer_flush(writer))
		{
			return -1;
		}
		writer->stats.forced_flushes++;
	}

	/* Copy data to buffer
	 * Note: On Linux/macOS, glibc/libSystem memcpy is highly optimized
	 * and outperforms custom SIMD. Only use xlog_simd_memcpy on Windows
	 * or for very specific alignment scenarios.
	 */
#if defined(XLOG_PLATFORM_WINDOWS)
	if (len >= 64)
	{
		xlog_simd_memcpy(writer->buffer + writer->used, data, len);
	}
	else
	{
		memcpy(writer->buffer + writer->used, data, len);
	}
#else
	memcpy(writer->buffer + writer->used, data, len);
#endif

	writer->used += len;
	writer->stats.bytes_written += len;
	writer->stats.entries_written++;
	writer->pending_entries++;

	/* Check thresholds */
	if (batch_writer_should_flush(writer))
	{
		batch_writer_flush(writer);
		writer->stats.auto_flushes++;
	}

	return (ssize_t) len;
}

int batch_writer_printf(batch_writer *writer, const char *fmt, ...)
{
	if (!writer || !fmt)
	{
		return -1;
	}

	va_list args;
	va_start(args, fmt);

	/* First, try to format directly into buffer */
	size_t available = writer->capacity - writer->used;

	va_list args_copy;
	va_copy(args_copy, args);
	int needed = vsnprintf(writer->buffer + writer->used, available, fmt, args_copy);
	va_end(args_copy);

	if (needed < 0)
	{
		va_end(args);
		writer->stats.write_errors++;
		return -1;
	}

	if ((size_t) needed < available)
	{
		/* Fits in buffer */
		writer->used += needed;
		writer->stats.bytes_written += needed;
		writer->stats.entries_written++;
		writer->pending_entries++;

		if (batch_writer_should_flush(writer))
		{
			batch_writer_flush(writer);
			writer->stats.auto_flushes++;
		}

		va_end(args);
		return needed;
	}

	/* Need to flush and retry */
	if (!batch_writer_flush(writer))
	{
		va_end(args);
		return -1;
	}
	writer->stats.forced_flushes++;

	/* Check if it will ever fit */
	if ((size_t) needed >= writer->capacity)
	{
		/* Too large - allocate temporary buffer */
		char *temp = malloc(needed + 1);
		if (!temp)
		{
			va_end(args);
			writer->stats.write_errors++;
			return -1;
		}

		vsnprintf(temp, needed + 1, fmt, args);
		va_end(args);

		/* Write directly */
		size_t written = fwrite(temp, 1, needed, writer->fp);
		free(temp);

		if (written != (size_t) needed)
		{
			writer->stats.write_errors++;
			return -1;
		}

		writer->stats.bytes_written += written;
		writer->stats.bytes_flushed += written;
		writer->stats.entries_written++;
		return needed;
	}

	/* Now it fits */
	needed = vsnprintf(writer->buffer + writer->used, writer->capacity - writer->used, fmt, args);
	va_end(args);

	if (needed > 0)
	{
		writer->used += needed;
		writer->stats.bytes_written += needed;
		writer->stats.entries_written++;
		writer->pending_entries++;
	}

	return needed;
}

bool batch_writer_flush(batch_writer *writer)
{
	if (!writer)
	{
		return false;
	}

	switch (writer->mode)
	{
		case BATCH_MODE_STDIO:
			return flush_stdio(writer);

#ifdef XLOG_PLATFORM_POSIX
		case BATCH_MODE_DIRECT:
			return flush_direct(writer);

		case BATCH_MODE_MMAP:
			return flush_mmap(writer);
#endif

		default:
			return false;
	}
}

bool batch_writer_should_flush(batch_writer *writer)
{
	if (!writer)
	{
		return false;
	}

	/* Check buffer threshold */
	float usage = (float) writer->used / (float) writer->capacity;
	if (usage >= writer->config.flush_threshold)
	{
		return true;
	}

	/* Check entry count */
	if (writer->pending_entries >= writer->config.max_pending)
	{
		return true;
	}

	/* Check timeout */
	if (writer->config.flush_timeout_ms > 0 && writer->used > 0)
	{
		uint64_t now = get_time_ns();
		uint64_t elapsed_ms = (now - writer->last_flush_time) / 1000000;
		if (elapsed_ms >= writer->config.flush_timeout_ms)
		{
			return true;
		}
	}

	return false;
}

size_t batch_writer_pending(batch_writer *writer)
{
	return writer ? writer->used : 0;
}

size_t batch_writer_capacity(batch_writer *writer)
{
	return writer ? writer->capacity : 0;
}

void batch_writer_get_stats(batch_writer *writer, batch_writer_stats *stats)
{
	if (!writer || !stats)
	{
		return;
	}
	*stats = writer->stats;
}

void batch_writer_reset_stats(batch_writer *writer)
{
	if (!writer)
	{
		return;
	}
	memset(&writer->stats, 0, sizeof(batch_writer_stats));
}

bool batch_writer_set_file(batch_writer *writer, FILE *fp)
{
	if (!writer || !fp)
	{
		return false;
	}

	/* Flush current buffer first */
	batch_writer_flush(writer);

	writer->fp = fp;
	return true;
}

char *batch_writer_reserve(batch_writer *writer, size_t len)
{
	if (!writer || len == 0)
	{
		return NULL;
	}

	/* Check if we need to flush first */
	if (writer->used + len > writer->capacity)
	{
		if (!batch_writer_flush(writer))
		{
			return NULL;
		}
	}

	/* Check if it fits now */
	if (writer->used + len > writer->capacity)
	{
		return NULL;  /* Too large even after flush */
	}

	writer->reserved = len;
	return writer->buffer + writer->used;
}

void batch_writer_commit(batch_writer *writer, size_t len)
{
	if (!writer)
	{
		return;
	}

	/* Ensure we don't commit more than reserved */
	if (len > writer->reserved)
	{
		len = writer->reserved;
	}

	writer->used += len;
	writer->stats.bytes_written += len;
	writer->stats.entries_written++;
	writer->pending_entries++;
	writer->reserved = 0;

	/* Check thresholds */
	if (batch_writer_should_flush(writer))
	{
		batch_writer_flush(writer);
		writer->stats.auto_flushes++;
	}
}

/* ============================================================================
 * Direct I/O Implementation (Linux-specific)
 * ============================================================================ */

#ifdef XLOG_PLATFORM_LINUX

batch_writer *batch_writer_create_direct(const char *path,
                                         const batch_writer_config *config)
{
	if (!path)
	{
		return NULL;
	}

	/* Open with O_DIRECT */
	int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_DIRECT, 0644);
	if (fd < 0)
	{
		return NULL;
	}

	batch_writer *writer = calloc(1, sizeof(batch_writer));
	if (!writer)
	{
		close(fd);
		return NULL;
	}

	/* Apply configuration */
	if (config)
	{
		writer->config = *config;
	}
	else
	{
		batch_writer_config defaults = BATCH_WRITER_CONFIG_DEFAULT;
		writer->config = defaults;
	}

	/* Buffer must be 512-byte aligned for O_DIRECT */
	writer->capacity = align_up(writer->config.buffer_size, 512);

	if (posix_memalign((void **) &writer->buffer, 512, writer->capacity) != 0)
	{
		close(fd);
		free(writer);
		return NULL;
	}

	writer->mode = BATCH_MODE_DIRECT;
	writer->fd = fd;
	writer->fp = NULL;
	writer->last_flush_time = get_time_ns();

	return writer;
}

#endif /* XLOG_PLATFORM_LINUX */

/* ============================================================================
 * Memory-mapped I/O Implementation
 * ============================================================================ */

#ifdef XLOG_PLATFORM_POSIX

batch_writer *batch_writer_create_mmap(const char *path,
                                       size_t initial_size,
                                       const batch_writer_config *config)
{
	if (!path || initial_size == 0)
	{
		return NULL;
	}

	/* Open file */
	int fd = open(path, O_RDWR | O_CREAT, 0644);
	if (fd < 0)
	{
		return NULL;
	}

	/* Extend file to initial size */
	if (ftruncate(fd, initial_size) != 0)
	{
		close(fd);
		return NULL;
	}

	/* Map the file */
	void *mapped = mmap(NULL, initial_size, PROT_READ | PROT_WRITE,
	                    MAP_SHARED, fd, 0);
	if (mapped == MAP_FAILED)
	{
		close(fd);
		return NULL;
	}

	batch_writer *writer = calloc(1, sizeof(batch_writer));
	if (!writer)
	{
		munmap(mapped, initial_size);
		close(fd);
		return NULL;
	}

	/* Apply configuration */
	if (config)
	{
		writer->config = *config;
	}
	else
	{
		batch_writer_config defaults = BATCH_WRITER_CONFIG_DEFAULT;
		writer->config = defaults;
	}

	writer->mode = BATCH_MODE_MMAP;
	writer->fd = fd;
	writer->fp = NULL;
	writer->mmap_base = (char *) mapped;
	writer->mmap_size = initial_size;
	writer->mmap_offset = 0;
	writer->buffer = NULL;  /* Not used in mmap mode */
	writer->capacity = initial_size;
	writer->last_flush_time = get_time_ns();

	return writer;
}

#elif defined(XLOG_PLATFORM_WINDOWS)

batch_writer *batch_writer_create_mmap(const char *path,
										size_t initial_size,
										const batch_writer_config *config)
										{
	if (!path || initial_size == 0) return NULL;

	/* Create file */
	HANDLE hFile = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
							   FILE_SHARE_READ, NULL,
							   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return NULL;

	/* Set file size */
	LARGE_INTEGER liSize;
	liSize.QuadPart = initial_size;
	if (!SetFilePointerEx(hFile, liSize, NULL, FILE_BEGIN) ||
		!SetEndOfFile(hFile))
	{
		CloseHandle(hFile);
		return NULL;
	}

	/* Create file mapping */
	HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READWRITE,
										  0, (DWORD)initial_size, NULL);
	if (!hMapping)
	{
		CloseHandle(hFile);
		return NULL;
	}

	/* Map view */
	void *mapped = MapViewOfFile(hMapping, FILE_MAP_WRITE, 0, 0, initial_size);
	if (!mapped)
	{
		CloseHandle(hMapping);
		CloseHandle(hFile);
		return NULL;
	}

	batch_writer *writer = calloc(1, sizeof(batch_writer));
	if (!writer)
	{
		UnmapViewOfFile(mapped);
		CloseHandle(hMapping);
		CloseHandle(hFile);
		return NULL;
	}

	/* Apply configuration */
	if (config)
	{
		writer->config = *config;
	}
	else
	{
		batch_writer_config defaults = BATCH_WRITER_CONFIG_DEFAULT;
		writer->config = defaults;
	}

	writer->mode = BATCH_MODE_MMAP;
	writer->fd = (int)(intptr_t)hFile;  /* Store handle as fd */
	writer->fp = NULL;
	writer->mmap_base = (char *)mapped;
	writer->mmap_size = initial_size;
	writer->mmap_offset = 0;
	writer->buffer = NULL;
	writer->capacity = initial_size;
	writer->last_flush_time = get_time_ns();

	/* Store mapping handle in reserved field */
	/* Note: In production, you'd want a proper structure for Windows handles */

	return writer;
}

#endif /* XLOG_PLATFORM_WINDOWS */

