/* =====================================================================================
*       Filename:  sink
*    Description:
*        Version:  1.0
*        Created:  1/29/26
*       Revision:  none
*       Compiler:  gcc
*         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
*        Company:  Onecloud
===================================================================================== */

#ifndef XLOG_SINK_H
#define XLOG_SINK_H

#include <stdio.h>
#include <stdbool.h>
#include "level.h"

/*
 * ============================================================================
 * Sink 定义
 * ============================================================================
 *
 * sink_t 是日志输出的抽象接口，包含：
 *   - ctx: 具体 sink 的上下文（如文件句柄、网络连接等）
 *   - write: 写入日志数据的函数指针
 *   - flush: 刷新缓冲区的函数指针
 *   - close: 关闭资源的函数指针
 *   - level: 该 sink 的日志级别，低于该级别的日志会被过滤掉
 *   - type: sink 类型（用于区分 console/file 等）
 *
 * 每种具体的 sink（如 file_sink、console_sink）都应该实现这些函数，并通过 sink_create 创建实例。
 *
 * ============================================================================
 * Sink Manager 定义
 * ============================================================================
 *
 * sink_manager_t 管理多个 sink，提供添加和移除 sink 的 API。
 *
 */

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

	log_level level;

	sink_type type;  /* Sink type for split write support */

} sink_t;

/* single sink create/destroy API, specific sink type create/destroy API
 * should be implemented in their own source file and call these API internally */
sink_t *sink_create(void *ctx,
                    void (*write)(struct sink_t *sink, const char *data, size_t len),
                    void (*flush)(struct sink_t *sink),
                    void (*close)(struct sink_t *sink),
                    log_level level,
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

void sink_manager_write(sink_manager_t *manager, log_level level,
                        const char *data, size_t len);

/* Write split: colored data to console sinks, plain data to file sinks */
void sink_manager_write_split(sink_manager_t *manager, log_level level,
                              const char *colored_data, size_t colored_len,
                              const char *plain_data, size_t plain_len);

void sink_manager_flush(sink_manager_t *manager);

size_t sink_manager_count(sink_manager_t *manager);

#endif //XLOG_SINK_H
