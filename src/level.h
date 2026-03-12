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

/* Skip if public API (include/xlog.h) already defined xlog_level */
#ifndef XLOG_H

typedef enum xlog_level
{
	XLOG_LEVEL_TRACE = 0,
	XLOG_LEVEL_DEBUG = 1,
	XLOG_LEVEL_INFO = 2,
	XLOG_LEVEL_WARNING = 3,
	XLOG_LEVEL_ERROR = 4,
	XLOG_LEVEL_FATAL = 5,
	XLOG_LEVEL_OFF = 6
} xlog_level;

#endif /* XLOG_H */

#endif /* XLOG_LEVEL_H */
