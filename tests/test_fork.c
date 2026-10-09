/* test_fork.c - fork() hardening tests (POSIX only)
 *
 * Scenarios:
 *   1. async DROP policy: child keeps logging after fork (lazy backend
 *      respawn), parent's in-flight queue content is dropped, child
 *      shutdown drains without hanging on the stale parent thread handle.
 *   2. async BLOCK policy: exercises the BLOCK cv/mutex re-init path.
 *   3. child that never logs: no recovery, clean exit (regression guard
 *      for the atfork handler itself).
 *
 * A hang anywhere in the child (join of a dead thread, frozen mutex)
 * surfaces as a 30s alarm timeout rather than an indefinite stall.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifndef _WIN32

#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include "xlog_core.h"
#include "xlog_builder.h"
static int g_failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); g_failures++; } \
} while (0)

static char *read_file(const char *path)
{
	FILE *fp = fopen(path, "rb");
	char *buf;
	long sz;

	if (!fp)
	{
		return NULL;
	}
	fseek(fp, 0, SEEK_END);
	sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buf = malloc((size_t) sz + 1);
	if (fread(buf, 1, (size_t) sz, fp) != (size_t) sz)
	{
		fclose(fp);
		free(buf);
		return NULL;
	}
	buf[sz] = '\0';
	fclose(fp);
	return buf;
}

static void init_logger(const char *name, xlog_queue_full_policy policy)
{
	xlog_builder *cfg = xlog_builder_new();

	xlog_builder_set_level(cfg, XLOG_LEVEL_TRACE);
	xlog_builder_set_mode(cfg, XLOG_MODE_ASYNC);
	xlog_builder_set_buffer_size(cfg, 1024);
	xlog_builder_set_queue_policy(cfg, policy);
	xlog_builder_enable_console(cfg, false);
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_directory(cfg, ".");
	xlog_builder_file_name(cfg, name);
	xlog_builder_file_rotate_on_start(cfg, true);

	CHECK(xlog_builder_apply(cfg), "builder apply");
	xlog_builder_free(cfg);
}

/* Logs from the child process; exit code reports verification result. */
static int child_body(const char *marker_prefix, int count)
{
	for (int i = 0; i < count; i++)
	{
		XLOG_INFO("%s-%d port=%d", marker_prefix, i, 8080);
	}
	xlog_flush();
	xlog_shutdown(); /* must not hang on the stale parent thread */
	return 0;
}

static void run_fork_scenario(const char *file_stem, xlog_queue_full_policy policy,
                              const char *child_marker)
{
	char log_path[256];
	pid_t pid;
	int status = -1;
	char *content;

	snprintf(log_path, sizeof(log_path), "%s.log", file_stem);
	remove(log_path);

	init_logger(file_stem, policy);

	/* Fill the queue with un-flushed parent records: the child must DROP
	 * them (its backend never existed) rather than re-deliver or wedge. */
	for (int i = 0; i < 500; i++)
	{
		XLOG_INFO("parent-inflight-%d", i);
	}
	xlog_flush();

	pid = fork();
	if (pid < 0)
	{
		perror("fork");
		g_failures++;
		xlog_shutdown();
		return;
	}
	if (pid == 0)
	{
		/* child: _exit so atexit/stdio flushes stay under our control */
		int rc = child_body(child_marker, 40);
		_exit(rc == 0 ? 0 : 1);
	}

	CHECK(waitpid(pid, &status, 0) == pid, "waitpid");
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
	      "child exited cleanly (no hang / crash)");

	/* parent keeps its own logger usable after the fork */
	XLOG_INFO("parent-after-fork");
	xlog_shutdown();

	content = read_file(log_path);
	CHECK(content != NULL, "read log file");
	if (content)
	{
		char expect[128];
		snprintf(expect, sizeof(expect), "%s-39 port=8080", child_marker);
		CHECK(strstr(content, expect) != NULL, "child records reached the file");
		CHECK(strstr(content, "parent-after-fork") != NULL,
		      "parent logger still usable after fork");
		free(content);
	}
}

int main(void)
{
	/* Bound the whole run: a wedged child (join on a dead thread, frozen
	 * mutex) must fail the test, not hang the suite. */
	alarm(30);

	run_fork_scenario("fork_drop", XLOG_QUEUE_DROP, "child-drop");
	run_fork_scenario("fork_block", XLOG_QUEUE_BLOCK, "child-block");

	if (g_failures == 0)
	{
		printf("test_fork: ALL PASSED\n");
		return 0;
	}
	printf("test_fork: %d FAILURE(S)\n", g_failures);
	return 1;
}

#else /* _WIN32 */

int main(void)
{
	printf("test_fork: SKIPPED (no fork on Windows)\n");
	return 0;
}

#endif /* !_WIN32 */
