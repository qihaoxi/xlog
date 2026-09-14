/* =====================================================================================
 *       Filename:  rotate.c
 *    Description:  Log rotation strategy implementation
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rotate.h"
#include "platform.h"
#include "compress.h"

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

static char *str_dup(const char *s)
{
	if (!s)
	{
		return NULL;
	}
	size_t len = strlen(s) + 1;
	char *dup = malloc(len);
	if (dup)
	{
		memcpy(dup, s, len);
	}
	return dup;
}

static void get_current_date(int *year, int *month, int *day)
{
	time_t now = time(NULL);
	struct tm tm_info;
	xlog_get_localtime(now, &tm_info);
	*year = tm_info.tm_year + 1900;
	*month = tm_info.tm_mon + 1;
	*day = tm_info.tm_mday;
}

static bool date_changed(const rotate_state *state)
{
	int year, month, day;
	get_current_date(&year, &month, &day);
	return (year != state->current_year ||
	        month != state->current_month ||
	        day != state->current_day);
}

static void update_current_date(rotate_state *state)
{
	get_current_date(&state->current_year, &state->current_month, &state->current_day);
}

static bool open_current_file(rotate_state *state)
{
	if (state->fp)
	{
		fclose(state->fp);
		state->fp = NULL;
	}

	state->fp = fopen(state->current_path, "a");
	if (!state->fp)
	{
		return false;
	}

	/* Get current file size */
	int64_t size = xlog_file_size(state->current_path);
	state->current_size = (size > 0) ? (uint64_t) size : 0;

	return true;
}

/* Compare function for sorting archive file names */
static int archive_name_compare(const void *a, const void *b)
{
	return strcmp(*(const char **) a, *(const char **) b);
}

/* ============================================================================
 * Path Generation Functions
 * ============================================================================ */

void rotate_gen_active_path(char *out, size_t out_size, const rotate_config *config)
{
	xlog_snprintf(out, out_size, "%s%c%s%s",
	              config->directory, XLOG_PATH_SEP,
	              config->base_name, config->extension);
}

void rotate_gen_dated_path(char *out, size_t out_size, const rotate_config *config,
                           int year, int month, int day)
{
	xlog_snprintf(out, out_size, "%s%c%s-%04d%02d%02d%s",
	              config->directory, XLOG_PATH_SEP,
	              config->base_name, year, month, day, config->extension);
}

void rotate_gen_sequenced_path(char *out, size_t out_size, const rotate_config *config,
                               int year, int month, int day, int sequence)
{
	xlog_snprintf(out, out_size, "%s%c%s-%04d%02d%02d-%02d%s",
	              config->directory, XLOG_PATH_SEP,
	              config->base_name, year, month, day, sequence, config->extension);
}

void rotate_gen_archive_pattern(char *out, size_t out_size, const rotate_config *config)
{
	xlog_snprintf(out, out_size, "%s-*%s*", config->base_name, config->extension);
}

/* ============================================================================
 * Directory Management Functions
 * ============================================================================ */

int64_t rotate_calc_dir_size(const rotate_state *state)
{
	char pattern[128];
	int64_t total = 0;

	/* Size of current active file */
	int64_t active_size = xlog_file_size(state->current_path);
	if (active_size > 0)
	{
		total += active_size;
	}

	/* Size of all archived files */
	rotate_gen_archive_pattern(pattern, sizeof(pattern), &state->config);
	int64_t archive_size = xlog_dir_used_space(state->dir_path, pattern);
	if (archive_size > 0)
	{
		total += archive_size;
	}

	return total;
}

/* Callback context for collecting file names */
typedef struct
{
	char **files;
	int count;
	int capacity;
	const char *dir_path;
} file_collector_t;

static void file_collector_callback(const char *filename, void *user_data)
{
	file_collector_t *collector = (file_collector_t *) user_data;

	if (collector->count >= collector->capacity)
	{
		int new_cap = collector->capacity * 2;
		char **new_files = realloc(collector->files, new_cap * sizeof(char *));
		if (!new_files)
		{
			return;
		}
		collector->files = new_files;
		collector->capacity = new_cap;
	}

	/* Store full path */
	char full_path[512];
	xlog_snprintf(full_path, sizeof(full_path), "%s%c%s",
	              collector->dir_path, XLOG_PATH_SEP, filename);
	collector->files[collector->count] = str_dup(full_path);
	if (collector->files[collector->count])
	{
		collector->count++;
	}
}

