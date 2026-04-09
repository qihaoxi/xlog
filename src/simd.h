/* =====================================================================================
 *       Filename:  simd.h
 *    Description:  Cross-platform SIMD operations for high-performance formatting
 *                  Supports: x86/x64 (SSE2/SSE4.2/AVX2), ARM64 (NEON), Fallback (scalar)
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#ifndef XLOG_SIMD_H
#define XLOG_SIMD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SIMD Feature Detection
 * ============================================================================ */

/* Architecture detection */
#if defined(__x86_64__) || defined(_M_X64)
#define XLOG_ARCH_X64 1
#elif defined(__i386__) || defined(_M_IX86)
#define XLOG_ARCH_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define XLOG_ARCH_ARM64 1
#elif defined(__arm__) || defined(_M_ARM)
#define XLOG_ARCH_ARM32 1
#endif

/* SIMD feature flags */
#if defined(XLOG_ARCH_X64) || defined(XLOG_ARCH_X86)
/* x86/x64: Check for SSE2, SSE4.2, AVX2 */
#ifdef XLOG_COMPILER_MSVC
#include <intrin.h>
#else

#include <cpuid.h>

#endif

#include <emmintrin.h>  /* SSE2 */

#ifdef __SSE4_2__

#include <nmmintrin.h>  /* SSE4.2 */

#define XLOG_HAS_SSE42 1
#endif

#ifdef __AVX2__

#include <immintrin.h>  /* AVX2 */

#define XLOG_HAS_AVX2 1
#endif

#define XLOG_HAS_SSE2 1
#define XLOG_SIMD_WIDTH 16  /* SSE2 operates on 128-bit (16 bytes) */

#elif defined(XLOG_ARCH_ARM64)
/* ARM64: NEON is always available */
#include <arm_neon.h>
#define XLOG_HAS_NEON 1
#define XLOG_SIMD_WIDTH 16  /* NEON operates on 128-bit (16 bytes) */

#elif defined(XLOG_ARCH_ARM32)
/* ARM32: Check for NEON */
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define XLOG_HAS_NEON 1
#define XLOG_SIMD_WIDTH 16
#else
#define XLOG_SIMD_WIDTH 1  /* Scalar fallback */
#endif

#else
/* Unknown architecture: scalar fallback */
#define XLOG_SIMD_WIDTH 1
#endif

/* ============================================================================
 * Runtime CPU Feature Detection
 * ============================================================================ */

typedef struct xlog_cpu_features
{
	bool sse2;
	bool sse42;
	bool avx2;
	bool neon;
} xlog_cpu_features;

/**
 * Detect CPU features at runtime.
 * Call once at initialization.
 */
void xlog_detect_cpu_features(xlog_cpu_features *features);

/**
 * Get globally detected features (initialized once).
 */
const xlog_cpu_features *xlog_get_cpu_features(void);

/* ============================================================================
 * SIMD String Operations
 * ============================================================================
 *
 * Note on Linux/macOS: These functions delegate to glibc/libSystem since
 * modern libc implementations (glibc 2.17+, macOS 10.9+) use highly optimized
 * AVX2/AVX-512 assembly that outperforms hand-written SIMD code.
 *
 * Custom SIMD implementations are used on:
 * - Windows (MSVC CRT is less optimized)
 * - Embedded systems without optimized libc
 * - Older platforms
 */

/**
 * Fast memory copy using SIMD.
 * Falls back to memcpy for small sizes or unsupported architectures.
 *
 * @param dst   Destination buffer
 * @param src   Source buffer
 * @param len   Number of bytes to copy
 */
void xlog_simd_memcpy(void *dst, const void *src, size_t len);

/**
 * Fast memory set using SIMD.
 *
 * @param dst   Destination buffer
 * @param val   Value to set (byte)
 * @param len   Number of bytes to set
 */
void xlog_simd_memset(void *dst, int val, size_t len);

/**
 * Fast string length using SIMD.
 * Uses SSE4.2 pcmpistri or NEON for acceleration.
 *
 * @param str   Null-terminated string
 * @return      Length of the string (not including null terminator)
 */
size_t xlog_simd_strlen(const char *str);

/**
 * Find character in string using SIMD.
 *
 * @param str   String to search
 * @param c     Character to find
 * @param len   Maximum length to search
 * @return      Pointer to first occurrence, or NULL if not found
 */
const char *xlog_simd_memchr(const char *str, int c, size_t len);

