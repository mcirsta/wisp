/* SPDX-License-Identifier: GPL-2.0-only */
/* Exercise size scaling through the real font callbacks and QPainter. */
#include <QApplication>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QtMath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <wisp/layout.h>
#include <wisp/plot_style.h>
#include <wisp/utils/nsoption.h>
}
#include "qt/layout.h"
#include "qt/misc.h"

nserror nsqt_schedule(int, void (*)(void *), void *) { return NSERROR_OK; }

#define REQUIRE(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); std::exit(EXIT_FAILURE); \
} } while (0)

static void check_font(const QString &family, qreal pixels, bool svg, int spacing)
{
    std::fprintf(stderr, "size=%g SVG=%d spacing=%d\n", double(pixels), svg, spacing);
    plot_font_style_t style = {};
    style.family = PLOT_FONT_FAMILY_SANS_SERIF;
    style.size = qRound(pixels * PLOT_STYLE_SCALE * (svg ? 1 : 0.75));
    style.flags = svg ? FONTF_SIZE_PIXELS : FONTF_NONE;
    style.weight = 400;
    style.letter_spacing = spacing;
    // Account only for the plot API's fixed-point quantization, not whole pixels.
    pixels = qreal(style.size) / PLOT_STYLE_SCALE * (svg ? 1 : 4.0 / 3);
    QFont font(family);
    font.setPixelSize(qMax(1, int(pixels)));
    qreal scale = pixels / font.pixelSize();
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacing / scale);
    QFontMetricsF metrics(font);
    const QByteArray text = QString::fromUtf8("café alpha beta gamma").toUtf8();
    auto advance = [&](int length) {
        return metrics.horizontalAdvance(QString::fromUtf8(text.constData(), length)) * scale;
    };
    int width;
    REQUIRE(nsqt_layout_table->width(&style, text.constData(), text.size(), &width) == NSERROR_OK);
    REQUIRE(width == (svg ? qRound(advance(text.size())) : qCeil(advance(text.size()))));
    int ascent, descent;
    REQUIRE(nsqt_layout_table->font_metrics(&style, &ascent, &descent) == NSERROR_OK);
    REQUIRE(ascent == qRound(metrics.ascent() * scale));
    REQUIRE(descent == qRound(metrics.descent() * scale));

    // Every word boundary: the next word fits only when its scaled advance fits.
    for (int end : {5, 11, 16, 22}) {
        REQUIRE(end <= text.size());
        size_t offset;
        int actual;
        int fit = qCeil(advance(end));
        REQUIRE(nsqt_layout_table->split(&style, text.constData(), text.size(), fit,
            &offset, &actual) == NSERROR_OK);
        REQUIRE(offset == size_t(end));
        REQUIRE(actual == fit);
        if (end > 5) {
            REQUIRE(nsqt_layout_table->split(&style, text.constData(), text.size(), fit - 1,
                &offset, &actual) == NSERROR_OK);
            REQUIRE(offset < size_t(end));
            REQUIRE(actual <= fit - 1);
        }
    }
    // Hit testing must return UTF-8 byte positions and scaled cursor coordinates.
    for (int end : {3, 5, 11, 16}) {
        size_t offset;
        int actual;
        REQUIRE(nsqt_layout_table->position(&style, text.constData(), text.size(), qFloor(advance(end)) + 1,
            &offset, &actual) == NSERROR_OK);
        REQUIRE(offset == size_t(end));
        REQUIRE(actual == qRound(advance(end)));
    }
    // Absolute letter spacing stays absolute even when glyphs are scaled.
    plot_font_style_t plain = style;
    plain.letter_spacing = 0;
    int plain_width;
    REQUIRE(nsqt_layout_table->width(&plain, text.constData(), text.size(), &plain_width) == NSERROR_OK);
    int characters = QString::fromUtf8(text).size();
    REQUIRE(qAbs(width - plain_width - characters * spacing) <= 1);

    for (qreal zoom : {0.6, 1.0, 1.25}) {
        for (int dpi : {96, 144}) {
            QImage actual(400, 100, QImage::Format_ARGB32_Premultiplied);
            actual.setDotsPerMeterX(qRound(dpi / 0.0254));
            actual.setDotsPerMeterY(qRound(dpi / 0.0254));
            actual.fill(Qt::white);
            QImage expected = actual.copy();
            QPainter painter(&actual), reference(&expected);
            painter.scale(zoom, zoom);
            reference.scale(zoom, zoom);
            QTransform transform = painter.transform();
            REQUIRE(nsqt_layout_plot(&painter, &style, 10, 40, text.constData(), text.size()) == NSERROR_OK);
            REQUIRE(painter.transform() == transform);
            reference.setFont(font);
            reference.translate(10, 40);
            reference.scale(scale, scale);
            reference.drawText(0, 0, QString::fromUtf8(text));
            painter.end();
            reference.end();
            REQUIRE(actual == expected);
        }
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    REQUIRE(argc == 2);
    REQUIRE(nsoption_init(nullptr, nullptr, nullptr) == NSERROR_OK);
    int id = QFontDatabase::addApplicationFont(QString::fromLocal8Bit(argv[1]));
    REQUIRE(id >= 0);
    QString family = QFontDatabase::applicationFontFamilies(id).first();
    nsoption_set_charp(font_sans, strdup(family.toUtf8().constData()));
    for (qreal pixels : {9.5, 15.0, 15.5, 15.9, 16.0, 20.25}) {
        for (bool svg : {false, true}) {
            for (int spacing : {-1, 0, 2})
                check_font(family, pixels, svg, spacing);
        }
    }
    nsoption_finalise(nullptr, nullptr);
    std::puts("Fractional fonts: widths, splits, UTF-8 positions, metrics, spacing, painting, zoom and DPI passed.");
    return EXIT_SUCCESS;
}
