/* SPDX-License-Identifier: GPL-2.0-only */
/* Exercise the real Qt font callbacks and retained text boxes across reflow. */
#include <QApplication>
#include <QPainter>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include <wisp/utils/nsoption.h>
void test_text_reflow(const struct gui_layout_table *fonts);
}
#include "qt/layout.h"
#include "qt/misc.h"

// No web fonts are loaded by this unit test.
nserror nsqt_schedule(int, void (*)(void *), void *) { return NSERROR_OK; }

#define REQUIRE(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); std::exit(EXIT_FAILURE); \
} } while (0)

static int measure(const plot_font_style_t &font, const char *text, size_t length)
{
    int width;
    REQUIRE(nsqt_layout_table->width(&font, text, length, &width) == NSERROR_OK);
    return width;
}

static void check_split(const plot_font_style_t &font, const char *text, const char *prefix, int extra)
{
    size_t expected = std::strlen(prefix), offset;
    int expected_width = measure(font, prefix, expected), width;
    REQUIRE(nsqt_layout_table->split(&font, text, std::strlen(text), expected_width + extra,
        &offset, &width) == NSERROR_OK);
    REQUIRE(offset == expected);
    REQUIRE(width == expected_width);
    REQUIRE(offset == std::strlen(text) || text[offset] == ' ');
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    REQUIRE(nsoption_init(nullptr, nullptr, nullptr) == NSERROR_OK);
    plot_font_style_t font = {};
    font.family = PLOT_FONT_FAMILY_SANS_SERIF;
    font.size = 12 * PLOT_STYLE_SCALE;
    font.weight = 400;

    // Exact fits, room before the next word, overlong first words and UTF-8.
    for (int extra : {0, 1}) {
        check_split(font, "alpha beta gamma", "alpha", extra);
        check_split(font, "alpha beta gamma", "alpha beta", extra);
        check_split(font, "café știri renderer", "café știri", extra);
        check_split(font, "alpha beta", "alpha beta", extra);
    }
    check_split(font, "unbreakable next", "unbreakable", -1);
    check_split(font, "unbreakable", "unbreakable", -1);
    check_split(font, "alpha beta", "alpha", -measure(font, "alpha", 5));
    check_split(font, "alpha beta", "alpha", -measure(font, "alpha", 5) - 10);

    test_text_reflow(nsqt_layout_table);
    nsoption_finalise(nullptr, nullptr);
    std::puts("Qt text split: boundaries, widths, UTF-8 and repeated/resized layout passed.");
    return EXIT_SUCCESS;
}
