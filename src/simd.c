/* =====================================================================================
 *       Filename:  simd.c
 *    Description:  Cross-platform SIMD operations implementation
 *                  Supports: x86/x64 (SSE2/SSE4.2/AVX2), ARM64 (NEON), Fallback (scalar)
 *        Version:  1.0
 *        Created:  2026-02-09
 *       Compiler:  gcc/clang/msvc (C11)
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 */

#include "simd.h"
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Digit Tables for Fast Conversion
 * ============================================================================ */

/* Precomputed 2-digit table: "00", "01", "02", ..., "99" */
const char XLOG_DIGIT_TABLE[200] = {
		'0', '0', '0', '1', '0', '2', '0', '3', '0', '4', '0', '5', '0', '6', '0', '7', '0', '8', '0', '9',
		'1', '0', '1', '1', '1', '2', '1', '3', '1', '4', '1', '5', '1', '6', '1', '7', '1', '8', '1', '9',
		'2', '0', '2', '1', '2', '2', '2', '3', '2', '4', '2', '5', '2', '6', '2', '7', '2', '8', '2', '9',
		'3', '0', '3', '1', '3', '2', '3', '3', '3', '4', '3', '5', '3', '6', '3', '7', '3', '8', '3', '9',
		'4', '0', '4', '1', '4', '2', '4', '3', '4', '4', '4', '5', '4', '6', '4', '7', '4', '8', '4', '9',
		'5', '0', '5', '1', '5', '2', '5', '3', '5', '4', '5', '5', '5', '6', '5', '7', '5', '8', '5', '9',
		'6', '0', '6', '1', '6', '2', '6', '3', '6', '4', '6', '5', '6', '6', '6', '7', '6', '8', '6', '9',
		'7', '0', '7', '1', '7', '2', '7', '3', '7', '4', '7', '5', '7', '6', '7', '7', '7', '8', '7', '9',
		'8', '0', '8', '1', '8', '2', '8', '3', '8', '4', '8', '5', '8', '6', '8', '7', '8', '8', '8', '9',
		'9', '0', '9', '1', '9', '2', '9', '3', '9', '4', '9', '5', '9', '6', '9', '7', '9', '8', '9', '9'
};

/* Hex digit tables for byte-to-hex conversion */
const char XLOG_HEX_TABLE_LOWER[512] = {
		'0', '0', '0', '1', '0', '2', '0', '3', '0', '4', '0', '5', '0', '6', '0', '7',
		'0', '8', '0', '9', '0', 'a', '0', 'b', '0', 'c', '0', 'd', '0', 'e', '0', 'f',
		'1', '0', '1', '1', '1', '2', '1', '3', '1', '4', '1', '5', '1', '6', '1', '7',
		'1', '8', '1', '9', '1', 'a', '1', 'b', '1', 'c', '1', 'd', '1', 'e', '1', 'f',
		'2', '0', '2', '1', '2', '2', '2', '3', '2', '4', '2', '5', '2', '6', '2', '7',
		'2', '8', '2', '9', '2', 'a', '2', 'b', '2', 'c', '2', 'd', '2', 'e', '2', 'f',
		'3', '0', '3', '1', '3', '2', '3', '3', '3', '4', '3', '5', '3', '6', '3', '7',
		'3', '8', '3', '9', '3', 'a', '3', 'b', '3', 'c', '3', 'd', '3', 'e', '3', 'f',
		'4', '0', '4', '1', '4', '2', '4', '3', '4', '4', '4', '5', '4', '6', '4', '7',
		'4', '8', '4', '9', '4', 'a', '4', 'b', '4', 'c', '4', 'd', '4', 'e', '4', 'f',
		'5', '0', '5', '1', '5', '2', '5', '3', '5', '4', '5', '5', '5', '6', '5', '7',
		'5', '8', '5', '9', '5', 'a', '5', 'b', '5', 'c', '5', 'd', '5', 'e', '5', 'f',
		'6', '0', '6', '1', '6', '2', '6', '3', '6', '4', '6', '5', '6', '6', '6', '7',
		'6', '8', '6', '9', '6', 'a', '6', 'b', '6', 'c', '6', 'd', '6', 'e', '6', 'f',
		'7', '0', '7', '1', '7', '2', '7', '3', '7', '4', '7', '5', '7', '6', '7', '7',
		'7', '8', '7', '9', '7', 'a', '7', 'b', '7', 'c', '7', 'd', '7', 'e', '7', 'f',
		'8', '0', '8', '1', '8', '2', '8', '3', '8', '4', '8', '5', '8', '6', '8', '7',
		'8', '8', '8', '9', '8', 'a', '8', 'b', '8', 'c', '8', 'd', '8', 'e', '8', 'f',
		'9', '0', '9', '1', '9', '2', '9', '3', '9', '4', '9', '5', '9', '6', '9', '7',
		'9', '8', '9', '9', '9', 'a', '9', 'b', '9', 'c', '9', 'd', '9', 'e', '9', 'f',
		'a', '0', 'a', '1', 'a', '2', 'a', '3', 'a', '4', 'a', '5', 'a', '6', 'a', '7',
		'a', '8', 'a', '9', 'a', 'a', 'a', 'b', 'a', 'c', 'a', 'd', 'a', 'e', 'a', 'f',
		'b', '0', 'b', '1', 'b', '2', 'b', '3', 'b', '4', 'b', '5', 'b', '6', 'b', '7',
		'b', '8', 'b', '9', 'b', 'a', 'b', 'b', 'b', 'c', 'b', 'd', 'b', 'e', 'b', 'f',
		'c', '0', 'c', '1', 'c', '2', 'c', '3', 'c', '4', 'c', '5', 'c', '6', 'c', '7',
		'c', '8', 'c', '9', 'c', 'a', 'c', 'b', 'c', 'c', 'c', 'd', 'c', 'e', 'c', 'f',
		'd', '0', 'd', '1', 'd', '2', 'd', '3', 'd', '4', 'd', '5', 'd', '6', 'd', '7',
		'd', '8', 'd', '9', 'd', 'a', 'd', 'b', 'd', 'c', 'd', 'd', 'd', 'e', 'd', 'f',
		'e', '0', 'e', '1', 'e', '2', 'e', '3', 'e', '4', 'e', '5', 'e', '6', 'e', '7',
		'e', '8', 'e', '9', 'e', 'a', 'e', 'b', 'e', 'c', 'e', 'd', 'e', 'e', 'e', 'f',
		'f', '0', 'f', '1', 'f', '2', 'f', '3', 'f', '4', 'f', '5', 'f', '6', 'f', '7',
		'f', '8', 'f', '9', 'f', 'a', 'f', 'b', 'f', 'c', 'f', 'd', 'f', 'e', 'f', 'f'
};

