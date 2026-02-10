/* =====================================================================================
*       Filename:  level
*    Description:
*        Version:  1.0
*        Created:  1/29/26
*       Revision:  none
*       Compiler:  gcc
*         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
*        Company:  Onecloud
===================================================================================== */

#ifndef XLOG_CONFIG_H
#define XLOG_CONFIG_H

#include "level.h"

typedef struct config
{
	log_level level;
	const char *app_name;
	const char *path; // log directory
} config;

#define _X_ARG_TYPE(x) _Generic((x), \
    int: RB_ARG_INT64, long: RB_ARG_INT64, long long: RB_ARG_INT64, \
    unsigned: RB_ARG_INT64, unsigned long: RB_ARG_INT64, unsigned long long: RB_ARG_INT64, \
    double: RB_ARG_DOUBLE, float: RB_ARG_DOUBLE, \
    char*: RB_ARG_STATIC_STR, const char*: RB_ARG_STATIC_STR, \
    default: RB_ARG_INT64)
#endif //XLOG_CONFIG_H
