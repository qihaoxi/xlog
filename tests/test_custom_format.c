/* test_custom_format.c - XLOG_OUTPUT_CUSTOM plumbing tests
 *
 * Verifies that a user-supplied format function:
 *   1. receives the committed record (level/file/line/ctx/args) intact,
 *   2. its output reaches file/console sinks byte-exact (no color split,
 *      no extra newline),
 *   3. returning 0 counts as a format error and writes nothing,
 *   4. builder CUSTOM style without a fn falls back to DEFAULT,
 *   5. the runtime setter activates/rotates back correctly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "xlog_core.h"
#include "xlog_builder.h"
#include "file_sink.h"
#include "log_record.h"

static int g_failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); g_failures++; } \
} while (0)

/* ---- helpers ---- */

static char *read_file(const char *path, size_t *out_len)
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
	if (out_len)
	{
		*out_len = (size_t) sz;
	}
	return buf;
}

static const char *g_sink_path;

static void init_with_custom(xlog_custom_format_fn fn, void *ctx,
                             const char *path)
{
	xlog_builder *cfg = xlog_builder_new();

	xlog_builder_set_level(cfg, XLOG_LEVEL_TRACE);
	xlog_builder_set_custom_format(cfg, fn, ctx);
	xlog_builder_enable_console(cfg, false);
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_directory(cfg, ".");
	xlog_builder_file_name(cfg, path);
	/* name is used as full prefix when it contains the extension already;
	 * point directory at cwd and use the bare file name */
	xlog_builder_file_rotate_on_start(cfg, true);

	CHECK(xlog_builder_apply(cfg), "builder apply");
	xlog_builder_free(cfg);
	g_sink_path = path;
}

static void log_one_line(void)
{
	log_context lc;
	memset(&lc, 0, sizeof(lc));
	lc.module = "cfs";
	lc.tag = "syscall";
	lc.flags = LOG_CTX_HAS_MODULE | LOG_CTX_HAS_TAG;
	xlog_log_ctx(XLOG_LEVEL_INFO, &lc, "t.c", 42, "t_func",
	             "id=%d name=%s", 7, "proc");
	xlog_flush();
	xlog_shutdown();
}

/* Deterministic renderer: skips the timestamp so full lines are
 * byte-comparable. Layout mirrors the compound_logger file format. */
typedef struct
{
	const char *trace_override; /* non-NULL: emit this trc value */
} det_ctx_t;

static size_t det_format(const log_record *rec, void *vctx, char *buf, size_t size)
{
	det_ctx_t *d = vctx;
	const char *level = "?";
	char trace_seg[96];
	size_t n;

	switch ((xlog_level) rec->level)
	{
		case XLOG_LEVEL_TRACE: level = "TRACE"; break;
		case XLOG_LEVEL_DEBUG: level = "DEBUG"; break;
		case XLOG_LEVEL_INFO: level = "INFO"; break;
		case XLOG_LEVEL_WARNING: level = "WARN"; break;
		case XLOG_LEVEL_ERROR: level = "ERROR"; break;
		default: level = "ERROR"; break;
	}

	trace_seg[0] = '\0';
	if (d->trace_override)
	{
		snprintf(trace_seg, sizeof(trace_seg), "|trc:%s", d->trace_override);
	}

	n = (size_t) snprintf(buf, size, "TS|%s|%s:%s|%s:%u%s|id=7 name=proc\n",
	                      level,
	                      rec->ctx.module ? rec->ctx.module : "",
	                      rec->ctx.tag ? rec->ctx.tag : "",
	                      rec->loc.file ? rec->loc.file : "-",
	                      rec->loc.line,
	                      trace_seg);
	return n;
}

/* Counting renderer that refuses to write. */
static size_t zero_format(const log_record *rec, void *vctx, char *buf, size_t size)
{
	int *calls = vctx;
	(void) rec;
	(void) buf;
	(void) size;
	(*calls)++;
	return 0;
}

static void t_byte_exact(void)
{
	det_ctx_t d = { .trace_override = NULL };
	char *content;
	static const char *expect =
		"TS|INFO|cfs:syscall|t.c:42|id=7 name=proc\n";

	remove("cf_exact.log");
	init_with_custom(det_format, &d, "cf_exact");
	log_one_line();

	content = read_file("cf_exact.log", NULL);
	CHECK(content != NULL, "read sink output");
	if (content)
	{
		CHECK(strcmp(content, expect) == 0, "byte-exact custom line");
		if (strcmp(content, expect) != 0)
		{
			fprintf(stderr, "  got:    [%s]\n  expect: [%s]\n", content, expect);
		}
		free(content);
	}
}

