/* =====================================================================================
 *       Filename:  test_root_quota.c
 *    Description:  跨目录根配额测试(rotate_enforce_root_quota,doc116 §3.2)
 *        Version:  1.0
 *        Created:  2026-09-14
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

#ifndef XLOG_PLATFORM_WINDOWS
#include <fcntl.h>
#include <sys/stat.h>
#endif

#define TEST_ROOT "/tmp/xlog_root_quota_test"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { printf("  FAILED: %s\n", msg); tests_failed++; return; } \
} while(0)

#define TEST_PASS(msg) do { printf("  ok: %s\n", msg); tests_passed++; } while(0)

static void rm_rf_root(void)
{
	char cmd[512];
	xlog_snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_ROOT);
	(void) system(cmd);
}

static void mkdirs(const char *path)
{
	xlog_mkdir_p(path);
}

static void write_file(const char *path, size_t bytes)
{
	FILE *f = fopen(path, "wb");
	if (!f)
	{
		return;
	}
	for (size_t i = 0; i < bytes; i++)
	{
		fputc('x', f);
	}
	fclose(f);
}

static void set_mtime_old(const char *path, time_t t)
{
#ifndef XLOG_PLATFORM_WINDOWS
	struct stat st;
	if (stat(path, &st) != 0)
	{
		return;
	}
	struct timespec times[2];
	times[0].tv_sec = t;
	times[0].tv_nsec = 0;
	times[1].tv_sec = t;
	times[1].tv_nsec = 0;
	utimensat(AT_FDCWD, path, times, 0);
#else
	(void) path;
	(void) t;
#endif
}

static rotate_config make_config(const char *dir, uint64_t root_quota)
{
	rotate_config c = {0};
	c.base_name = "pel";
	c.extension = ".log";
	c.directory = dir;
	c.max_file_size = 50 * XLOG_MB;
	c.max_dir_size = 500 * XLOG_MB;
	c.max_files = 100;
	c.max_root_size = root_quota;
	c.rotate_on_start = false;
	c.compress_old = false;
	return c;
}

/* 靶1:跨目录按 mtime 淘汰 + active/异名零触碰 + grace 保护 + 空目录回收 */
static void test_cross_dir_eviction(void)
{
	char a[512], b[512], c[512], path[600];
	xlog_snprintf(a, sizeof(a), "%s%c1001", TEST_ROOT, XLOG_PATH_SEP);
	xlog_snprintf(b, sizeof(b), "%s%c1002", TEST_ROOT, XLOG_PATH_SEP);
	xlog_snprintf(c, sizeof(c), "%s%c1003", TEST_ROOT, XLOG_PATH_SEP);
	mkdirs(a); mkdirs(b); mkdirs(c);

	time_t now = time(NULL);
	time_t old1 = now - 4000;   /* 超出 grace(300s)的最旧 */
	time_t old2 = now - 3500;
	time_t old3 = now - 3000;
	time_t fresh = now - 10;    /* grace 保护期内,名字最"旧"也须保留 */

	/* a:最旧归档 + 异名文件(零触碰) */
	xlog_snprintf(path, sizeof(path), "%s%cpel-20250101.log", a, XLOG_PATH_SEP);
	write_file(path, 100); set_mtime_old(path, old1);
	xlog_snprintf(path, sizeof(path), "%s%cother-20250101.log", a, XLOG_PATH_SEP);
	write_file(path, 100); set_mtime_old(path, old1);
	/* b:次旧归档(淘汰后目录变空 → best-effort rmdir) */
	xlog_snprintf(path, sizeof(path), "%s%cpel-20250102.log", b, XLOG_PATH_SEP);
	write_file(path, 100); set_mtime_old(path, old2);
	/* c:本实例目录:第三旧归档 + grace 保护的新归档;active 由 rotate_init 创建 */
	xlog_snprintf(path, sizeof(path), "%s%cpel-20250103.log", c, XLOG_PATH_SEP);
	write_file(path, 100); set_mtime_old(path, old3);
	xlog_snprintf(path, sizeof(path), "%s%cpel-20241231.log", c, XLOG_PATH_SEP);
	write_file(path, 100); set_mtime_old(path, fresh);

	/* 配额 250B:old 归档 100×3 + grace 内 100 = 400(active 0)→ 需淘汰至 ≤250:
	 * 按 mtime 升序 old1(删,300>250)、old2(删,200≤250 停);old3 与 grace 文件保留 */
	rotate_state st;
	rotate_config cfg = make_config(c, 250);
	TEST_ASSERT(rotate_init(&st, &cfg), "rotate_init with root quota");

	xlog_snprintf(path, sizeof(path), "%s%cpel-20250101.log", a, XLOG_PATH_SEP);
	TEST_ASSERT(!xlog_file_exists(path), "oldest cross-dir archive evicted");
	xlog_snprintf(path, sizeof(path), "%s%cother-20250101.log", a, XLOG_PATH_SEP);
	TEST_ASSERT(xlog_file_exists(path), "foreign-named file untouched");
	xlog_snprintf(path, sizeof(path), "%s%cpel-20250102.log", b, XLOG_PATH_SEP);
	TEST_ASSERT(!xlog_file_exists(path), "second-oldest cross-dir archive evicted");
	TEST_ASSERT(!xlog_is_directory(b), "emptied sibling dir reclaimed");
	xlog_snprintf(path, sizeof(path), "%s%cpel-20250103.log", c, XLOG_PATH_SEP);
	TEST_ASSERT(xlog_file_exists(path), "newer archive kept (within quota)");
	xlog_snprintf(path, sizeof(path), "%s%cpel-20241231.log", c, XLOG_PATH_SEP);
	TEST_ASSERT(xlog_file_exists(path), "grace-protected fresh archive kept");
	xlog_snprintf(path, sizeof(path), "%s%cpel.log", c, XLOG_PATH_SEP);
	TEST_ASSERT(xlog_file_exists(path), "active file never evicted");

	rotate_cleanup(&st);
	TEST_PASS("cross-dir eviction respects mtime/quota/grace/foreign/active");
}

