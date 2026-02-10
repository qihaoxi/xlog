/* =====================================================================================
 *       Filename:  platform.h
 *    Description:  Cross-platform compatibility layer (Linux/macOS/Windows)
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
 *        Company:  Onecloud
 * =====================================================================================
 */

#ifndef XLOG_PLATFORM_H
#define XLOG_PLATFORM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Platform Detection
 * ============================================================================ */

#if defined(_WIN32) || defined(_WIN64)
#define XLOG_PLATFORM_WINDOWS 1
#define XLOG_PLATFORM_NAME "Windows"
#elif defined(__APPLE__) && defined(__MACH__)
#define XLOG_PLATFORM_MACOS 1
#define XLOG_PLATFORM_NAME "macOS"
#elif defined(__linux__)
#define XLOG_PLATFORM_LINUX 1
#define XLOG_PLATFORM_NAME "Linux"
#elif defined(__unix__)
#define XLOG_PLATFORM_UNIX 1
#define XLOG_PLATFORM_NAME "Unix"
#else
#define XLOG_PLATFORM_UNKNOWN 1
#define XLOG_PLATFORM_NAME "Unknown"
#endif

/* POSIX-like systems */
#if defined(XLOG_PLATFORM_LINUX) || defined(XLOG_PLATFORM_MACOS) || defined(XLOG_PLATFORM_UNIX)
#define XLOG_PLATFORM_POSIX 1
#endif

/* ============================================================================
 * Platform-specific Includes
 * ============================================================================ */

#ifdef XLOG_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <process.h>

/* Windows path separator */
#define XLOG_PATH_SEP '\\'
#define XLOG_PATH_SEP_STR "\\"

/* File mode for open */
#define XLOG_O_RDONLY _O_RDONLY
#define XLOG_O_WRONLY _O_WRONLY
#define XLOG_O_CREAT  _O_CREAT
#define XLOG_O_APPEND _O_APPEND
#define XLOG_O_TRUNC  _O_TRUNC

#else /* POSIX */

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <errno.h>

/* POSIX path separator */
#define XLOG_PATH_SEP '/'
#define XLOG_PATH_SEP_STR "/"

#define XLOG_O_RDONLY O_RDONLY
#define XLOG_O_WRONLY O_WRONLY
#define XLOG_O_CREAT  O_CREAT
#define XLOG_O_APPEND O_APPEND
#define XLOG_O_TRUNC  O_TRUNC
#endif

/* ============================================================================
 * Cache Line Size
 * ============================================================================ */

#ifdef XLOG_PLATFORM_MACOS
/* Apple Silicon (M1/M2) uses 128-byte cache lines */
#define XLOG_CACHE_LINE_SIZE 128
#else
/* Most x86/x64 processors use 64-byte cache lines */
#define XLOG_CACHE_LINE_SIZE 64
#endif

/* ============================================================================
 * Compiler-specific Attributes
 * ============================================================================ */

#if defined(__GNUC__) || defined(__clang__)
#define XLOG_LIKELY(x)      __builtin_expect(!!(x), 1)
#define XLOG_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#define XLOG_ALIGNED(n)     __attribute__((aligned(n)))
#define XLOG_UNUSED         __attribute__((unused))
#define XLOG_ALWAYS_INLINE  __attribute__((always_inline)) inline
#define XLOG_NOINLINE       __attribute__((noinline))
#define XLOG_THREAD_LOCAL   __thread
#elif defined(_MSC_VER)
#define XLOG_LIKELY(x)      (x)
#define XLOG_UNLIKELY(x)    (x)
#define XLOG_ALIGNED(n)     __declspec(align(n))
#define XLOG_UNUSED
#define XLOG_ALWAYS_INLINE  __forceinline
#define XLOG_NOINLINE       __declspec(noinline)
#define XLOG_THREAD_LOCAL   __declspec(thread)
#else
#define XLOG_LIKELY(x)      (x)
#define XLOG_UNLIKELY(x)    (x)
#define XLOG_ALIGNED(n)
#define XLOG_UNUSED
#define XLOG_ALWAYS_INLINE  inline
#define XLOG_NOINLINE
#define XLOG_THREAD_LOCAL
#endif