const char XLOG_HEX_TABLE_UPPER[512] = {
		'0', '0', '0', '1', '0', '2', '0', '3', '0', '4', '0', '5', '0', '6', '0', '7',
		'0', '8', '0', '9', '0', 'A', '0', 'B', '0', 'C', '0', 'D', '0', 'E', '0', 'F',
		'1', '0', '1', '1', '1', '2', '1', '3', '1', '4', '1', '5', '1', '6', '1', '7',
		'1', '8', '1', '9', '1', 'A', '1', 'B', '1', 'C', '1', 'D', '1', 'E', '1', 'F',
		'2', '0', '2', '1', '2', '2', '2', '3', '2', '4', '2', '5', '2', '6', '2', '7',
		'2', '8', '2', '9', '2', 'A', '2', 'B', '2', 'C', '2', 'D', '2', 'E', '2', 'F',
		'3', '0', '3', '1', '3', '2', '3', '3', '3', '4', '3', '5', '3', '6', '3', '7',
		'3', '8', '3', '9', '3', 'A', '3', 'B', '3', 'C', '3', 'D', '3', 'E', '3', 'F',
		'4', '0', '4', '1', '4', '2', '4', '3', '4', '4', '4', '5', '4', '6', '4', '7',
		'4', '8', '4', '9', '4', 'A', '4', 'B', '4', 'C', '4', 'D', '4', 'E', '4', 'F',
		'5', '0', '5', '1', '5', '2', '5', '3', '5', '4', '5', '5', '5', '6', '5', '7',
		'5', '8', '5', '9', '5', 'A', '5', 'B', '5', 'C', '5', 'D', '5', 'E', '5', 'F',
		'6', '0', '6', '1', '6', '2', '6', '3', '6', '4', '6', '5', '6', '6', '6', '7',
		'6', '8', '6', '9', '6', 'A', '6', 'B', '6', 'C', '6', 'D', '6', 'E', '6', 'F',
		'7', '0', '7', '1', '7', '2', '7', '3', '7', '4', '7', '5', '7', '6', '7', '7',
		'7', '8', '7', '9', '7', 'A', '7', 'B', '7', 'C', '7', 'D', '7', 'E', '7', 'F',
		'8', '0', '8', '1', '8', '2', '8', '3', '8', '4', '8', '5', '8', '6', '8', '7',
		'8', '8', '8', '9', '8', 'A', '8', 'B', '8', 'C', '8', 'D', '8', 'E', '8', 'F',
		'9', '0', '9', '1', '9', '2', '9', '3', '9', '4', '9', '5', '9', '6', '9', '7',
		'9', '8', '9', '9', '9', 'A', '9', 'B', '9', 'C', '9', 'D', '9', 'E', '9', 'F',
		'A', '0', 'A', '1', 'A', '2', 'A', '3', 'A', '4', 'A', '5', 'A', '6', 'A', '7',
		'A', '8', 'A', '9', 'A', 'A', 'A', 'B', 'A', 'C', 'A', 'D', 'A', 'E', 'A', 'F',
		'B', '0', 'B', '1', 'B', '2', 'B', '3', 'B', '4', 'B', '5', 'B', '6', 'B', '7',
		'B', '8', 'B', '9', 'B', 'A', 'B', 'B', 'B', 'C', 'B', 'D', 'B', 'E', 'B', 'F',
		'C', '0', 'C', '1', 'C', '2', 'C', '3', 'C', '4', 'C', '5', 'C', '6', 'C', '7',
		'C', '8', 'C', '9', 'C', 'A', 'C', 'B', 'C', 'C', 'C', 'D', 'C', 'E', 'C', 'F',
		'D', '0', 'D', '1', 'D', '2', 'D', '3', 'D', '4', 'D', '5', 'D', '6', 'D', '7',
		'D', '8', 'D', '9', 'D', 'A', 'D', 'B', 'D', 'C', 'D', 'D', 'D', 'E', 'D', 'F',
		'E', '0', 'E', '1', 'E', '2', 'E', '3', 'E', '4', 'E', '5', 'E', '6', 'E', '7',
		'E', '8', 'E', '9', 'E', 'A', 'E', 'B', 'E', 'C', 'E', 'D', 'E', 'E', 'E', 'F',
		'F', '0', 'F', '1', 'F', '2', 'F', '3', 'F', '4', 'F', '5', 'F', '6', 'F', '7',
		'F', '8', 'F', '9', 'F', 'A', 'F', 'B', 'F', 'C', 'F', 'D', 'F', 'E', 'F', 'F'
};

