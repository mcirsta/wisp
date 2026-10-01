/* SPDX-License-Identifier: GPL-2.0-only */
/* Compare cached callbacks with their uncached results, including eviction
 * and caller-owned buffers whose contents/lifetimes change between calls. */
#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QWidget>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include <wisp/utils/nsoption.h>
}
#include "qt/capture_inspect.h"
#include "qt/layout.h"
#include "qt/misc.h"

nserror nsqt_schedule(int, void (*)(void *), void *)
{
    return NSERROR_OK;
}

#define REQUIRE(expr)                                                                                                  \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr);                                                    \
            std::exit(EXIT_FAILURE);                                                                                   \
        }                                                                                                              \
    } while (0)

struct Result {
    int width, split_width;
    size_t offset;
    bool operator==(const Result &other) const
    {
        return width == other.width && split_width == other.split_width && offset == other.offset;
    }
};

static Result measure(const plot_font_style_t &style, const QByteArray &text, int available)
{
    Result result;
    REQUIRE(nsqt_layout_table->width(&style, text.constData(), text.size(), &result.width) == NSERROR_OK);
    REQUIRE(nsqt_layout_table->split(
                &style, text.constData(), text.size(), available, &result.offset, &result.split_width) == NSERROR_OK);
    return result;
}

static qint64 stat(const char *name)
{
    return nsqt_capture_text_cache_stats()[name].toInteger();
}

class MetricsWidget : public QWidget
{
public:
    int dpi = 96;

protected:
    int metric(PaintDeviceMetric type) const override
    {
        return type == PdmDpiX || type == PdmDpiY ? dpi : QWidget::metric(type);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    REQUIRE(argc == 2);
    REQUIRE(nsoption_init(nullptr, nullptr, nullptr) == NSERROR_OK);
    int id = QFontDatabase::addApplicationFont(QString::fromLocal8Bit(argv[1]));
    REQUIRE(id >= 0);
    QByteArray family = QFontDatabase::applicationFontFamilies(id).first().toUtf8();
    nsoption_set_charp(font_sans, strdup(family.constData()));
    plot_font_style_t base = {};
    base.family = PLOT_FONT_FAMILY_SANS_SERIF;
    base.size = 12 * PLOT_STYLE_SCALE;
    base.weight = 400;
    const qsizetype budget = stat("budget_bytes");

    struct Request {
        plot_font_style_t style;
        QByteArray text;
        int available;
        Result expected;
    };
    std::vector<Request> requests;
    const QByteArray texts[] = {"", " ", "a b c", QByteArray("a\0b c", 5), "AVATAR office affinity final",
        "café știri renderer final", "a\u0301bc \U0001F469\u200d\U0001F4BBxyz final",
        "\u0633\u0644\u0627\u0645 \u0627\u0644\u0639\u0627\u0644\u0645 renderer"};
    nsqt_capture_text_cache_limit(0);
    for (int size : {0, 12 * PLOT_STYLE_SCALE, 97 * PLOT_STYLE_SCALE / 8}) {
        for (auto flags : {FONTF_NONE, FONTF_SIZE_PIXELS, FONTF_ITALIC, FONTF_SMALLCAPS}) {
            for (int spacing : {-1, 0, 2}) {
                for (int weight : {400, 700}) {
                    auto style = base;
                    style.size = size;
                    style.flags = flags;
                    style.letter_spacing = spacing;
                    style.weight = weight;
                    for (const auto &text : texts) {
                        for (int available : {-1, 0, 23, 24, 80, 400})
                            requests.push_back({style, text, available, measure(style, text, available)});
                    }
                }
            }
        }
    }
    REQUIRE(stat("entries") == 0);
    for (qsizetype limit : {budget, qsizetype(1024)}) {
        nsqt_capture_text_cache_limit(limit);
        for (size_t i = 0; i < requests.size(); ++i) {
            const auto &r = requests[i];
            for (int repeat = 0; repeat < 2; ++repeat) {
                Result actual = measure(r.style, r.text, r.available);
                if (!(actual == r.expected))
                    std::fprintf(stderr, "request %zu, repeat %d, budget %lld: cached %d/%zu/%d, expected %d/%zu/%d\n",
                        i, repeat, (long long)limit, actual.width, actual.offset, actual.split_width, r.expected.width,
                        r.expected.offset, r.expected.split_width);
                REQUIRE(actual == r.expected);
                REQUIRE(stat("cost_bytes") <= limit);
            }
        }
        REQUIRE(stat("width_hits") >= qint64(requests.size()));
        REQUIRE(stat("split_hits") >= qint64(requests.size()));
    }

    // Same address and length, different bytes. Stored keys must own their text.
    nsqt_capture_text_cache_limit(budget);
    QByteArray mutable_text("a bbbbbbb");
    Result narrow = measure(base, mutable_text, 40);
    std::memcpy(mutable_text.data(), "bbbbbbb a", 9);
    Result wide = measure(base, mutable_text, 40);
    REQUIRE(wide.offset != narrow.offset || wide.split_width != narrow.split_width);
    mutable_text.clear();
    REQUIRE(measure(base, QByteArray("a bbbbbbb"), 40) == narrow);
    REQUIRE(measure(base, QByteArray("bbbbbbb a"), 40) == wide);
    REQUIRE(stat("width_hits") == 2 && stat("split_hits") == 2);

    // Colour is irrelevant to metrics and must not cause another shape operation.
    auto coloured = base;
    coloured.foreground = 0x123456;
    coloured.background = 0xabcdef;
    REQUIRE(measure(coloured, "a bbbbbbb", 40) == narrow);
    REQUIRE(stat("width_hits") == 3 && stat("split_hits") == 3);

    // Check true LRU eviction: keep one pair hot while newer strings displace
    // the other pair. The chosen budget holds two pairs but not three.
    nsqt_capture_text_cache_limit(budget);
    measure(base, "a b", 23);
    qsizetype pair_cost = stat("cost_bytes");
    nsqt_capture_text_cache_limit(pair_cost * 2);
    measure(base, "a b", 23);
    measure(base, "c d", 23);
    measure(base, "a b", 23);
    measure(base, "e f", 23);
    qint64 hits = stat("width_hits");
    measure(base, "a b", 23);
    REQUIRE(stat("width_hits") == hits + 1);
    qint64 misses = stat("width_misses");
    measure(base, "c d", 23);
    REQUIRE(stat("width_misses") == misses + 1);

    // Oversized inputs still use the original path without evicting small entries.
    QByteArray long_text(8192, 'a');
    nsqt_capture_text_cache_limit(0);
    Result long_expected = measure(base, long_text, 100000);
    nsqt_capture_text_cache_limit(budget);
    measure(base, "a b", 23);
    qint64 entries = stat("entries");
    REQUIRE(measure(base, long_text, 100000) == long_expected);
    REQUIRE(stat("entries") == entries);

    // A new screen/device DPI must not reuse the previous metrics entry.
    MetricsWidget device;
    nsqt_capture_text_cache_limit(budget);
    measure(base, "a b", 23);
    REQUIRE(stat("entries") == 2);
    device.dpi = 144;
    Result at144 = measure(base, "a b", 23);
    REQUIRE(stat("entries") == 4);
    REQUIRE(stat("width_misses") == 2 && stat("split_misses") == 2);
    nsqt_capture_text_cache_limit(0);
    REQUIRE(measure(base, "a b", 23) == at144);

    nsoption_finalise(nullptr, nullptr);
    std::puts(
        "Qt text cache: uncached parity, UTF-8, fractional/zero sizing, spacing, styles, ownership and LRU passed.");
    return EXIT_SUCCESS;
}
