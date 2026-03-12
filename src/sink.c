/* =====================================================================================
*       Filename:  sink
*    Description:
*        Version:  1.0
*        Created:  1/29/26
*       Revision:  none
*       Compiler:  gcc
*         Author:  qihao.xi (qhxi)
===================================================================================== */

#include <stdlib.h>
#include <string.h>
#include "sink.h"

/* ============================================================================
 * Single Sink API Implementation
 * ============================================================================ */

sink_t *sink_create(void *ctx,
                    void (*write)(struct sink_t *sink, const char *data, size_t len),
                    void (*flush)(struct sink_t *sink),
                    void (*close)(struct sink_t *sink),
                    xlog_level level,
                    sink_type type)
{
	sink_t *sink = (sink_t *) malloc(sizeof(sink_t));
	if (!sink)
	{
		return NULL;
	}
	sink->ctx = ctx;
	sink->write = write;
	sink->flush = flush;
	sink->close = close;
	sink->level = level;
	sink->type = type;
	return sink;
}

void sink_destroy(sink_t *sink)
{
	if (sink)
	{
		if (sink->close)
		{
			sink->close(sink);
		}
		free(sink);
	}
}

void sink_close(sink_t *sink)
{
	if (sink && sink->close)
	{
		sink->close(sink);
	}
}

void sink_flush(sink_t *sink)
{
	if (sink && sink->flush)
	{
		sink->flush(sink);
	}
}

void sink_write(sink_t *sink, const char *data, size_t len)
{
	if (sink && sink->write)
	{
		sink->write(sink, data, len);
	}
}

/* ============================================================================
 * Sink Manager API Implementation
 * ============================================================================ */

sink_manager_t *sink_manager_create(void)
{
	sink_manager_t *manager = calloc(1, sizeof(sink_manager_t));
	if (!manager)
	{
		return NULL;
	}

	manager->capacity = SINK_MANAGER_DEFAULT_CAPACITY;
	manager->sinks = calloc(manager->capacity, sizeof(sink_t *));
	if (!manager->sinks)
	{
		free(manager);
		return NULL;
	}

	manager->count = 0;
	return manager;
}

void sink_manager_destroy(sink_manager_t *manager)
{
	if (!manager)
	{
		return;
	}

	/* Destroy all sinks */
	for (size_t i = 0; i < manager->count; i++)
	{
		sink_destroy(manager->sinks[i]);
	}

	free(manager->sinks);
	free(manager);
}

bool sink_manager_add(sink_manager_t *manager, sink_t *sink)
{
	if (!manager || !sink)
	{
		return false;
	}

	/* Grow array if needed */
	if (manager->count >= manager->capacity)
	{
		size_t new_capacity = manager->capacity * 2;
		sink_t **new_sinks = realloc(manager->sinks, new_capacity * sizeof(sink_t *));
		if (!new_sinks)
		{
			return false;
		}

		manager->sinks = new_sinks;
		manager->capacity = new_capacity;
	}

	manager->sinks[manager->count++] = sink;
	return true;
}

bool sink_manager_remove(sink_manager_t *manager, sink_t *sink)
{
	if (!manager || !sink)
	{
		return false;
	}

	for (size_t i = 0; i < manager->count; i++)
	{
		if (manager->sinks[i] == sink)
		{
			/* Shift remaining sinks */
			memmove(&manager->sinks[i], &manager->sinks[i + 1],
			        (manager->count - i - 1) * sizeof(sink_t *));
			manager->count--;
			return true;
		}
	}

	return false;
}

void sink_manager_write(sink_manager_t *manager, xlog_level level,
                        const char *data, size_t len)
{
	if (!manager || !data || len == 0)
	{
		return;
	}

	for (size_t i = 0; i < manager->count; i++)
	{
		sink_t *sink = manager->sinks[i];
		/* Only write to sinks with level <= message level */
		if (sink && sink->write && level >= sink->level)
		{
			sink->write(sink, data, len);
		}
	}
}

void sink_manager_write_split(sink_manager_t *manager, xlog_level level,
                              const char *colored_data, size_t colored_len,
                              const char *plain_data, size_t plain_len)
{
	if (!manager)
	{
		return;
	}

	for (size_t i = 0; i < manager->count; i++)
	{
		sink_t *sink = manager->sinks[i];
		if (!sink || !sink->write || level < sink->level)
		{
			continue;
		}

		/* Console sinks get colored output, others get plain */
		if (sink->type == SINK_TYPE_CONSOLE && colored_data && colored_len > 0)
		{
			sink->write(sink, colored_data, colored_len);
		}
		else if (plain_data && plain_len > 0)
		{
			sink->write(sink, plain_data, plain_len);
		}
		else if (colored_data && colored_len > 0)
		{
			/* Fallback to colored if no plain available */
			sink->write(sink, colored_data, colored_len);
		}
	}
}

void sink_manager_flush(sink_manager_t *manager)
{
	if (!manager)
	{
		return;
	}

	for (size_t i = 0; i < manager->count; i++)
	{
		sink_t *sink = manager->sinks[i];
		if (sink && sink->flush)
		{
			sink->flush(sink);
		}
	}
}

size_t sink_manager_count(sink_manager_t *manager)
{
	return manager ? manager->count : 0;
}