/* ============================================================================
 * CPU Feature Detection
 * ============================================================================ */

static xlog_cpu_features g_cpu_features = {0};
static atomic_int g_features_detected = 0;

#if defined(XLOG_ARCH_X64) || defined(XLOG_ARCH_X86)

static void cpuid(int info[4], int func_id)
{
#ifdef XLOG_COMPILER_MSVC
	__cpuid(info, func_id);
#else
	__cpuid(func_id, info[0], info[1], info[2], info[3]);
#endif
}

static void cpuid_ex(int info[4], int func_id, int sub_func)
{
#ifdef XLOG_COMPILER_MSVC
	__cpuidex(info, func_id, sub_func);
#else
	__cpuid_count(func_id, sub_func, info[0], info[1], info[2], info[3]);
#endif
}

#endif /* x86/x64 */

void xlog_detect_cpu_features(xlog_cpu_features *features)
{
	if (!features)
	{
		return;
	}
	memset(features, 0, sizeof(*features));

#if defined(XLOG_ARCH_X64) || defined(XLOG_ARCH_X86)
	int info[4];

	/* Check basic CPUID */
	cpuid(info, 0);
	int max_func = info[0];

	if (max_func >= 1)
	{
		cpuid(info, 1);
		/* SSE2: EDX bit 26 */
		features->sse2 = (info[3] & (1 << 26)) != 0;
		/* SSE4.2: ECX bit 20 */
		features->sse42 = (info[2] & (1 << 20)) != 0;
	}

	if (max_func >= 7)
	{
		cpuid_ex(info, 7, 0);
		/* AVX2: EBX bit 5 */
		features->avx2 = (info[1] & (1 << 5)) != 0;
	}

#elif defined(XLOG_ARCH_ARM64)
	/* ARM64 always has NEON */
	features->neon = true;

#elif defined(XLOG_ARCH_ARM32) && defined(XLOG_HAS_NEON)
	/* ARM32 with NEON compiled in */
	features->neon = true;
#endif

	g_cpu_features = *features;
	atomic_store(&g_features_detected, 1);
}

const xlog_cpu_features *xlog_get_cpu_features(void)
{
	if (!atomic_load(&g_features_detected))
	{
		xlog_detect_cpu_features(&g_cpu_features);
	}
	return &g_cpu_features;
}

/* ============================================================================
 * SIMD Memory Operations
 * ============================================================================ */

#if defined(XLOG_HAS_SSE2)

