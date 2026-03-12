/* =====================================================================================
*       Filename:  sink
*    Description:
*        Version:  1.0
*        Created:  1/29/26
*       Revision:  none
*       Compiler:  gcc
*         Author:  qihao.xi (qhxi)
===================================================================================== */

#ifndef XLOG_SINK_H
#define XLOG_SINK_H

#include <stdio.h>
#include <stdbool.h>
#include "level.h"

/* Sink type enumeration */
typedef enum sink_type
{
	SINK_TYPE_UNKNOWN = 0,
	SINK_TYPE_CONSOLE = 1,
	SINK_TYPE_FILE = 2,
	SINK_TYPE_SYSLOG = 3,
	SINK_TYPE_CUSTOM = 4
} sink_type;

typedef struct sink_t
{
	void *ctx;

	void (*write)(struct sink_t *sink, const char *data, size_t len);

	void (*flush)(struct sink_t *sink);

	void (*close)(struct sink_t *sink);

	xlog_level level;

	sink_type type;  /* Sink type for split write support */

} sink_t;

/* single sink create/destroy API, specific sink type create/destroy API
 * should be implemented in their own source file and call these API internally */
sink_t *sink_create(void *ctx,
                    void (*write)(struct sink_t *sink, const char *data, size_t len),
                    void (*flush)(struct sink_t *sink),
                    void (*close)(struct sink_t *sink),
                    xlog_level level,
                    sink_type type);

void sink_destroy(sink_t *sink);

void sink_close(sink_t *sink);

void sink_flush(sink_t *sink);

void sink_write(sink_t *sink, const char *data, size_t len);

/* sink manager API */
#define SINK_MANAGER_DEFAULT_CAPACITY 8

typedef struct sink_manager_t
{
	sink_t **sinks;

	size_t count;

	size_t capacity;
} sink_manager_t;

sink_manager_t *sink_manager_create(void);

void sink_manager_destroy(sink_manager_t *manager);

bool sink_manager_add(sink_manager_t *manager, sink_t *sink);

bool sink_manager_remove(sink_manager_t *manager, sink_t *sink);

void sink_manager_write(sink_manager_t *manager, xlog_level level,
                        const char *data, size_t len);

/* Write split: colored data to console sinks, plain data to file sinks */
void sink_manager_write_split(sink_manager_t *manager, xlog_level level,
                              const char *colored_data, size_t colored_len,
                              const char *plain_data, size_t plain_len);

void sink_manager_flush(sink_manager_t *manager);

size_t sink_manager_count(sink_manager_t *manager);

#endif //XLOG_SINK_H