/* 靶2:配额内零动作 */
static void test_within_quota_noop(void)
{
	char a[512], c[512], path[600];
	xlog_snprintf(a, sizeof(a), "%s%c2001", TEST_ROOT, XLOG_PATH_SEP);
	xlog_snprintf(c, sizeof(c), "%s%c2002", TEST_ROOT, XLOG_PATH_SEP);
	mkdirs(a); mkdirs(c);

	xlog_snprintf(path, sizeof(path), "%s%cpel-20250101.log", a, XLOG_PATH_SEP);
	write_file(path, 100);
	set_mtime_old(path, time(NULL) - 4000);

	rotate_state st;
	rotate_config cfg = make_config(c, 10 * XLOG_MB);
	TEST_ASSERT(rotate_init(&st, &cfg), "rotate_init within quota");
	TEST_ASSERT(xlog_file_exists(path), "no eviction when within quota");
	rotate_cleanup(&st);
	TEST_PASS("within quota: no-op");
}

/* 靶3:锁互斥(他持锁→跳过)与陈锁抢占(mtime>120s) */
static void test_lock_semantics(void)
{
	char c[512], path[600], lock[600], arch[600];
	xlog_snprintf(c, sizeof(c), "%s%c3001", TEST_ROOT, XLOG_PATH_SEP);
	mkdirs(c);
	xlog_snprintf(arch, sizeof(arch), "%s%cpel-20250101.log", c, XLOG_PATH_SEP);
	write_file(arch, 100);
	set_mtime_old(arch, time(NULL) - 4000);
	xlog_snprintf(lock, sizeof(lock), "%s%c%s", TEST_ROOT, XLOG_PATH_SEP,
	              ".xlog-quota.lock");

	/* 他持锁(新鲜)→ 跳过,不淘汰 */
	TEST_ASSERT(xlog_mkdir_lock(lock), "acquire lock manually");
	rotate_state st;
	rotate_config cfg = make_config(c, 10);   /* 远超配额 */
	TEST_ASSERT(rotate_init(&st, &cfg), "rotate_init with foreign lock held");
	TEST_ASSERT(xlog_file_exists(arch), "fresh foreign lock -> sweep skipped");
	rotate_cleanup(&st);

	/* 陈锁(mtime 老)→ 抢占后执行淘汰 */
	set_mtime_old(lock, time(NULL) - 600);
	rotate_state st2;
	rotate_config cfg2 = make_config(c, 10);
	TEST_ASSERT(rotate_init(&st2, &cfg2), "rotate_init stealing stale lock");
	TEST_ASSERT(!xlog_file_exists(arch), "stale lock stolen -> sweep ran");
	TEST_ASSERT(!xlog_is_directory(lock), "lock released after sweep");
	rotate_cleanup(&st2);
	(void) path;
	TEST_PASS("lock: mutual exclusion + stale steal");
}

int main(void)
{
	rm_rf_root();

	test_cross_dir_eviction();
	rm_rf_root();
	test_within_quota_noop();
	rm_rf_root();
	test_lock_semantics();
	rm_rf_root();

	printf("\n=== test_root_quota: %d passed, %d failed ===\n",
	       tests_passed, tests_failed);
	return tests_failed == 0 ? 0 : 1;
}