/* CPU pause instruction for spin loops */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#ifdef _MSC_VER
#include <intrin.h>
#define XLOG_CPU_PAUSE() _mm_pause()
#else
#define XLOG_CPU_PAUSE() __asm__ volatile("pause" ::: "memory")
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#ifdef _MSC_VER
#define XLOG_CPU_PAUSE() __yield()
#else
#define XLOG_CPU_PAUSE() __asm__ volatile("yield" ::: "memory")
#endif
#else
#define XLOG_CPU_PAUSE() ((void)0)
#endif

/* Memory barrier */
#if defined(__GNUC__) || defined(__clang__)
#define XLOG_MEMORY_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#elif defined(_MSC_VER)
#define XLOG_MEMORY_BARRIER() MemoryBarrier()
#else
#define XLOG_MEMORY_BARRIER() ((void)0)
#endif

/* ============================================================================
 * Thread ID
 * ============================================================================ */

#ifdef XLOG_PLATFORM_WINDOWS
typedef DWORD xlog_tid_t;
static inline xlog_tid_t xlog_get_tid(void)
{
	return GetCurrentThreadId();
}
#elif defined(XLOG_PLATFORM_LINUX)

#include <sys/syscall.h>

typedef pid_t xlog_tid_t;

static inline xlog_tid_t xlog_get_tid(void)
{
	return (pid_t) syscall(SYS_gettid);
}

#elif defined(XLOG_PLATFORM_MACOS)
#include <pthread.h>
typedef uint64_t xlog_tid_t;
static inline xlog_tid_t xlog_get_tid(void)
{
	uint64_t tid;
	pthread_threadid_np(NULL, &tid);
	return tid;
}
#else
typedef unsigned long xlog_tid_t;
static inline xlog_tid_t xlog_get_tid(void)
{
	return (unsigned long)pthread_self();
}
#endif

/* ============================================================================
 * High-resolution Timestamp
 * ============================================================================ */

#ifdef XLOG_PLATFORM_WINDOWS
static inline uint64_t xlog_get_timestamp_ns(void)
{
	static LARGE_INTEGER freq = {0};
	if (freq.QuadPart == 0)
	{
		QueryPerformanceFrequency(&freq);
	}
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (uint64_t)(now.QuadPart * 1000000000ULL / freq.QuadPart);
}

static inline void xlog_get_localtime(time_t t, struct tm *tm_out)
{
	localtime_s(tm_out, &t);
}
#else

static inline uint64_t xlog_get_timestamp_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

static inline void xlog_get_localtime(time_t t, struct tm *tm_out)
{
	localtime_r(&t, tm_out);
}

#endif

/* ============================================================================
 * File System Operations
 * ============================================================================ */

/* Get file size */
int64_t xlog_file_size(const char *path);

/* Check if file exists */
bool xlog_file_exists(const char *path);

/* Check if path is a directory */
bool xlog_is_directory(const char *path);

/* Create directory (including parents) */
bool xlog_mkdir_p(const char *path);

/* Rename file */
bool xlog_rename(const char *old_path, const char *new_path);

/* Remove file */
bool xlog_remove(const char *path);

/* Get directory free space in bytes */
int64_t xlog_dir_free_space(const char *path);

/* Get directory total used space (by xlog files matching pattern) */
int64_t xlog_dir_used_space(const char *dir_path, const char *pattern);

/* List files in directory matching pattern */
typedef void (*xlog_dir_callback)(const char *filename, void *user_data);

int xlog_list_files(const char *dir_path, const char *pattern,
                    xlog_dir_callback callback, void *user_data);

/* ============================================================================
 * String Operations (safe versions)
 * ============================================================================ */

