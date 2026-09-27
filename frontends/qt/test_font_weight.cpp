/* SPDX-License-Identifier: GPL-2.0-only */
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QPainter>
#include <QRawFont>
#include <QtEndian>
#include <QtMath>
#include <cstdio>
#include <cstdlib>

extern "C" {
#include <libcss/properties.h>
#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include <wisp/utils/nsoption.h>
}
#include "qt/capture_inspect.h"
#include "qt/layout.h"
#include "qt/misc.h"

nserror nsqt_schedule(int, void (*)(void *), void *) { return NSERROR_OK; }

#define REQUIRE(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); std::exit(EXIT_FAILURE); \
} } while (0)

struct Family {
    QByteArray alias;
    lwc_string *names[2] = {};
    plot_font_style_t style = {};

    explicit Family(const char *name) : alias(name)
    {
        REQUIRE(lwc_intern_string(alias.constData(), alias.size(), &names[0]) == lwc_error_ok);
        style.families = names;
        style.family = PLOT_FONT_FAMILY_SANS_SERIF;
        style.size = 12 * PLOT_STYLE_SCALE;
    }
    ~Family() { lwc_string_unref(names[0]); }

    void load(const QString &directory, const char *file, int weight, bool italic = false)
    {
        QFile source(directory + "/" + file);
        REQUIRE(source.open(QIODevice::ReadOnly));
        QByteArray data = source.readAll();
        font_variant_id id = {alias.data(), uint8_t(CSS_FONT_WEIGHT_100 + weight / 100 - 1),
            uint8_t(italic ? CSS_FONT_STYLE_ITALIC : CSS_FONT_STYLE_NORMAL)};
        REQUIRE(nsqt_layout_table->load_font_data(&id,
            reinterpret_cast<const uint8_t *>(data.constData()), data.size()) == NSERROR_OK);
    }

    QFont font(int requested, bool italic = false)
    {
        style.weight = requested;
        style.flags = italic ? FONTF_ITALIC : plot_font_flags_t(0);
        return nsqt_capture_font(&style);
    }

    void check(int requested, int expected, bool italic = false)
    {
        for (int repeat = 0; repeat < 2; repeat++) {
            QFont selected = font(requested, italic);
            QRawFont raw = QRawFont::fromFont(selected);
            QByteArray os2 = raw.fontTable("OS/2");
            REQUIRE(os2.size() >= 8);
            int actual = qFromBigEndian<quint16>(os2.constData() + 4);
            if (actual != expected)
                std::fprintf(stderr, "%s %d italic=%d: expected %d, got %d (%s)\n", alias.constData(),
                    requested, italic, expected, actual, raw.styleName().toUtf8().constData());
            REQUIRE(actual == expected);
            REQUIRE(qFromBigEndian<quint16>(os2.constData() + 6) == 5); // Not a condensed face.
            REQUIRE(selected.italic() == italic);
            int width;
            REQUIRE(nsqt_layout_table->width(&style, "MMMM", 4, &width) == NSERROR_OK);
            REQUIRE(width == qCeil(QFontMetricsF(selected).horizontalAdvance("MMMM")));

            QImage actual_image(100, 40, QImage::Format_ARGB32_Premultiplied);
            actual_image.fill(Qt::white);
            QImage expected_image = actual_image.copy();
            QPainter actual_painter(&actual_image), expected_painter(&expected_image);
            REQUIRE(nsqt_layout_plot(&actual_painter, &style, 0, 25, "MMMM", 4) == NSERROR_OK);
            expected_painter.setFont(selected);
            expected_painter.drawText(0, 25, "MMMM");
            actual_painter.end();
            expected_painter.end();
            REQUIRE(actual_image == expected_image);
        }
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    REQUIRE(argc == 2);
    REQUIRE(nsoption_init(nullptr, nullptr, nullptr) == NSERROR_OK);
    QString directory = QString::fromLocal8Bit(argv[1]);
    Family sparse("Wisp CSS Weight Alias"), normal("Wisp CSS Normal Alias"), variable("Wisp CSS Variable Alias");
    sparse.load(directory, "weight-500.ttf", 500);
    sparse.load(directory, "weight-700.ttf", 700);
    sparse.check(600, 700); // Missing 600 must prefer 700 over the equally close 500.
    sparse.check(400, 500);
    sparse.check(500, 500);
    sparse.check(800, 700);
    sparse.load(directory, "weight-300.ttf", 300);
    sparse.check(400, 500); // 400 prefers 500 before trying lower weights.
    sparse.load(directory, "weight-100.ttf", 100);
    sparse.check(200, 100);
    sparse.load(directory, "condensed-900.ttf", 900);
    sparse.check(800, 700); // A narrower face must not supply the missing weight.
    sparse.load(directory, "weight-900.ttf", 900);
    sparse.check(800, 900);
    sparse.load(directory, "italic-500.ttf", 500, true);
    sparse.load(directory, "italic-700.ttf", 700, true);
    sparse.check(600, 700, true);
    sparse.check(600, 700);
    sparse.load(directory, "weight-600.ttf", 600); // A font arriving later invalidates the cached fallback.
    sparse.check(600, 600);
    sparse.check(600, 700, true); // Upright 600 must not displace italic 700.

    normal.load(directory, "normal-400.ttf", 400);
    normal.check(700, 400);
    REQUIRE(normal.font(700).weight() == QFont::Bold); // Preserve Qt's synthetic bold request.
    normal.load(directory, "normal-700.ttf", 700);
    normal.check(400, 400);
    normal.check(500, 400); // Missing 500 prefers 400 before 700.
    normal.check(600, 700);

    variable.load(directory, "variable.ttf", 400);
    QFont selected = variable.font(600);
    REQUIRE(!QRawFont::fromFont(selected).fontTable("fvar").isEmpty());
    REQUIRE(selected.weight() == QFont::DemiBold); // Do not snap a continuous range to a named instance.
    QFont native("Wisp Variable Test", -1, 600);
    native.setPixelSize(16);
    REQUIRE(QFontMetrics(selected).horizontalAdvance("MMMM") == QFontMetrics(native).horizontalAdvance("MMMM"));
    nsoption_finalise(nullptr, nullptr);
    std::puts("Qt font weights: CSS fallback, exact weights, style/width, cache reload, painting and variable fonts passed.");
    return EXIT_SUCCESS;
}
