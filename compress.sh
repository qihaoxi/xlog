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
filter_internal_includes() {
    grep -v '^[[:space:]]*#[[:space:]]*include[[:space:]]*"' "$1" || true
}

# Start generating the single header file
cat > "$OUTPUT_FILE" << 'HEADER'
/* =====================================================================================
 *       Filename:  xlog.h (Single Header Version)
 *    Description:  xlog - High Performance Async Logging Library for C
 *        Version:  1.0.0
 *      Generated:  Auto-generated - DO NOT EDIT
 *         Author:  qihao.xi (qhxi), xiqh@onecloud.cn
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
 *         xlog_init_console(LOG_LEVEL_DEBUG);
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

/*
 * ============================================================================
 * PART 1: PUBLIC API
 * ============================================================================
 */

HEADER

echo "Including public API..."
filter_internal_includes "$INCLUDE_DIR/xlog.h" >> "$OUTPUT_FILE"

cat >> "$OUTPUT_FILE" << 'IMPL_START'

/*
 * ============================================================================
 * PART 2: IMPLEMENTATION (only when XLOG_IMPLEMENTATION is defined)
 * ============================================================================
 */

#ifdef XLOG_IMPLEMENTATION
#ifndef XLOG_IMPLEMENTATION_GUARD
#define XLOG_IMPLEMENTATION_GUARD

IMPL_START

# Headers in dependency order
echo "Including internal headers..."
for h in config.h level.h platform.h color.h ringbuf.h log_record.h \
         sink.h rotate.h batch_writer.h simd.h console_sink.h \
         file_sink.h syslog_sink.h xlog_core.h xlog_builder.h; do
    if [ -f "$SRC_DIR/$h" ]; then
        echo "  $h"
        echo "" >> "$OUTPUT_FILE"
        echo "/* -------- src/$h -------- */" >> "$OUTPUT_FILE"
        filter_internal_includes "$SRC_DIR/$h" >> "$OUTPUT_FILE"
    fi
done

# Source files
echo "Including source files..."
for s in platform.c color.c ringbuf.c log_record.c sink.c rotate.c \
         batch_writer.c simd.c console_sink.c file_sink.c syslog_sink.c \
         xlog.c xlog_builder.c; do
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
