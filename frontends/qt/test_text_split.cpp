/* SPDX-License-Identifier: GPL-2.0-only */
/* Exercise the real Qt font callbacks and retained text boxes across reflow. */
#include <QApplication>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include <wisp/utils/nsoption.h>
void test_text_reflow(const struct gui_layout_table *fonts);
void test_fractional_text_reflow(const struct gui_layout_table *fonts);
void test_fragment_rounding_reflow(const struct gui_layout_table *fonts);
void test_inline_spacing_reflow(const struct gui_layout_table *fonts);
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
    if (offset != expected || width != expected_width)
        std::fprintf(stderr, "split '%s' size=%d spacing=%d available=%d: got %zu/%d, expected %zu/%d\n",
            text, font.size, font.letter_spacing, expected_width + extra, offset, width, expected, expected_width);
    REQUIRE(offset == expected);
    REQUIRE(width == expected_width);
    REQUIRE(offset == std::strlen(text) || text[offset] == ' ');
}

static void check_prefix_boundaries(const plot_font_style_t &base)
{
    // Compare split results to independent width calls at complete words.
    // UTF-8 byte offsets differ from UTF-16 indices; shaping must still use
    // the exact prefix, including combining marks, ligatures and bidi text.
    const char *texts[] = {
        "AVATAR office affinity final",
        "caf\xc3\xa9 \xc8\x99tiri renderer final",
        "a\u0301bc \U0001F600xyz \U0001F469\u200d\U0001F4BBxyz final",
        "\u0633\u0644\u0627\u0645 \u0627\u0644\u0639\u0627\u0644\u0645 renderer final",
        "iiiiiiiiiiiiiiiiiiiiiiii WWWWWWWWWWWWWWWWWWWWWWWW iii WWW iii WWW final",
    };
    for (int size : {9 * PLOT_STYLE_SCALE, 101 * PLOT_STYLE_SCALE / 8, 15 * PLOT_STYLE_SCALE}) {
        for (int spacing : {-1, 0, 2}) {
            plot_font_style_t font = base;
            font.size = size;
            font.letter_spacing = spacing;
            for (const char *text : texts) {
                QByteArray bytes(text);
                for (int end = 1; end <= bytes.size(); end++) {
                    if (end != bytes.size() && bytes[end] != ' ')
                        continue;
                    QByteArray prefix = bytes.left(end);
                    for (int extra : {0, 1})
                        check_split(font, text, prefix.constData(), extra);
                }
            }
        }
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    REQUIRE(argc == 2);
    REQUIRE(nsoption_init(nullptr, nullptr, nullptr) == NSERROR_OK);
    // Reuse the generated test face: at 16px, "a b" advances 23.1875px.
    // Install it before the first font lookup so results do not depend on the host's Sans.
    int font_id = QFontDatabase::addApplicationFont(QString::fromLocal8Bit(argv[1]));
    REQUIRE(font_id >= 0);
    QByteArray family = QFontDatabase::applicationFontFamilies(font_id).first().toUtf8();
    nsoption_set_charp(font_sans, strdup(family.constData()));
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

    QFont face(QString::fromUtf8(family));
    face.setPixelSize(16);
    qreal advance = QFontMetricsF(face).horizontalAdvance("a b");
    REQUIRE(advance > 23 && advance < 23.5);
    REQUIRE(measure(font, "a b", 3) == 24);
    struct {
        const char *text;
        int available;
        const char *prefix;
        int width;
    } cases[] = {
        {"a b", 23, "a", 10},     // Whole-string fit must not round down.
        {"a b c", 23, "a", 10},   // Neither may the initial/greedy split estimate.
        {"a b c", 24, "a b", 24}, // Just enough room for the fractional advance.
        {"a b", 24, "a b", 24},
        {"a", 9, "a", 10},        // An overlong first word still makes progress.
        {"", 0, "", 0},
    };
    for (const auto &test : cases) {
        size_t offset;
        int width;
        REQUIRE(nsqt_layout_table->split(&font, test.text, std::strlen(test.text), test.available,
            &offset, &width) == NSERROR_OK);
        if (offset != std::strlen(test.prefix) || width != test.width)
            std::fprintf(stderr, "fractional split '%s' in %dpx: offset=%zu width=%d\n",
                test.text, test.available, offset, width);
        REQUIRE(offset == std::strlen(test.prefix));
        REQUIRE(width == test.width);
    }
    // At 20px the same face has integral advances: exact fits must still fit.
    plot_font_style_t integral = font;
    integral.size = 15 * PLOT_STYLE_SCALE;
    REQUIRE(measure(integral, "a b", 3) == 29);
    check_split(integral, "a b c", "a b", 0);
    // A 13px face has a 3.25px space; do not inflate every inter-box gap to 4px.
    plot_font_style_t spacing = font;
    spacing.size = 39 * PLOT_STYLE_SCALE / 4;
    REQUIRE(measure(spacing, " ", 1) == 3);

    check_prefix_boundaries(font);
    test_text_reflow(nsqt_layout_table);
    test_inline_spacing_reflow(nsqt_layout_table);
    test_fractional_text_reflow(nsqt_layout_table);
    test_fragment_rounding_reflow(nsqt_layout_table);
    nsoption_finalise(nullptr, nullptr);
    std::puts("Qt text split: fractional/exact boundaries, widths, UTF-8 and repeated/resized layout passed.");
    return EXIT_SUCCESS;
}