static void simd_memcpy_sse2(void *dst, const void *src, size_t len)
{
	char *d = (char *) dst;
	const char *s = (const char *) src;

	/* Handle unaligned prefix */
	while (len >= 16 && ((uintptr_t) d & 15))
	{
		*d++ = *s++;
		len--;
	}

	/* Main SIMD loop */
	while (len >= 64)
	{
		__m128i v0 = _mm_loadu_si128((const void *) (s));
		__m128i v1 = _mm_loadu_si128((const void *) (s + 16));
		__m128i v2 = _mm_loadu_si128((const void *) (s + 32));
		__m128i v3 = _mm_loadu_si128((const void *) (s + 48));

		/* d is 16-byte aligned here */
		_mm_store_si128((void *) (d), v0);
		_mm_store_si128((void *) (d + 16), v1);
		_mm_store_si128((void *) (d + 32), v2);
		_mm_store_si128((void *) (d + 48), v3);

		s += 64;
		d += 64;
		len -= 64;
	}

	while (len >= 16)
	{
		__m128i v = _mm_loadu_si128((const void *) s);
		_mm_store_si128((void *) d, v);
		s += 16;
		d += 16;
		len -= 16;
	}

	/* Handle tail */
	while (len--)
	{
		*d++ = *s++;
	}
}

#endif /* XLOG_HAS_SSE2 */

#if defined(XLOG_HAS_AVX2)

static void simd_memcpy_avx2(void *dst, const void *src, size_t len)
{
	char *d = (char *) dst;
	const char *s = (const char *) src;

	/* Handle unaligned prefix */
	while (len >= 32 && ((uintptr_t) d & 31))
	{
		*d++ = *s++;
		len--;
	}

	/* Main AVX2 loop */
	while (len >= 128)
	{
		__m256i v0 = _mm256_loadu_si256((const void *) (s));
		__m256i v1 = _mm256_loadu_si256((const void *) (s + 32));
		__m256i v2 = _mm256_loadu_si256((const void *) (s + 64));
		__m256i v3 = _mm256_loadu_si256((const void *) (s + 96));

		/* d is 32-byte aligned here */
		_mm256_store_si256((void *) (d), v0);
		_mm256_store_si256((void *) (d + 32), v1);
		_mm256_store_si256((void *) (d + 64), v2);
		_mm256_store_si256((void *) (d + 96), v3);

		s += 128;
		d += 128;
		len -= 128;
	}

	while (len >= 32)
	{
		__m256i v = _mm256_loadu_si256((const void *) s);
		_mm256_store_si256((void *) d, v);
		s += 32;
		d += 32;
		len -= 32;
	}

	/* Handle tail with SSE2 */
	while (len >= 16)
	{
		__m128i v = _mm_loadu_si128((const void *) s);
		_mm_storeu_si128((void *) d, v);
		s += 16;
		d += 16;
		len -= 16;
	}

	while (len--)
	{
		*d++ = *s++;
	}
}

#endif /* XLOG_HAS_AVX2 */

#if defined(XLOG_HAS_NEON)

static void simd_memcpy_neon(void *dst, const void *src, size_t len)
{
	char *d = (char *)dst;
	const char *s = (const char *)src;

	/* Main NEON loop */
	while (len >= 64)
	{
		uint8x16_t v0 = vld1q_u8((const uint8_t *)s);
		uint8x16_t v1 = vld1q_u8((const uint8_t *)(s + 16));
		uint8x16_t v2 = vld1q_u8((const uint8_t *)(s + 32));
		uint8x16_t v3 = vld1q_u8((const uint8_t *)(s + 48));

		vst1q_u8((uint8_t *)d, v0);
		vst1q_u8((uint8_t *)(d + 16), v1);
		vst1q_u8((uint8_t *)(d + 32), v2);
		vst1q_u8((uint8_t *)(d + 48), v3);

		s += 64;
		d += 64;
		len -= 64;
	}

	while (len >= 16)
	{
		uint8x16_t v = vld1q_u8((const uint8_t *)s);
		vst1q_u8((uint8_t *)d, v);
		s += 16;
		d += 16;
		len -= 16;
	}

	while (len--)
	{
		*d++ = *s++;
	}
}

#endif /* XLOG_HAS_NEON */

void xlog_simd_memcpy(void *dst, const void *src, size_t len)
{
	/*
	 * Note: Modern glibc (2.17+) uses highly optimized AVX2/AVX-512 assembly
	 * for memcpy that outperforms hand-written SIMD in most cases.
	 * We directly use the standard library implementation.
	 *
	 * Custom SIMD implementations are kept for:
	 * 1. Platforms without optimized libc (embedded, older systems)
	 * 2. Windows with MSVC (less optimized CRT)
	 * 3. Educational/reference purposes
	 */
#if defined(XLOG_PLATFORM_LINUX) || defined(XLOG_PLATFORM_MACOS)
	/* Use glibc/libSystem optimized implementation */
	memcpy(dst, src, len);
#elif defined(XLOG_PLATFORM_WINDOWS)
	/* Windows CRT is less optimized, use SIMD for large copies */
	if (len < 64)
	{
		memcpy(dst, src, len);
		return;
	}
#if defined(XLOG_HAS_AVX2)
	if (xlog_get_cpu_features()->avx2)
	{
		simd_memcpy_avx2(dst, src, len);
		return;
	}
#endif
#if defined(XLOG_HAS_SSE2)
	if (xlog_get_cpu_features()->sse2)
	{
		simd_memcpy_sse2(dst, src, len);
		return;
	}
#endif
	memcpy(dst, src, len);
#else
	/* Fallback for unknown platforms */
#if defined(XLOG_HAS_NEON)
	if (len >= 64)
	{
		simd_memcpy_neon(dst, src, len);
		return;
	}
#endif
	memcpy(dst, src, len);
#endif
}