#ifdef XLOG_PLATFORM_WINDOWS
#define xlog_snprintf(buf, size, fmt, ...) \
		_snprintf_s(buf, size, _TRUNCATE, fmt, ##__VA_ARGS__)
#define xlog_strncpy(dst, src, size) strncpy_s(dst, size, src, _TRUNCATE)
#define xlog_strerror(err, buf, size) strerror_s(buf, size, err)
#else
#define xlog_snprintf snprintf
#define xlog_strncpy(dst, src, size) do \
{ \
        strncpy(dst, src, (size) - 1); \
        (dst)[(size) - 1] = '\0'; \
    } while(0)
#define xlog_strerror(err, buf, size) strerror_r(err, buf, size)
#endif

/* ============================================================================
 * Sleep
 * ============================================================================ */

#ifdef XLOG_PLATFORM_WINDOWS
static inline void xlog_sleep_us(unsigned int us)
{
	Sleep(us / 1000);  /* Windows Sleep is in milliseconds */
}
static inline void xlog_sleep_ms(unsigned int ms)
{
	Sleep(ms);
}
#else

static inline void xlog_sleep_us(unsigned int us)
{
	usleep(us);
}

static inline void xlog_sleep_ms(unsigned int ms)
{
	usleep(ms * 1000);
}

#endif

/* ============================================================================
 * Atomic Operations (C11 stdatomic compatible)
 * ============================================================================ */

/* Most compilers support C11 stdatomic.h now, but MSVC needs special handling */
#ifdef _MSC_VER
/* MSVC doesn't fully support C11 stdatomic until recent versions */
#if _MSC_VER >= 1928  /* Visual Studio 2019 16.8+ */
#include <stdatomic.h>
#else
	/* Fallback for older MSVC */
#include <windows.h>
	typedef volatile LONG atomic_int;
	typedef volatile LONGLONG atomic_llong;
	typedef volatile LONG atomic_bool;
	typedef volatile size_t atomic_size_t;

#define atomic_load(ptr) (*(ptr))
#define atomic_store(ptr, val) (*(ptr) = (val))
#define atomic_fetch_add(ptr, val) InterlockedExchangeAdd((ptr), (val))
#define ATOMIC_BOOL_LOCK_FREE 2
#endif
#else

#include <stdatomic.h>

#endif

/* ============================================================================
 * Thread Support
 * ============================================================================ */

#ifdef XLOG_PLATFORM_WINDOWS
typedef HANDLE xlog_thread_t;
typedef CRITICAL_SECTION xlog_mutex_t;
typedef CONDITION_VARIABLE xlog_cond_t;

int xlog_thread_create(xlog_thread_t *thread, void *(*func)(void *), void *arg);
int xlog_thread_join(xlog_thread_t thread, void **retval);
int xlog_mutex_init(xlog_mutex_t *mutex);
int xlog_mutex_destroy(xlog_mutex_t *mutex);
int xlog_mutex_lock(xlog_mutex_t *mutex);
int xlog_mutex_unlock(xlog_mutex_t *mutex);
int xlog_cond_init(xlog_cond_t *cond);
int xlog_cond_destroy(xlog_cond_t *cond);
int xlog_cond_wait(xlog_cond_t *cond, xlog_mutex_t *mutex);
int xlog_cond_signal(xlog_cond_t *cond);
int xlog_cond_broadcast(xlog_cond_t *cond);
#else
typedef pthread_t xlog_thread_t;
typedef pthread_mutex_t xlog_mutex_t;
typedef pthread_cond_t xlog_cond_t;

#define xlog_thread_create(t, f, a) pthread_create(t, NULL, f, a)
#define xlog_thread_join(t, r) pthread_join(t, r)
#define xlog_mutex_init(m) pthread_mutex_init(m, NULL)
#define xlog_mutex_destroy(m) pthread_mutex_destroy(m)
#define xlog_mutex_lock(m) pthread_mutex_lock(m)
#define xlog_mutex_unlock(m) pthread_mutex_unlock(m)
#define xlog_cond_init(c) pthread_cond_init(c, NULL)
#define xlog_cond_destroy(c) pthread_cond_destroy(c)
#define xlog_cond_wait(c, m) pthread_cond_wait(c, m)
#define xlog_cond_signal(c) pthread_cond_signal(c)
#define xlog_cond_broadcast(c) pthread_cond_broadcast(c)
#endif

/* ============================================================================
 * TTY Detection
 * ============================================================================ */

#ifdef XLOG_PLATFORM_WINDOWS
static inline bool xlog_is_tty(int fd)
{
	return _isatty(fd) != 0;
}
#else

static inline bool xlog_is_tty(int fd)
{
	return isatty(fd) != 0;
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* XLOG_PLATFORM_H */

