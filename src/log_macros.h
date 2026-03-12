/* =====================================================================================
 *       Filename:  log_macros.h
 *    Description:  High-performance logging macros with deferred formatting
 *                  Provides XLOG_INFO, XLOG_DEBUG, etc. macros
 *        Version:  1.0
 *        Created:  2026-02-07
 *       Compiler:  gcc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#ifndef XLOG_LOG_MACROS_H
#define XLOG_LOG_MACROS_H

#include "log_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 参数计数宏
 * ============================================================================
 * 自动统计可变参数个数（最多支持 16 个）
 */
#define LOG_ARG_COUNT(...) \
    LOG_ARG_COUNT_IMPL(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define LOG_ARG_COUNT_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, \
                           _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N

/* ============================================================================
 * 参数展开辅助宏
 * ============================================================================ */
#define LOG_EXPAND(x) x
#define LOG_CONCAT(a, b) LOG_CONCAT_IMPL(a, b)
#define LOG_CONCAT_IMPL(a, b) a##b

/* ============================================================================
 * 填充参数到 log_record 的宏
 * ============================================================================
 * 每个 LOG_FILL_ARG_N 宏负责填充 N 个参数
 */
#define LOG_FILL_ARG_0(rec)
#define LOG_FILL_ARG_1(rec, a1) \
    do { \
        (rec)->arg_types[0] = LOG_ARG_TYPE_OF(a1); \
        (rec)->arg_values[0] = LOG_ARG_TO_U64(a1); \
    } while(0)

#define LOG_FILL_ARG_2(rec, a1, a2) \
    do { \
        LOG_FILL_ARG_1(rec, a1); \
        (rec)->arg_types[1] = LOG_ARG_TYPE_OF(a2); \
        (rec)->arg_values[1] = LOG_ARG_TO_U64(a2); \
    } while(0)

#define LOG_FILL_ARG_3(rec, a1, a2, a3) \
    do { \
        LOG_FILL_ARG_2(rec, a1, a2); \
        (rec)->arg_types[2] = LOG_ARG_TYPE_OF(a3); \
        (rec)->arg_values[2] = LOG_ARG_TO_U64(a3); \
    } while(0)

#define LOG_FILL_ARG_4(rec, a1, a2, a3, a4) \
    do { \
        LOG_FILL_ARG_3(rec, a1, a2, a3); \
        (rec)->arg_types[3] = LOG_ARG_TYPE_OF(a4); \
        (rec)->arg_values[3] = LOG_ARG_TO_U64(a4); \
    } while(0)

#define LOG_FILL_ARG_5(rec, a1, a2, a3, a4, a5) \
    do { \
        LOG_FILL_ARG_4(rec, a1, a2, a3, a4); \
        (rec)->arg_types[4] = LOG_ARG_TYPE_OF(a5); \
        (rec)->arg_values[4] = LOG_ARG_TO_U64(a5); \
    } while(0)

#define LOG_FILL_ARG_6(rec, a1, a2, a3, a4, a5, a6) \
    do { \
        LOG_FILL_ARG_5(rec, a1, a2, a3, a4, a5); \
        (rec)->arg_types[5] = LOG_ARG_TYPE_OF(a6); \
        (rec)->arg_values[5] = LOG_ARG_TO_U64(a6); \
    } while(0)

#define LOG_FILL_ARG_7(rec, a1, a2, a3, a4, a5, a6, a7) \
    do { \
        LOG_FILL_ARG_6(rec, a1, a2, a3, a4, a5, a6); \
        (rec)->arg_types[6] = LOG_ARG_TYPE_OF(a7); \
        (rec)->arg_values[6] = LOG_ARG_TO_U64(a7); \
    } while(0)

#define LOG_FILL_ARG_8(rec, a1, a2, a3, a4, a5, a6, a7, a8) \
    do { \
        LOG_FILL_ARG_7(rec, a1, a2, a3, a4, a5, a6, a7); \
        (rec)->arg_types[7] = LOG_ARG_TYPE_OF(a8); \
        (rec)->arg_values[7] = LOG_ARG_TO_U64(a8); \
    } while(0)

/* 调度宏：根据参数数量选择对应的填充宏 */
#define LOG_FILL_ARGS(rec, n, ...) \
    LOG_CONCAT(LOG_FILL_ARG_, n)(rec, ##__VA_ARGS__)

/* ============================================================================
 * 核心日志提交函数声明
 * ============================================================================
 * 这些函数在 xlog.c 中实现，用于将日志记录提交到 ring_buffer
 */

/**
 * 获取可写的日志槽位
 * @return 日志记录指针，失败返回 NULL
 */
log_record *xlog_acquire_slot(void);

/**
 * 提交日志记录
 * @param rec 日志记录指针
 */
void xlog_commit_slot(log_record *rec);

/**
 * 获取当前纳秒时间戳
 */
uint64_t xlog_now_ns(void);

/**
 * 获取当前线程 ID
 */
uint32_t xlog_get_tid(void);

/**
 * 检查日志级别是否启用
 */
bool xlog_level_enabled(log_level level);

/* ============================================================================
 * 高性能日志宏（延迟格式化）
 * ============================================================================
 * 使用示例：
 *   XLOG_INFO("User %s logged in, id=%d", username, user_id);
 *   XLOG_DEBUG("Processing item {} with value {}", item_id, value);
 *
 * 关键特性：
 *   1. 编译期类型推导（使用 _Generic）
 *   2. 零格式化开销（在后台线程格式化）
 *   3. 自动捕获源码位置
 */

/* 无参数版本 */
#define XLOG_IMPL_0(level, fmt) \
    do { \
        if (!xlog_level_enabled(level)) break; \
        log_record *_rec = xlog_acquire_slot(); \
        if (_rec) { \
            log_record_set_meta(_rec, level, fmt, \
                __FILE__, __func__, __LINE__, \
                xlog_get_tid(), xlog_now_ns()); \
            _rec->arg_count = 0; \
            xlog_commit_slot(_rec); \
        } \
    } while(0)

/* 有参数版本 */
#define XLOG_IMPL_N(level, fmt, n, ...) \
    do { \
        if (!xlog_level_enabled(level)) break; \
        log_record *_rec = xlog_acquire_slot(); \
        if (_rec) { \
            log_record_set_meta(_rec, level, fmt, \
                __FILE__, __func__, __LINE__, \
                xlog_get_tid(), xlog_now_ns()); \
            _rec->arg_count = (n > LOG_MAX_ARGS) ? LOG_MAX_ARGS : n; \
            LOG_FILL_ARGS(_rec, n, ##__VA_ARGS__); \
            xlog_commit_slot(_rec); \
        } \
    } while(0)

/* 选择器宏 */
#define XLOG_SELECT(_0, _1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME

#define XLOG_DISPATCH(level, fmt, ...) \
    XLOG_SELECT(, ##__VA_ARGS__, \
        XLOG_IMPL_N, XLOG_IMPL_N, XLOG_IMPL_N, XLOG_IMPL_N, \
        XLOG_IMPL_N, XLOG_IMPL_N, XLOG_IMPL_N, XLOG_IMPL_N, \
        XLOG_IMPL_0)(level, fmt, LOG_ARG_COUNT(__VA_ARGS__), ##__VA_ARGS__)

/* ============================================================================
 * 用户级日志宏
 * ============================================================================ */
#define XLOG_TRACE(fmt, ...)  XLOG_DISPATCH(LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
#define XLOG_DEBUG(fmt, ...)  XLOG_DISPATCH(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define XLOG_INFO(fmt, ...)   XLOG_DISPATCH(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define XLOG_WARN(fmt, ...)   XLOG_DISPATCH(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define XLOG_ERROR(fmt, ...)  XLOG_DISPATCH(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define XLOG_FATAL(fmt, ...)  XLOG_DISPATCH(LOG_LEVEL_FATAL, fmt, ##__VA_ARGS__)

/* 别名 */
#define XLOG_WARNING(fmt, ...) XLOG_WARN(fmt, ##__VA_ARGS__)

/* ============================================================================
 * 条件日志宏
 * ============================================================================ */
#define XLOG_IF(level, cond, fmt, ...) \
    do { \
        if (cond) { \
            XLOG_DISPATCH(level, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define XLOG_INFO_IF(cond, fmt, ...)  XLOG_IF(LOG_LEVEL_INFO, cond, fmt, ##__VA_ARGS__)
#define XLOG_WARN_IF(cond, fmt, ...)  XLOG_IF(LOG_LEVEL_WARNING, cond, fmt, ##__VA_ARGS__)
#define XLOG_ERROR_IF(cond, fmt, ...) XLOG_IF(LOG_LEVEL_ERROR, cond, fmt, ##__VA_ARGS__)

/* ============================================================================
 * 带返回值检查的日志宏
 * ============================================================================ */
#define XLOG_CHECK(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            XLOG_ERROR("CHECK FAILED: " #cond " - " fmt, ##__VA_ARGS__); \
        } \
    } while(0)

/* ============================================================================
 * 编译期日志级别过滤
 * ============================================================================
 * 定义 XLOG_MIN_LEVEL 可以在编译期完全移除低级别日志
 * 例如：-DXLOG_MIN_LEVEL=LOG_LEVEL_INFO 会移除 TRACE 和 DEBUG
 */
#ifdef XLOG_MIN_LEVEL
#if XLOG_MIN_LEVEL > LOG_LEVEL_TRACE
#undef XLOG_TRACE
#define XLOG_TRACE(fmt, ...) ((void)0)
#endif
#if XLOG_MIN_LEVEL > LOG_LEVEL_DEBUG
#undef XLOG_DEBUG
#define XLOG_DEBUG(fmt, ...) ((void)0)
#endif
#if XLOG_MIN_LEVEL > LOG_LEVEL_INFO
#undef XLOG_INFO
#define XLOG_INFO(fmt, ...) ((void)0)
#endif
#if XLOG_MIN_LEVEL > LOG_LEVEL_WARNING
#undef XLOG_WARN
#undef XLOG_WARNING
#define XLOG_WARN(fmt, ...) ((void)0)
#define XLOG_WARNING(fmt, ...) ((void)0)
#endif
#if XLOG_MIN_LEVEL > LOG_LEVEL_ERROR
#undef XLOG_ERROR
#define XLOG_ERROR(fmt, ...) ((void)0)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* XLOG_LOG_MACROS_H */