/* ============================================================================
 * SIMD Memory Set
 * ============================================================================ */

#if defined(XLOG_HAS_SSE2)

static void simd_memset_sse2(void *dst, int val, size_t len)
{
	char *d = (char *) dst;
	unsigned char c = (unsigned char) val;

	/* Create a 128-bit vector filled with the value */
	__m128i v = _mm_set1_epi8((char) c);

	/* Handle unaligned prefix */
	while (len > 0 && ((uintptr_t) d & 15))
	{
		*d++ = c;
		len--;
	}

	/* Main SIMD loop */
	while (len >= 64)
	{
		/* d is 16-byte aligned here */
		_mm_store_si128((void *) (d), v);
		_mm_store_si128((void *) (d + 16), v);
		_mm_store_si128((void *) (d + 32), v);
		_mm_store_si128((void *) (d + 48), v);
		d += 64;
		len -= 64;
	}

	while (len >= 16)
	{
		_mm_store_si128((void *) d, v);
		d += 16;
		len -= 16;
	}

	while (len--)
	{
		*d++ = c;
	}
}

#endif /* XLOG_HAS_SSE2 */

#if defined(XLOG_HAS_NEON)

static void simd_memset_neon(void *dst, int val, size_t len)
{
	char *d = (char *)dst;
	unsigned char c = (unsigned char)val;

	uint8x16_t v = vdupq_n_u8(c);

	while (len >= 64)
	{
		vst1q_u8((uint8_t *)d, v);
		vst1q_u8((uint8_t *)(d + 16), v);
		vst1q_u8((uint8_t *)(d + 32), v);
		vst1q_u8((uint8_t *)(d + 48), v);
		d += 64;
		len -= 64;
	}

	while (len >= 16)
	{
		vst1q_u8((uint8_t *)d, v);
		d += 16;
		len -= 16;
	}

	while (len--)
	{
		*d++ = c;
	}
}

#endif /* XLOG_HAS_NEON */

void xlog_simd_memset(void *dst, int val, size_t len)
{
	/* Use glibc/libSystem optimized implementation on Linux/macOS */
#if defined(XLOG_PLATFORM_LINUX) || defined(XLOG_PLATFORM_MACOS)
	memset(dst, val, len);
#elif defined(XLOG_PLATFORM_WINDOWS)
	if (len < 64)
	{
		memset(dst, val, len);
		return;
	}
#if defined(XLOG_HAS_SSE2)
	if (xlog_get_cpu_features()->sse2)
	{
		simd_memset_sse2(dst, val, len);
		return;
	}
#endif
	memset(dst, val, len);
#else
#if defined(XLOG_HAS_NEON)
	if (len >= 64)
	{
		simd_memset_neon(dst, val, len);
		return;
	}
#endif
	memset(dst, val, len);
#endif
}

/* ============================================================================
 * SIMD String Length
 * ============================================================================ */

#if defined(XLOG_HAS_SSE42)

static size_t simd_strlen_sse42(const char *str)
{
	const char *s = str;

	/* Handle unaligned prefix */
	while ((uintptr_t) s & 15)
	{
		if (*s == '\0')
		{
			return s - str;
		}
		s++;
	}

	/* Use pcmpistri to find null terminator */
	__m128i zero = _mm_setzero_si128();

	for (;;)
	{
		/* s is 16-byte aligned here after the prefix loop */
		__m128i chunk = _mm_load_si128((const void *) s);
		int idx = _mm_cmpistri(chunk, zero, _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_EACH);
		if (idx != 16)
		{
			return (s - str) + idx;
		}
		s += 16;
	}
}

#endif /* XLOG_HAS_SSE42 */

#if defined(XLOG_HAS_SSE2) && !defined(XLOG_HAS_SSE42)

