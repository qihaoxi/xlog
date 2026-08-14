#!/bin/bash
# compress.sh - Generate single header version of xlog
#
# Only removes #include "xxx.h" (internal includes).
# All header guards and other preprocessor directives are kept intact.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src"
INCLUDE_DIR="$SCRIPT_DIR/include"
# Allow caller to override output directory (e.g., build tree)
OUTPUT_DIR="${SINGLE_OUT_DIR:-$SCRIPT_DIR/single_include}"
OUTPUT_FILE="$OUTPUT_DIR/xlog.h"

mkdir -p "$OUTPUT_DIR"

echo "============================================================"
echo "  xlog Single Header Generator"
echo "============================================================"
echo ""

# Function: remove only #include "xxx.h" lines
# Uses sed to preserve all characters (including ||) in the file
filter_internal_includes() {
    sed '/^[[:space:]]*#[[:space:]]*include[[:space:]]*"/d' "$1"
}

# Start generating the single header file
cat > "$OUTPUT_FILE" << 'HEADER'
/* =====================================================================================
 *       Filename:  xlog.h (Single Header Version)
 *    Description:  xlog - High Performance Async Logging Library for C
 *        Version:  1.0.0
 *      Generated:  Auto-generated - DO NOT EDIT
 *         Author:  qihao.xi (qhxi)
 * =====================================================================================
 *
 * USAGE:
 *   In ONE .c file, before including:
 *     #define XLOG_IMPLEMENTATION
 *     #include "xlog.h"
 *
 *   In other files:
 *     #include "xlog.h"
 *
 * EXAMPLE:
 *     #define XLOG_IMPLEMENTATION
 *     #include "xlog.h"
 *
 *     int main(void) {
 *         xlog_init_console(XLOG_LEVEL_DEBUG);
 *         XLOG_INFO("Hello %s", "world");
 *         xlog_shutdown();
 *         return 0;
 *     }
 *
 * COMPILE:
 *     gcc -o myapp myapp.c -lpthread
 *
 * NOTE: Use XLOG_* macros to avoid conflicts with syslog.h
 * =====================================================================================
 */

#ifndef XLOG_SINGLE_HEADER_H
#define XLOG_SINGLE_HEADER_H

/* ============================================================================
 * MSVC Compatibility: stdatomic.h handling
 * ============================================================================
 * MSVC's C11 stdatomic.h support is incomplete/buggy until VS2022.
 * We provide a Windows Interlocked API fallback for older versions.
 *
 * NOTE: For best MSVC compatibility, compile as C++ with:
 *       cl /TP /EHsc your_file.c
 *   Or use MSVC 2022+ which has better C11/C17 support.
 */
#ifdef _MSC_VER
    /* Define compiler detection macros early (consistent with PEL) */
    #ifndef XLOG_COMPILER_MSVC
    #define XLOG_COMPILER_MSVC 1
    #define XLOG_MSVC_VERSION _MSC_VER
    #endif

    /* Disable warnings for MSVC */
    #pragma warning(disable: 4201)  /* nameless struct/union */
    #pragma warning(disable: 4204)  /* non-constant aggregate initializer */
    #pragma warning(disable: 4221)  /* cannot be initialized using address of automatic variable */
    #pragma warning(disable: 4819)  /* code page warning */
    #pragma warning(disable: 4996)  /* deprecated functions */

    /* 注：老 MSVC（<VS2022）的 stdatomic/alignas/_Static_assert fallback 不在此预导言
     * 重复携带——统一由 PART 2 依赖序列中的 src/platform.h 提供（唯一权威实现，
     * 含 64 位 Interlocked 分派与 CAS expected 回写）。公共 API（PART 1）不用原子。 */

    /* MSVC C mode doesn't support _Generic */
    #ifndef __cplusplus
        #define XLOG_NO_GENERIC 1
    #endif
#endif

/* Platform detection and POSIX compatibility for Windows */
#ifdef _MSC_VER
    #ifndef XLOG_PLATFORM_WINDOWS
    #define XLOG_PLATFORM_WINDOWS 1
    #endif

    /* Include Windows headers */
    #include <io.h>
    #include <process.h>
    #include <BaseTsd.h>

    /* ssize_t for MSVC */
    #ifndef ssize_t
    typedef SSIZE_T ssize_t;
    #endif

    /* POSIX function compatibility */
    #define access _access
    #define write _write
    #define read _read
    #define open _open
    #define close _close
    #define isatty _isatty
    #define fileno _fileno
    #define F_OK 0
    #define W_OK 2
    #define R_OK 4

    /* Standard file descriptors */
    #ifndef STDIN_FILENO
    #define STDIN_FILENO 0
    #endif
    #ifndef STDOUT_FILENO
    #define STDOUT_FILENO 1
    #endif
    #ifndef STDERR_FILENO
    #define STDERR_FILENO 2
    #endif

    /* Thread-local storage */
    #define _Thread_local __declspec(thread)
    #define XLOG_THREAD_LOCAL __declspec(thread)

    /* localtime_r replacement (use localtime_s on Windows) */
    #ifndef localtime_r
    #define localtime_r(timep, result) (localtime_s((result), (timep)) == 0 ? (result) : NULL)
    #endif
