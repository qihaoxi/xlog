/* =====================================================================================
*       Filename:  level.h
*    Description:  Log level definitions
*        Version:  1.0
*        Created:  1/29/26
*       Compiler:  gcc
*         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
*        Company:  Onecloud
===================================================================================== */

#ifndef XLOG_LEVEL_H
#define XLOG_LEVEL_H

/* Skip if public API (include/xlog.h) already defined log_level */
#ifndef XLOG_H

typedef enum log_level
{
	LOG_LEVEL_TRACE = 0,
	LOG_LEVEL_DEBUG = 1,
	LOG_LEVEL_INFO = 2,
	LOG_LEVEL_WARNING = 3,
	LOG_LEVEL_ERROR = 4,
	LOG_LEVEL_FATAL = 5,
	LOG_LEVEL_OFF = 6
} log_level;

#endif /* XLOG_H */

#endif /* XLOG_LEVEL_H */