static size_t simd_strlen_sse2(const char *str)
{
	const char *s = str;

	/* Handle unaligned prefix */
	while ((uintptr_t)s & 15)
	{
		if (*s == '\0') return s - str;
		s++;
	}

	__m128i zero = _mm_setzero_si128();

	for (;;)
	{
		/* s is 16-byte aligned here after the prefix loop */
		__m128i chunk = _mm_load_si128((const void *)s);
		__m128i cmp = _mm_cmpeq_epi8(chunk, zero);
		int mask = _mm_movemask_epi8(cmp);
		if (mask != 0)
		{
			/* Find position of first null byte */
			int idx;
#ifdef XLOG_COMPILER_MSVC
			unsigned long pos;
			_BitScanForward(&pos, mask);
			idx = pos;
#else
			idx = __builtin_ctz(mask);
#endif
			return (s - str) + idx;
		}
		s += 16;
	}
}

#endif /* XLOG_HAS_SSE2 && !XLOG_HAS_SSE42 */

#if defined(XLOG_HAS_NEON)

static size_t simd_strlen_neon(const char *str)
{
	const char *s = str;

	/* Handle unaligned prefix */
	while ((uintptr_t)s & 15)
	{
		if (*s == '\0') return s - str;
		s++;
	}

	uint8x16_t zero = vdupq_n_u8(0);

	for (;;)
	{
		uint8x16_t chunk = vld1q_u8((const uint8_t *)s);
		uint8x16_t cmp = vceqq_u8(chunk, zero);

		/* Check if any byte is zero */
		uint64_t lo = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0);
		uint64_t hi = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);

		if (lo != 0)
		{
			/* Find position in lower 8 bytes */
			for (int i = 0; i < 8; i++)
			{
				if (s[i] == '\0') return (s - str) + i;
			}
		}
		if (hi != 0)
		{
			for (int i = 8; i < 16; i++)
			{
				if (s[i] == '\0')
					return (s - str) + i;
			}
		}
		s += 16;
	}
}

#endif /* XLOG_HAS_NEON */

size_t xlog_simd_strlen(const char *str)
{
	if (!str)
	{
		return 0;
	}

	/* Use glibc/libSystem optimized implementation on Linux/macOS */
#if defined(XLOG_PLATFORM_LINUX) || defined(XLOG_PLATFORM_MACOS)
	return strlen(str);
#else
	/* Custom SIMD for Windows and other platforms */
#if defined(XLOG_HAS_SSE42)
	if (xlog_get_cpu_features()->sse42)
	{
		return simd_strlen_sse42(str);
	}
#endif

#if defined(XLOG_HAS_SSE2) && !defined(XLOG_HAS_SSE42)
	if (xlog_get_cpu_features()->sse2)
	{
		return simd_strlen_sse2(str);
	}
#endif

#if defined(XLOG_HAS_NEON)
	return simd_strlen_neon(str);
#endif

	return strlen(str);
#endif
}

/* ============================================================================
 * SIMD Memory Search
 * ============================================================================ */

#if defined(XLOG_HAS_SSE2)

static const char *simd_memchr_sse2(const char *str, int c, size_t len)
{
	const char *s = str;
	unsigned char target = (unsigned char) c;

	/* Handle unaligned prefix */
	while (len > 0 && ((uintptr_t) s & 15))
	{
		if (*s == target)
		{
			return s;
		}
		s++;
		len--;
	}

	if (len < 16)
	{
		while (len--)
		{
			if (*s == target)
			{
				return s;
			}
			s++;
		}
		return NULL;
	}

	__m128i needle = _mm_set1_epi8((char) target);

	while (len >= 16)
	{
		/* s is 16-byte aligned here after the prefix loop */
		__m128i chunk = _mm_load_si128((const void *) s);
		__m128i cmp = _mm_cmpeq_epi8(chunk, needle);
		int mask = _mm_movemask_epi8(cmp);
		if (mask != 0)
		{
			int idx;
#ifdef XLOG_COMPILER_MSVC
			unsigned long pos;
			_BitScanForward(&pos, mask);
			idx = pos;
#else
			idx = __builtin_ctz(mask);
#endif
			return s + idx;
		}
		s += 16;
		len -= 16;
	}

	/* Handle tail */
	while (len--)
	{
		if (*s == target)
		{
			return s;
		}
		s++;
	}

	return NULL;
}

#endif /* XLOG_HAS_SSE2 */

#if defined(XLOG_HAS_NEON)

static const char *simd_memchr_neon(const char *str, int c, size_t len)
{
	const char *s = str;
	unsigned char target = (unsigned char)c;

	uint8x16_t needle = vdupq_n_u8(target);

	while (len >= 16)
	{
		uint8x16_t chunk = vld1q_u8((const uint8_t *)s);
		uint8x16_t cmp = vceqq_u8(chunk, needle);

		/* Check if any byte matches */
		uint64_t lo = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0);
		uint64_t hi = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);

		if (lo != 0)
		{
			for (int i = 0; i < 8; i++)
			{
				if (s[i] == target) return s + i;
			}
		}
		if (hi != 0)
		{
			for (int i = 8; i < 16; i++)
			{
				if (s[i] == target) return s + i;
			}
		}
		s += 16;
		len -= 16;
	}

	while (len--)
	{
		if (*s == target) return s;
		s++;
	}

	return NULL;
}