#else
    #define XLOG_THREAD_LOCAL __thread
#endif

/*
 * ============================================================================
 * PART 1: PUBLIC API
 * ============================================================================
 */

HEADER

echo "Including public API..."
echo "Including public API..."
filter_internal_includes "$INCLUDE_DIR/xlog.h" >> "$OUTPUT_FILE"
cat >> "$OUTPUT_FILE" << 'IMPL_PART2'
/*
 * ============================================================================
 * PART 2: IMPLEMENTATION (only when XLOG_IMPLEMENTATION is defined)
 * ============================================================================
 */
#ifdef XLOG_IMPLEMENTATION
#ifndef XLOG_IMPLEMENTATION_GUARD
#define XLOG_IMPLEMENTATION_GUARD
/* Configure miniz for deflate-only (reduces size by ~60%) */
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_INFLATE_APIS
#define MINIZ_NO_ARCHIVE_APIS
IMPL_PART2
# Include miniz header and implementation
echo "Including miniz (deflate-only configuration)..."
if [ -f "$SRC_DIR/miniz.h" ]; then
    echo "" >> "$OUTPUT_FILE"
    echo "/* -------- src/miniz.h (deflate-only) -------- */" >> "$OUTPUT_FILE"
    filter_internal_includes "$SRC_DIR/miniz.h" >> "$OUTPUT_FILE"
fi
if [ -f "$SRC_DIR/miniz_impl.c" ]; then
    echo "" >> "$OUTPUT_FILE"
    echo "/* -------- src/miniz_impl.c (deflate-only) -------- */" >> "$OUTPUT_FILE"
    # Remove the #include "miniz.h" line since it's already included
    grep -v '#include "miniz.h"' "$SRC_DIR/miniz_impl.c" | grep -v '^/\* xlog:' >> "$OUTPUT_FILE"
fi
# Headers in dependency order
echo "Including internal headers..."
for h in level.h platform.h color.h ringbuf.h log_record.h \
         sink.h compress.h rotate.h batch_writer.h simd.h console_sink.h \
         file_sink.h syslog_sink.h xlog_core.h formatter.h xlog_builder.h; do
    if [ -f "$SRC_DIR/$h" ]; then
        echo "  $h"
        echo "" >> "$OUTPUT_FILE"
        echo "/* -------- src/$h -------- */" >> "$OUTPUT_FILE"
        filter_internal_includes "$SRC_DIR/$h" >> "$OUTPUT_FILE"
    fi
done
# Source files (including compress.c)
echo "Including source files..."
for s in platform.c color.c ringbuf.c log_record.c sink.c rotate.c \
         batch_writer.c simd.c console_sink.c file_sink.c syslog_sink.c \
         xlog.c formatter.c xlog_builder.c compress.c; do
    if [ -f "$SRC_DIR/$s" ]; then
        echo "  $s"
        echo "" >> "$OUTPUT_FILE"
        echo "/* -------- src/$s -------- */" >> "$OUTPUT_FILE"
        filter_internal_includes "$SRC_DIR/$s" >> "$OUTPUT_FILE"
    fi
done
cat >> "$OUTPUT_FILE" << 'FOOTER'
#endif /* XLOG_IMPLEMENTATION_GUARD */
#endif /* XLOG_IMPLEMENTATION */
#endif /* XLOG_SINGLE_HEADER_H */
FOOTER
LINES=$(wc -l < "$OUTPUT_FILE")
SIZE=$(ls -lh "$OUTPUT_FILE" | awk '{print $5}')
echo ""
echo "============================================================"
echo "  Output: $OUTPUT_FILE"
echo "  Lines:  $LINES"
echo "  Size:   $SIZE"
echo "============================================================"
echo ""
echo "To test:"
echo "  gcc -I $OUTPUT_DIR test_single_header.c -lpthread -o test_single_header"
echo "  ./test_single_header"
echo ""