char **rotate_list_archives(const rotate_state *state, int *count_out)
{
	char pattern[128];
	rotate_gen_archive_pattern(pattern, sizeof(pattern), &state->config);

	file_collector_t collector =
			{
					.files = malloc(64 * sizeof(char *)),
					.count = 0,
					.capacity = 64,
					.dir_path = state->dir_path
			};

	if (!collector.files)
	{
		*count_out = 0;
		return NULL;
	}

	xlog_list_files(state->dir_path, pattern, file_collector_callback, &collector);

	/* Sort by name (which sorts by date due to YYYYMMDD format) */
	if (collector.count > 1)
	{
		qsort(collector.files, collector.count, sizeof(char *), archive_name_compare);
	}

	*count_out = collector.count;
	return collector.files;
}

void rotate_free_archive_list(char **list, int count)
{
	if (!list)
	{
		return;
	}
	for (int i = 0; i < count; i++)
	{
		free(list[i]);
	}
	free(list);
}

int rotate_enforce_dir_limit(rotate_state *state)
{
	int deleted = 0;
	int count;
	char **archives = rotate_list_archives(state, &count);

	if (!archives || count == 0)
	{
		rotate_free_archive_list(archives, count);
		return 0;
	}

	/* Check total size and file count limits */
	int64_t total_size = rotate_calc_dir_size(state);
	int total_files = count + 1;  /* archives + current active file */

	/* Delete oldest files until we're under limits */
	int idx = 0;
	while (idx < count &&
	       (total_size > (int64_t) state->config.max_dir_size ||
	        total_files > (int) state->config.max_files))
	{

		int64_t file_size = xlog_file_size(archives[idx]);
		if (xlog_remove(archives[idx]))
		{
			if (file_size > 0)
			{
				total_size -= file_size;
			}
			total_files--;
			deleted++;
			state->files_deleted++;
		}
		idx++;
	}

	rotate_free_archive_list(archives, count);
	return deleted;
}

/* ============================================================================
 * Cross-directory root quota (doc116 §3.2)
 *
 * 语义:config.max_root_size > 0 时,每次轮转(init + rotate_force)对
 * directory 的父目录执行一次总量淘汰——统计所有兄弟目录中匹配本 logger
 * 归档模式(base-YYYYMMDD*)的文件,按 mtime 从旧到新跨目录删除直到回到
 * 限内。active 文件(无日期后缀)永不匹配;异名(他应用)文件零触碰。
 * 多进程安全:mkdir 原子锁,拿不到即跳过(他人会做);陈锁(>120s)抢占重试
 * 一次;全程幂等,失败 best-effort 不阻塞写日志。
 * ============================================================================ */

#define XLOG_ROOT_QUOTA_LOCK_NAME     ".xlog-quota.lock"
#define XLOG_ROOT_QUOTA_LOCK_STALE_S  120   /* 锁持有者存活判定阈值(秒) */
#define XLOG_ROOT_QUOTA_GRACE_S       300   /* 新归档保护期(异步压缩源文件在途) */
#define XLOG_ROOT_QUOTA_MAX_CANDIDATES 8192 /* 防御性上限(多实例规模之外留裕量) */