/**
 * Find substring in string using SIMD.
 *
 * @param haystack  String to search in
 * @param needle    Substring to find
 * @return          Pointer to first occurrence, or NULL if not found
 */
const char *xlog_simd_strstr(const char *haystack, const char *needle);

/* ============================================================================
 * SIMD Integer to String Conversion
 * ============================================================================ */

/**
 * Convert uint64 to decimal string using SIMD.
 * Much faster than sprintf for numbers.
 *
 * @param value     Value to convert
 * @param buffer    Output buffer (must be at least 21 bytes)
 * @return          Number of characters written (not including null terminator)
 */
int xlog_simd_u64toa(uint64_t value, char *buffer);

/**
 * Convert int64 to decimal string using SIMD.
 *
 * @param value     Value to convert
 * @param buffer    Output buffer (must be at least 22 bytes)
 * @return          Number of characters written (not including null terminator)
 */
int xlog_simd_i64toa(int64_t value, char *buffer);

/**
 * Convert uint32 to decimal string using SIMD.
 *
 * @param value     Value to convert
 * @param buffer    Output buffer (must be at least 11 bytes)
 * @return          Number of characters written (not including null terminator)
 */
int xlog_simd_u32toa(uint32_t value, char *buffer);

/**
 * Convert uint64 to hexadecimal string using SIMD.
 *
 * @param value     Value to convert
 * @param buffer    Output buffer (must be at least 17 bytes)
 * @param uppercase Use uppercase letters (A-F) if true
 * @return          Number of characters written (not including null terminator)
 */
int xlog_simd_u64tohex(uint64_t value, char *buffer, bool uppercase);

/* ============================================================================
 * SIMD Format Specifier Parsing
 * ============================================================================ */

/**
 * Count format specifiers (%...) in format string using SIMD.
 *
 * @param fmt   Format string
 * @return      Number of format specifiers
 */
int xlog_simd_count_specifiers(const char *fmt);

/**
 * Find next '%' character in format string using SIMD.
 *
 * @param fmt   Format string
 * @param len   Length of format string
 * @return      Offset to next '%', or -1 if not found
 */
int xlog_simd_find_percent(const char *fmt, size_t len);

/* ============================================================================
 * SIMD Digit Tables (for fast conversion)
 * ============================================================================ */

/* Precomputed 2-digit table for 00-99 */
extern const char XLOG_DIGIT_TABLE[200];

/* Hex digit tables */
extern const char XLOG_HEX_TABLE_LOWER[512];
extern const char XLOG_HEX_TABLE_UPPER[512];

/* ============================================================================
 * Inline SIMD Helpers
 * ============================================================================ */

/* Fast modulo for powers of 2 */
#define XLOG_FAST_MOD(x, n) ((x) & ((n) - 1))

/* Prefetch for read */
#if defined(XLOG_COMPILER_GCC) || defined(XLOG_COMPILER_CLANG)
#define XLOG_PREFETCH_READ(addr) __builtin_prefetch((addr), 0, 3)
#define XLOG_PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
#elif defined(XLOG_COMPILER_MSVC)
#include <intrin.h>
#define XLOG_PREFETCH_READ(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#define XLOG_PREFETCH_WRITE(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#else
#define XLOG_PREFETCH_READ(addr) ((void)0)
#define XLOG_PREFETCH_WRITE(addr) ((void)0)
#endif

/* ============================================================================
 * SIMD-optimized Timestamp Formatting
 * ============================================================================ */

/**
 * Format timestamp components into buffer using SIMD.
 * Format: "YYYY-MM-DD HH:MM:SS"
 *
 * @param year      Year (1900-9999)
 * @param month     Month (1-12)
 * @param day       Day (1-31)
 * @param hour      Hour (0-23)
 * @param minute    Minute (0-59)
 * @param second    Second (0-59)
 * @param buffer    Output buffer (must be at least 20 bytes)
 * @return          Number of characters written (always 19)
 */
int xlog_simd_format_datetime(int year, int month, int day,
                              int hour, int minute, int second,
                              char *buffer);

/**
 * Format microseconds suffix ".NNNNNN" using SIMD.
 *
 * @param usec      Microseconds (0-999999)
 * @param buffer    Output buffer (must be at least 8 bytes)
 * @return          Number of characters written (always 7)
 */
int xlog_simd_format_usec(uint32_t usec, char *buffer);

#ifdef __cplusplus
}
#endif

#endif /* XLOG_SIMD_H */