#endif /* XLOG_HAS_NEON */

const char *xlog_simd_memchr(const char *str, int c, size_t len)
{
	if (!str || len == 0)
	{
		return NULL;
	}

	/* Use glibc/libSystem optimized implementation on Linux/macOS */
#if defined(XLOG_PLATFORM_LINUX) || defined(XLOG_PLATFORM_MACOS)
	return memchr(str, c, len);
#else
#if defined(XLOG_HAS_SSE2)
	if (xlog_get_cpu_features()->sse2)
	{
		return simd_memchr_sse2(str, c, len);
	}
#endif

#if defined(XLOG_HAS_NEON)
	return simd_memchr_neon(str, c, len);
#endif

	return memchr(str, c, len);
#endif
}

/* ============================================================================
 * SIMD Integer to String Conversion
 * ============================================================================ */

/* Fast division by 100 using multiply-shift
 * Magic number from libdivide for full 32-bit range correctness */
static inline uint32_t div100(uint32_t n)
{
	return (uint32_t) (((uint64_t) n * 0x51EB851FUL) >> 37);
}

/* Fast division by 10000 */
static inline uint32_t div10000(uint32_t n)
{
	return (uint32_t) (((uint64_t) n * 0xD1B71759) >> 45);
}

int xlog_simd_u64toa(uint64_t value, char *buffer)
{
	char *p = buffer;
	char temp[24];
	char *t = temp + sizeof(temp);

	if (value == 0)
	{
		*p++ = '0';
		*p = '\0';
		return 1;
	}

	/* Convert from right to left using 2-digit table */
	while (value >= 100)
	{
		uint64_t q = value / 100;
		uint32_t r = (uint32_t) (value - q * 100);
		value = q;
		t -= 2;
		memcpy(t, &XLOG_DIGIT_TABLE[r * 2], 2);
	}

	/* Handle remaining 1-2 digits */
	if (value >= 10)
	{
		t -= 2;
		memcpy(t, &XLOG_DIGIT_TABLE[value * 2], 2);
	}
	else
	{
		*--t = '0' + (char) value;
	}

	/* Copy to output buffer */
	size_t len = (temp + sizeof(temp)) - t;
	memcpy(p, t, len);
	p[len] = '\0';

	return (int) len;
}

int xlog_simd_i64toa(int64_t value, char *buffer)
{
	if (value < 0)
	{
		*buffer++ = '-';
		return 1 + xlog_simd_u64toa((uint64_t) (-value), buffer);
	}
	return xlog_simd_u64toa((uint64_t) value, buffer);
}

int xlog_simd_u32toa(uint32_t value, char *buffer)
{
	char *p = buffer;
	char temp[12];
	char *t = temp + sizeof(temp);

	if (value == 0)
	{
		*p++ = '0';
		*p = '\0';
		return 1;
	}

	while (value >= 100)
	{
		uint32_t q = div100(value);
		uint32_t r = value - q * 100;
		value = q;
		t -= 2;
		memcpy(t, &XLOG_DIGIT_TABLE[r * 2], 2);
	}

	if (value >= 10)
	{
		t -= 2;
		memcpy(t, &XLOG_DIGIT_TABLE[value * 2], 2);
	}
	else
	{
		*--t = '0' + (char) value;
	}

	size_t len = (temp + sizeof(temp)) - t;
	memcpy(p, t, len);
	p[len] = '\0';

	return (int) len;
}

int xlog_simd_u64tohex(uint64_t value, char *buffer, bool uppercase)
{
	const char *table = uppercase ? XLOG_HEX_TABLE_UPPER : XLOG_HEX_TABLE_LOWER;
	char *p = buffer;

	if (value == 0)
	{
		*p++ = '0';
		*p = '\0';
		return 1;
	}

	/* Find first non-zero nibble */
	int shift = 60;
	while (shift >= 0 && ((value >> shift) & 0xF) == 0)
	{
		shift -= 4;
	}

	/* Convert nibbles */
	while (shift >= 0)
	{
		uint8_t nibble = (value >> shift) & 0xF;
		*p++ = table[nibble * 2 + 1];  /* Single hex digit */
		shift -= 4;
	}

	*p = '\0';
	return (int) (p - buffer);
}

/* ============================================================================
 * SIMD Format Specifier Operations
 * ============================================================================ */

#if defined(XLOG_HAS_SSE2)