static bool root_quota_dirname(const char *dir, char *out, size_t out_size)
{
	if (!dir || !out || out_size == 0)
	{
		return false;
	}

	char tmp[512];
	size_t len = strlen(dir);
	if (len == 0 || len >= sizeof(tmp))
	{
		return false;
	}
	memcpy(tmp, dir, len + 1);

	/* 去尾部连续分隔符 */
	while (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
	{
		tmp[--len] = '\0';
	}

	/* 找最后一个分隔符;无 → 无父目录(顶层),跳过 */
	char *last = NULL;
	for (char *p = tmp; *p; p++)
	{
		if (*p == '/' || *p == '\\')
		{
			last = p;
		}
	}
	if (!last || last == tmp)
	{
		return false;   /* 顶层路径或 "/x" 形态:无兄弟目录可言 */
	}
	*last = '\0';
	if (strlen(tmp) == 0 || strlen(tmp) >= out_size)
	{
		return false;
	}
	memcpy(out, tmp, strlen(tmp) + 1);
	return true;
}

typedef struct
{
	char *path;
	int64_t size;
	int64_t mtime;
} quota_file_t;

typedef struct
{
	quota_file_t *files;
	int count;
	int capacity;
	const char *pattern;
} quota_collect_t;

/* 收集单个子目录内匹配 pattern 的归档文件(path/size/mtime) */
typedef struct
{
	quota_collect_t *collect;
	char dir_path[512];
} quota_dir_ctx_t;

static void quota_collect_dir(const char *subdir_name, void *user_data)
{
	quota_dir_ctx_t *ctx = (quota_dir_ctx_t *) user_data;
	quota_collect_t *c = ctx->collect;
	char dir_path[1024];   /* parent(≤511)+sep+name,-Wformat-truncation 免疫 */
	xlog_snprintf(dir_path, sizeof(dir_path), "%s%c%s",
	              ctx->dir_path, XLOG_PATH_SEP, subdir_name);

	file_collector_t names =
			{
					.files = malloc(64 * sizeof(char *)),
					.count = 0,
					.capacity = 64,
					.dir_path = dir_path
			};
	if (!names.files)
	{
		return;
	}
	xlog_list_files(dir_path, c->pattern, file_collector_callback, &names);

	for (int i = 0; i < names.count; i++)
	{
		if (c->count >= c->capacity)
		{
			free(names.files[i]);   /* 候选满:丢弃剩余(best-effort,下轮再收) */
			continue;
		}
		int64_t sz = xlog_file_size(names.files[i]);
		int64_t mt = xlog_file_mtime(names.files[i]);
		if (sz <= 0 || mt < 0)
		{
			free(names.files[i]);
			continue;
		}
		c->files[c->count].path = names.files[i];   /* 所有权移交候选数组 */
		c->files[c->count].size = sz;
		c->files[c->count].mtime = mt;
		c->count++;
	}
	free(names.files);
}

static int quota_file_compare(const void *a, const void *b)
{
	const quota_file_t *fa = (const quota_file_t *) a;
	const quota_file_t *fb = (const quota_file_t *) b;
	if (fa->mtime < fb->mtime) return -1;
	if (fa->mtime > fb->mtime) return 1;
	return strcmp(fa->path, fb->path);
}

/* 回收已空兄弟目录(best-effort);活实例目录有 active 文件 rmdir 自然失败,
 * 自身目录永不触碰(轮转中存在"归档已生成、新 active 未开"的零文件瞬间) */
typedef struct
{
	char parent[512];
	char self_name[256];
} quota_rmdir_ctx_t;

static void quota_rmdir_empty_cb(const char *subdir_name, void *user_data)
{
	quota_rmdir_ctx_t *ctx = (quota_rmdir_ctx_t *) user_data;
	if (strcmp(subdir_name, ctx->self_name) == 0)
	{
		return;
	}
	char full[1024];   /* 同上:parent(≤511)+sep+name 余量 */
	xlog_snprintf(full, sizeof(full), "%s%c%s",
	              ctx->parent, XLOG_PATH_SEP, subdir_name);
	xlog_rmdir_empty(full);
}

int rotate_enforce_root_quota(rotate_state *state)
{
	if (!state || state->config.max_root_size == 0)
	{
		return 0;
	}

	char parent[512];
	if (!root_quota_dirname(state->dir_path, parent, sizeof(parent)))
	{
		return 0;   /* 无父目录:配额无作用域 */
	}

	/* ---- 多进程互斥:mkdir 原子锁(陈锁 >120s 抢占重试一次) ---- */
	char lock_path[576];
	xlog_snprintf(lock_path, sizeof(lock_path), "%s%c%s",
	              parent, XLOG_PATH_SEP, XLOG_ROOT_QUOTA_LOCK_NAME);
	if (!xlog_mkdir_lock(lock_path))
	{
		int64_t lock_mtime = xlog_file_mtime(lock_path);
		if (lock_mtime < 0 ||
		    lock_mtime > time(NULL) - XLOG_ROOT_QUOTA_LOCK_STALE_S)
		{
			return 0;   /* 他人持有(或不可判):本轮跳过,他人会做 */
		}
		xlog_rmdir_empty(lock_path);   /* 陈锁:抢占 */
		if (!xlog_mkdir_lock(lock_path))
		{
			return 0;
		}
	}

	int deleted = 0;

	/* ---- 收集兄弟目录(含自身)内匹配归档模式的候选 ---- */
	char pattern[128];
	rotate_gen_archive_pattern(pattern, sizeof(pattern), &state->config);

	quota_collect_t collect =
			{
					.files = malloc(XLOG_ROOT_QUOTA_MAX_CANDIDATES * sizeof(quota_file_t)),
					.count = 0,
					.capacity = XLOG_ROOT_QUOTA_MAX_CANDIDATES,
					.pattern = pattern
			};
	quota_dir_ctx_t ctx = {0};
	ctx.collect = &collect;
	xlog_strncpy(ctx.dir_path, parent, sizeof(ctx.dir_path));

	if (collect.files)
	{
		xlog_list_subdirs(parent, quota_collect_dir, &ctx);

		int64_t total = 0;
		for (int i = 0; i < collect.count; i++)
		{
			total += collect.files[i].size;
		}

		if (total > (int64_t) state->config.max_root_size && collect.count > 0)
		{
			qsort(collect.files, (size_t) collect.count,
			      sizeof(quota_file_t), quota_file_compare);

			int64_t grace_cutoff = (int64_t) time(NULL) - XLOG_ROOT_QUOTA_GRACE_S;
			for (int i = 0; i < collect.count && total > (int64_t) state->config.max_root_size; i++)
			{
				if (collect.files[i].mtime > grace_cutoff)
				{
					break;   /* 已按 mtime 升序:其后全在保护期内 */
				}
				if (xlog_remove(collect.files[i].path))
				{
					total -= collect.files[i].size;
					deleted++;
					state->files_deleted++;
				}
			}

			/* 顺手回收已空兄弟目录(best-effort;活实例目录有 active 文件,
			 * rmdir 自然失败;自身目录永不触碰) */
			if (deleted > 0)
			{
				quota_rmdir_ctx_t rctx;
				xlog_strncpy(rctx.parent, parent, sizeof(rctx.parent));
				/* 自身目录名 = dir_path 末段 */
				const char *base = state->dir_path;
				for (const char *p = state->dir_path; *p; p++)
				{
					if (*p == '/' || *p == '\\')
					{
						base = p + 1;
					}
				}
				xlog_strncpy(rctx.self_name, base, sizeof(rctx.self_name));
				xlog_list_subdirs(parent, quota_rmdir_empty_cb, &rctx);
			}
		}

		for (int i = 0; i < collect.count; i++)
		{
			free(collect.files[i].path);
		}
		free(collect.files);
	}

	xlog_rmdir_empty(lock_path);   /* 释放锁 */
	return deleted;
}

/**
 * Find next sequence number for archives.
 * Returns the next available sequence number (1, 2, 3, ...)
 * Note: This does NOT check dated archive without sequence.
 */
static int find_next_sequence_number(const rotate_state *state)
{
	char path[512];

	/* Find next sequence number starting from 1 */
	for (int seq = 1; seq <= 99; seq++)
	{
		rotate_gen_sequenced_path(path, sizeof(path), &state->config,
		                          state->current_year, state->current_month,
		                          state->current_day, seq);
		if (!xlog_file_exists(path))
		{
			return seq;
		}
	}

	return 99;  /* Max sequence reached */
}

/**
 * Check if dated archive (without sequence) exists.
 * e.g., pel-20260210.log
 */
static bool dated_archive_exists(const rotate_state *state)
{
	char path[512];
	rotate_gen_dated_path(path, sizeof(path), &state->config,
	                      state->current_year, state->current_month, state->current_day);
	return xlog_file_exists(path);
}

/**
 * Find next sequence number for archives.
 * Returns:
 *   0 - If the dated archive (e.g., pel-20260210.log) doesn't exist yet
 *   N - The next available sequence number
 */
int rotate_find_next_sequence(const rotate_state *state)
{
	if (!dated_archive_exists(state))
	{
		return 0;  /* Dated archive doesn't exist, use it first */
	}

	return find_next_sequence_number(state);
}

/**
 * Normalize dated archive to sequenced format.
 * If pel-20260210.log exists, rename it to pel-20260210-01.log
 * This ensures all archives have consistent naming with sequence numbers.
 */
static bool rotate_normalize_dated_archive(rotate_state *state)
{
	char dated_path[512];
	char sequenced_path[512];

	/* Check if dated archive without sequence exists */
	rotate_gen_dated_path(dated_path, sizeof(dated_path), &state->config,
	                      state->current_year, state->current_month, state->current_day);

	if (!xlog_file_exists(dated_path))
	{
		return true;  /* No dated archive, nothing to do */
	}

	/* Check if pel-20260210-01.log already exists */
	rotate_gen_sequenced_path(sequenced_path, sizeof(sequenced_path), &state->config,
	                          state->current_year, state->current_month,
	                          state->current_day, 1);

	if (xlog_file_exists(sequenced_path))
	{
		return true;  /* Already normalized or 01 exists from other source */
	}

	/* Rename dated archive to -01 */
	if (!xlog_rename(dated_path, sequenced_path))
	{
		return false;
	}

	return true;
}

/* ============================================================================
 * Core Rotation API
 * ============================================================================ */

bool rotate_init(rotate_state *state, const rotate_config *config)
{
	if (!state || !config || !config->base_name || !config->directory)
	{
		return false;
	}

	memset(state, 0, sizeof(rotate_state));

	/* Copy configuration */
	memcpy(&state->config, config, sizeof(rotate_config));

	/* Set defaults if not specified */
	if (state->config.max_file_size == 0)
	{
		state->config.max_file_size = XLOG_ROTATE_DEFAULT_MAX_FILE_SIZE;
	}
	if (state->config.max_dir_size == 0)
	{
		state->config.max_dir_size = XLOG_ROTATE_DEFAULT_MAX_DIR_SIZE;
	}
	if (state->config.max_files == 0)
	{
		state->config.max_files = XLOG_ROTATE_DEFAULT_MAX_FILES;
	}
	if (state->config.extension == NULL)
	{
		state->config.extension = ".log";
	}

	/* Cache paths into owned local buffers */
	xlog_strncpy(state->dir_path, config->directory, sizeof(state->dir_path));
	xlog_strncpy(state->base_name, config->base_name, sizeof(state->base_name));
	xlog_strncpy(state->extension, state->config.extension, sizeof(state->extension));

	/* Re-point config pointers to the local buffers to avoid use-after-free
	 * when the caller frees the original strings after rotate_init() returns */
	state->config.directory = state->dir_path;
	state->config.base_name = state->base_name;
	state->config.extension = state->extension;

	/* Ensure directory exists */
	if (!xlog_mkdir_p(state->dir_path))
	{
		return false;
	}

	/* Generate current active file path */
	rotate_gen_active_path(state->current_path, sizeof(state->current_path), &state->config);

	/* Initialize date tracking */
	update_current_date(state);

	/* Check for startup rotation */
	if (config->rotate_on_start)
	{
		int64_t existing_size = xlog_file_size(state->current_path);
		if (existing_size > 0)
		{
			/* File exists from previous run, rotate it */
			state->current_size = (uint64_t) existing_size;

			/* Check if we should rotate based on size or date */
			if (existing_size >= (int64_t) state->config.max_file_size)
			{
				rotate_force(state);
			}
		}
	}

	/* Open current file */
	if (!open_current_file(state))
	{
		return false;
	}

	/* Enforce directory limits on startup, then cross-directory root quota */
	rotate_enforce_dir_limit(state);
	rotate_enforce_root_quota(state);

	return true;
}

void rotate_cleanup(rotate_state *state)
{
	if (!state)
	{
		return;
	}

	/* Reclaim any in-flight async compression task: cancel + join thread + free task */
	if (state->pending_compress)
	{
		xlog_compress_cancel(state->pending_compress);
		state->pending_compress = NULL;
	}

	if (state->fp)
	{
		fflush(state->fp);
		fclose(state->fp);
		state->fp = NULL;
	}
}

bool rotate_needed(rotate_state *state)
{
	if (!state)
	{
		return false;
	}

	/* Check date change */
	if (date_changed(state))
	{
		return true;
	}

	/* Check size limit */
	if (state->current_size >= state->config.max_file_size)
	{
		return true;
	}

	return false;
}

bool rotate_force(rotate_state *state)
{
	if (!state)
	{
		return false;
	}

	char archive_path[512];
	char dated_path[512];
	char sequenced_path[512];

	/* Close current file */
	if (state->fp)
	{
		fflush(state->fp);
		fclose(state->fp);
		state->fp = NULL;
	}

	/* Check if dated archive exists (e.g., pel-20260210.log) */
	bool has_dated_archive = dated_archive_exists(state);

	if (!has_dated_archive)
	{
		/* First archive of the day - use dated name without sequence */
		/* pel.log -> pel-20260210.log */
		rotate_gen_dated_path(archive_path, sizeof(archive_path), &state->config,
		                      state->current_year, state->current_month, state->current_day);
		state->current_sequence = 1;
	}
	else
	{
		/* Dated archive (pel-20260210.log) exists, this is second rotation of the day
		 * Rename it to -01, then archive current file to -02
		 */
		rotate_gen_dated_path(dated_path, sizeof(dated_path), &state->config,
		                      state->current_year, state->current_month, state->current_day);

		/* Check if -01 already exists */
		rotate_gen_sequenced_path(sequenced_path, sizeof(sequenced_path), &state->config,
		                          state->current_year, state->current_month,
		                          state->current_day, 1);

		if (!xlog_file_exists(sequenced_path))
		{
			/* First time: rename pel-20260210.log to pel-20260210-01.log */
			xlog_rename(dated_path, sequenced_path);
		}

		/* Find next available sequence number for current file */
		int seq = find_next_sequence_number(state);

		/* Archive current file to pel-20260210-NN.log */
		rotate_gen_sequenced_path(archive_path, sizeof(archive_path), &state->config,
		                          state->current_year, state->current_month,
		                          state->current_day, seq);
		state->current_sequence = seq + 1;
	}

	/* Rename current file to archive */
	if (xlog_file_exists(state->current_path))
	{
		if (!xlog_rename(state->current_path, archive_path))
		{
			/* Rename failed, try to reopen the original file */
			open_current_file(state);
			return false;
		}

		/* Compress the archived file if configured */
		if (state->config.compress_old)
		{
			/* Reclaim the previous in-flight compression (join + free) before
			 * launching a new one, so a task + thread is never orphaned.
			 * No-op (instant join) when the previous task already finished. */
			if (state->pending_compress)
			{
				xlog_compress_wait(state->pending_compress, NULL);
				state->pending_compress = NULL;
			}

			/* Async compression to avoid blocking the logger write path */
			state->pending_compress = xlog_compress_async(
				archive_path, NULL,
				XLOG_COMPRESS_LEVEL_DEFAULT,
				true  /* delete source after compression */
			);
		}
	}

	/* Update state */
	state->total_rotations++;

	/* Enforce directory limits, then cross-directory root quota */
	rotate_enforce_dir_limit(state);
	rotate_enforce_root_quota(state);

	/* Open new file */
	if (!open_current_file(state))
	{
		return false;
	}

	return true;
}

bool rotate_check_and_rotate(rotate_state *state)
{
	if (!state)
	{
		return false;
	}

	/* Check for date change first */
	if (date_changed(state))
	{
		/* Date changed - update and reset sequence */
		update_current_date(state);
		state->current_sequence = 0;

		/* Rotate current file if it has content */
		if (state->current_size > 0)
		{
			return rotate_force(state);
		}
		return true;
	}

	/* Check size limit */
	if (state->current_size >= state->config.max_file_size)
	{
		return rotate_force(state);
	}

	return true;
}

int64_t rotate_write(rotate_state *state, const char *data, size_t len)
{
	if (!state || !data || len == 0)
	{
		return -1;
	}

	/* Check and rotate if needed */
	if (!rotate_check_and_rotate(state))
	{
		return -1;
	}

	/* Ensure file is open */
	if (!state->fp)
	{
		if (!open_current_file(state))
		{
			return -1;
		}
	}

	/* Write data */
	size_t written = fwrite(data, 1, len, state->fp);
	if (written > 0)
	{
		state->current_size += written;
		state->total_bytes_written += written;
	}

	return (int64_t) written;
}

void rotate_flush(rotate_state *state)
{
	if (state && state->fp)
	{
		fflush(state->fp);
	}
}

const char *rotate_get_current_path(const rotate_state *state)
{
	return state ? state->current_path : NULL;
}

uint64_t rotate_get_current_size(const rotate_state *state)
{
	return state ? state->current_size : 0;
}