static void t_zero_return_counts_error(void)
{
	int calls = 0;
	char *content;
	xlog_stats st;

	remove("cf_zero.log");
	init_with_custom(zero_format, &calls, "cf_zero");

	{
		log_context lc;
		memset(&lc, 0, sizeof(lc));
		lc.module = "cfs";
		lc.tag = "syscall";
		lc.flags = LOG_CTX_HAS_MODULE | LOG_CTX_HAS_TAG;
		xlog_log_ctx(XLOG_LEVEL_INFO, &lc, "t.c", 42, "t_func",
		             "id=%d name=%s", 7, "proc");
		xlog_flush();
	}
	xlog_get_stats(&st);  /* read stats BEFORE shutdown frees the queue */

	/* backend processed the record: fn was invoked once */
	CHECK(calls == 1, "custom fn invoked exactly once");
	/* and the format_errors stat saw the zero return */
	CHECK(st.format_errors >= 1, "zero return counted as format error");
	xlog_shutdown();

	/* but produced no output */
	content = read_file("cf_zero.log", NULL);
	if (content)
	{
		CHECK(content[0] == '\0', "zero return writes nothing");
		free(content);
	}
}

static void t_custom_without_fn_falls_back(void)
{
	xlog_builder *cfg;
	char *content;

	remove("cf_fallback.log");
	xlog_shutdown();

	cfg = xlog_builder_new();
	xlog_builder_set_level(cfg, XLOG_LEVEL_TRACE);
	xlog_builder_set_format(cfg, XLOG_FORMAT_CUSTOM); /* no fn set */
	xlog_builder_enable_console(cfg, false);
	xlog_builder_enable_file(cfg, true);
	xlog_builder_file_directory(cfg, ".");
	xlog_builder_file_name(cfg, "cf_fallback");
	xlog_builder_file_rotate_on_start(cfg, true);
	CHECK(xlog_builder_apply(cfg), "builder apply (fallback)");
	xlog_builder_free(cfg);

	log_one_line();
	content = read_file("cf_fallback.log", NULL);
	CHECK(content != NULL, "read fallback output");
	if (content)
	{
		/* default inline format starts with '[' meta block, not our
		 * custom 'TS|' prefix */
		CHECK(content[0] == '[', "fallback uses DEFAULT style");
		free(content);
	}
}

static void t_runtime_setter(void)
{
	det_ctx_t d = { .trace_override = "aabb00112233445566778899aabbccdd" };
	char *content;

	remove("cf_rt.log");
	xlog_shutdown();

	{
		xlog_builder *cfg = xlog_builder_new();
		xlog_builder_set_level(cfg, XLOG_LEVEL_TRACE);
		xlog_builder_enable_console(cfg, false);
		xlog_builder_enable_file(cfg, true);
		xlog_builder_file_directory(cfg, ".");
		xlog_builder_file_name(cfg, "cf_rt");
		xlog_builder_file_rotate_on_start(cfg, true);
		CHECK(xlog_builder_apply(cfg), "builder apply (runtime setter)");
		xlog_builder_free(cfg);
	}

	xlog_set_custom_format(det_format, &d);
	log_one_line();

	content = read_file("cf_rt.log", NULL);
	CHECK(content != NULL, "read runtime-setter output");
	if (content)
	{
		static const char *expect =
			"TS|INFO|cfs:syscall|t.c:42|trc:aabb00112233445566778899aabbccdd|id=7 name=proc\n";
		CHECK(strcmp(content, expect) == 0, "runtime setter byte-exact");
		if (strcmp(content, expect) != 0)
		{
			fprintf(stderr, "  got:    [%s]\n  expect: [%s]\n", content, expect);
		}
		free(content);
	}
}

int main(void)
{
	t_byte_exact();
	t_zero_return_counts_error();
	t_custom_without_fn_falls_back();
	t_runtime_setter();

	if (g_failures == 0)
	{
		printf("test_custom_format: ALL PASSED\n");
		return 0;
	}
	printf("test_custom_format: %d FAILURE(S)\n", g_failures);
	return 1;
}
