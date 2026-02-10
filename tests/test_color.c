/* test_color.c - Test ANSI color output */
#include <stdio.h>
#include <string.h>
#include "color.h"
#include "level.h"
int main(void) {
    printf("============================================\n");
    printf("  ANSI Color Output Test\n");
    printf("============================================\n\n");
    /* Initialize color support */
    xlog_color_init();
    /* Check TTY support */
    printf("Color support (stdout): %s\n", xlog_color_supported(1) ? "yes" : "no");
    printf("Color support (stderr): %s\n\n", xlog_color_supported(2) ? "yes" : "no");
    /* Test different color schemes */
    printf("=== Default Color Scheme ===\n");
    xlog_color_set_mode(XLOG_COLOR_ALWAYS);
    char buf[256];
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_TRACE);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_DEBUG);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_INFO);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_WARNING);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_ERROR);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_FATAL);
    printf("  %s message\n", buf);
    /* Test vivid scheme */
    printf("\n=== Vivid Color Scheme ===\n");
    xlog_color_set_custom(xlog_color_get_scheme(XLOG_SCHEME_VIVID));
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_TRACE);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_DEBUG);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_INFO);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_WARNING);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_ERROR);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_FATAL);
    printf("  %s message\n", buf);
    /* Test pastel scheme */
    printf("\n=== Pastel Color Scheme ===\n");
    xlog_color_set_custom(xlog_color_get_scheme(XLOG_SCHEME_PASTEL));
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_TRACE);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_DEBUG);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_INFO);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_WARNING);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_ERROR);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_FATAL);
    printf("  %s message\n", buf);
    /* Test monochrome scheme */
    printf("\n=== Monochrome Scheme ===\n");
    xlog_color_set_custom(xlog_color_get_scheme(XLOG_SCHEME_MONOCHROME));
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_TRACE);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_DEBUG);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_INFO);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_WARNING);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_ERROR);
    printf("  %s message\n", buf);
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_FATAL);
    printf("  %s message\n", buf);
    /* Test timestamp coloring */
    printf("\n=== Timestamp Coloring ===\n");
    xlog_color_set_custom(xlog_color_get_scheme(XLOG_SCHEME_DEFAULT));
    xlog_color_format_timestamp(buf, sizeof(buf), "2026-02-09 14:30:45.123456");
    printf("  Timestamp: %s\n", buf);
    /* Test color stripping */
    printf("\n=== Color Stripping ===\n");
    const char *colored = "\033[1m\033[31mBold Red Text\033[0m";
    char stripped[256];
    xlog_color_strip(stripped, sizeof(stripped), colored);
    printf("  Original length: %zu\n", strlen(colored));
    printf("  Stripped: '%s' (len=%zu)\n", stripped, strlen(stripped));
    printf("  Display width: %zu\n", xlog_color_display_width(colored));
    /* Test color modes */
    printf("\n=== Color Modes ===\n");
    xlog_color_set_mode(XLOG_COLOR_ALWAYS);
    printf("  ALWAYS mode: ");
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_INFO);
    printf("%s\n", buf);
    xlog_color_set_mode(XLOG_COLOR_NEVER);
    printf("  NEVER mode:  ");
    xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_INFO);
    printf("%s\n", buf);
    printf("\n============================================\n");
    printf("  Test completed!\n");
    printf("============================================\n");
    return 0;
}