static int simd_find_percent_sse2(const char *fmt, size_t len)
{
	const char *s = fmt;
	size_t remaining = len;

	__m128i percent = _mm_set1_epi8('%');

	/* Handle unaligned prefix */
	while (remaining > 0 && ((uintptr_t) s & 15))
	{
		if (*s == '%')
		{
			return (int) (s - fmt);
		}
		s++;
		remaining--;
	}

	while (remaining >= 16)
	{
		/* s is 16-byte aligned here after the prefix loop */
		__m128i chunk = _mm_load_si128((const void *) s);
		__m128i cmp = _mm_cmpeq_epi8(chunk, percent);
		int mask = _mm_movemask_epi8(cmp);
		if (mask != 0)
		{
			int idx;
#ifdef XLOG_COMPILER_MSVC
			unsigned long pos;
			_BitScanForward(&pos, mask);
			idx = pos;
#else
			idx = __builtin_ctz(mask);
#endif
			return (int) (s - fmt) + idx;
		}
		s += 16;
		remaining -= 16;
	}

	/* Handle tail */
	while (remaining--)
	{
		if (*s == '%')
		{
			return (int) (s - fmt);
		}
		s++;
	}

	return -1;
}

#endif /* XLOG_HAS_SSE2 */

int xlog_simd_find_percent(const char *fmt, size_t len)
{
	if (!fmt || len == 0)
	{
		return -1;
	}

#if defined(XLOG_HAS_SSE2)
	if (xlog_get_cpu_features()->sse2 && len >= 16)
	{
		return simd_find_percent_sse2(fmt, len);
	}
#endif

	/* Scalar fallback */
	for (size_t i = 0; i < len; i++)
	{
		if (fmt[i] == '%')
		{
			return (int) i;
		}
	}
	return -1;
}

int xlog_simd_count_specifiers(const char *fmt)
{
	if (!fmt)
	{
		return 0;
	}

	int count = 0;
	const char *p = fmt;

	while (*p)
	{
		if (*p == '%')
		{
			p++;
			if (*p == '\0')
			{
				break;
			}
			if (*p != '%')
			{  /* Skip %% */
				count++;
			}
		}
		p++;
	}

	return count;
}

/* ============================================================================
 * SIMD Timestamp Formatting
 * ============================================================================ */

int xlog_simd_format_datetime(int year, int month, int day,
                              int hour, int minute, int second,
                              char *buffer)
{
	char *p = buffer;

	/* Year: 4 digits */
	int y1 = year / 100;
	int y2 = year % 100;
	memcpy(p, &XLOG_DIGIT_TABLE[y1 * 2], 2);
	p += 2;
	memcpy(p, &XLOG_DIGIT_TABLE[y2 * 2], 2);
	p += 2;

	*p++ = '-';

	/* Month: 2 digits */
	memcpy(p, &XLOG_DIGIT_TABLE[month * 2], 2);
	p += 2;

	*p++ = '-';

	/* Day: 2 digits */
	memcpy(p, &XLOG_DIGIT_TABLE[day * 2], 2);
	p += 2;

	*p++ = ' ';

	/* Hour: 2 digits */
	memcpy(p, &XLOG_DIGIT_TABLE[hour * 2], 2);
	p += 2;

	*p++ = ':';

	/* Minute: 2 digits */
	memcpy(p, &XLOG_DIGIT_TABLE[minute * 2], 2);
	p += 2;

	*p++ = ':';

	/* Second: 2 digits */
	memcpy(p, &XLOG_DIGIT_TABLE[second * 2], 2);
	p += 2;

	*p = '\0';
	return 19;
}

int xlog_simd_format_usec(uint32_t usec, char *buffer)
{
	char *p = buffer;

	*p++ = '.';

	/* Ensure usec is in range */
	if (usec > 999999)
	{
		usec = 999999;
	}

	/* Format 6 digits with leading zeros */
	uint32_t hi = usec / 10000;  /* First 2 digits */
	uint32_t mid = (usec / 100) % 100;  /* Middle 2 digits */
	uint32_t lo = usec % 100;  /* Last 2 digits */

	memcpy(p, &XLOG_DIGIT_TABLE[hi * 2], 2);
	p += 2;
	memcpy(p, &XLOG_DIGIT_TABLE[mid * 2], 2);
	p += 2;
	memcpy(p, &XLOG_DIGIT_TABLE[lo * 2], 2);
	p += 2;

	*p = '\0';
	return 7;
}

/* ============================================================================
 * Simple strstr fallback (no SIMD optimization for now)
 * ============================================================================ */

const char *xlog_simd_strstr(const char *haystack, const char *needle)
{
	if (!haystack || !needle)
	{
		return NULL;
	}
	return strstr(haystack, needle);
}

