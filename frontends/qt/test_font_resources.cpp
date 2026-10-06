/* SPDX-License-Identifier: GPL-2.0-only */
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <cstdio>
#include <cstdlib>
extern "C" {
#include <libcss/properties.h>
#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include <wisp/utils/nsoption.h>
}
#include "qt/layout.h"
#include "qt/capture_inspect.h"

#define REQUIRE(e)                                                                                                     \
    do {                                                                                                               \
        if (!(e)) {                                                                                                    \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #e);                                                            \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    REQUIRE(argc >= 2);
    const bool benchmark = argc == 3 && QByteArray(argv[2]) == "--benchmark";
    REQUIRE(nsoption_init(nullptr, nullptr, nullptr) == NSERROR_OK);
    QFile source(QString::fromLocal8Bit(argv[1]));
    REQUIRE(source.open(QIODevice::ReadOnly));
    const QByteArray data = source.readAll();
    char name[] = "Shared resource";
    lwc_string *families[2] = {};
    REQUIRE(lwc_intern_string(name, sizeof(name) - 1, &families[0]) == lwc_error_ok);
    plot_font_style_t style = {};
    style.families = families;
    style.font_set = 1;
    style.family = PLOT_FONT_FAMILY_SANS_SERIF;
    style.size = 12 * PLOT_STYLE_SCALE;
    style.weight = 400;
    font_variant_id id = {1, 0, name, CSS_FONT_WEIGHT_400, CSS_FONT_STYLE_NORMAL};
    auto load = [&]() {
        REQUIRE(nsqt_layout_table->load_font_data(
                    &id, reinterpret_cast<const uint8_t *>(data.constData()), data.size()) == NSERROR_OK);
    };
    load();
    const QString native_family = nsqt_capture_font(&style).family();
    int expected;
    REQUIRE(nsqt_layout_table->width(&style, "MMMM", 4, &expected) == NSERROR_OK);
    QImage reference(400, 40, QImage::Format_ARGB32_Premultiplied);
    reference.fill(Qt::white);
    {
        QPainter painter(&reference);
        REQUIRE(nsqt_layout_plot(&painter, &style, 0, 25, "MMMM", 4) == NSERROR_OK);
    }
    qint64 binding_ns = 0, text_ns = 0;
    QElapsedTimer timer;
    const int rounds = benchmark ? 100 : 5;
    const auto initial_hits = nsqt_capture_text_cache_stats()["width_hits"].toInteger();
    for (int i = 0; i < rounds; i++) {
        id.font_set = style.font_set = 100 + i;
        timer.start();
        load();
        binding_ns += timer.nsecsElapsed();
        timer.restart();
        int width;
        REQUIRE(nsqt_layout_table->width(&style, "MMMM", 4, &width) == NSERROR_OK && width == expected);
        QImage actual(reference.size(), reference.format());
        actual.fill(Qt::white);
        {
            QPainter painter(&actual);
            REQUIRE(nsqt_layout_plot(&painter, &style, 0, 25, "MMMM", 4) == NSERROR_OK);
        }
        text_ns += timer.nsecsElapsed();
        REQUIRE(actual == reference);
        if (!benchmark)
            REQUIRE(nsqt_capture_font(&style).family() == native_family);
        nsqt_layout_table->release_font_set(id.font_set);
        REQUIRE(QFontDatabase::families().contains(native_family)); // Anchor still owns the resource.
    }
    if (!benchmark) {
        REQUIRE(nsqt_capture_text_cache_stats()["width_hits"].toInteger() >= initial_hits + rounds);
        // Identical bytes with different CSS metadata must not share a normalized face.
        id.font_set = style.font_set = 2;
        id.weight = CSS_FONT_WEIGHT_700;
        style.weight = 700;
        load();
        REQUIRE(nsqt_capture_font(&style).family() != native_family);
        nsqt_layout_table->release_font_set(2);
    }
    nsqt_layout_table->release_font_set(1);
    REQUIRE(!QFontDatabase::families().contains(native_family));
    lwc_string_unref(families[0]);
    nsoption_finalise(nullptr, nullptr);
    std::printf("%d document bindings: %.3f ms; first measure+paint: %.3f ms; identical pixels\n", rounds,
        binding_ns / 1e6, text_ns / 1e6);
    return 0;
}
