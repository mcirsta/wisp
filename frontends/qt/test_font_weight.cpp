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


#define REQUIRE(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); std::exit(EXIT_FAILURE); \
} } while (0)

struct Family {
    QByteArray alias;
    lwc_string *names[2] = {};
    plot_font_style_t style = {};

    explicit Family(const char *name, uint64_t font_set = 1) : alias(name)
    {
        REQUIRE(lwc_intern_string(alias.constData(), alias.size(), &names[0]) == lwc_error_ok);
        style.font_set = font_set;
        style.families = names;
        style.family = PLOT_FONT_FAMILY_SANS_SERIF;
        style.size = 12 * PLOT_STYLE_SCALE;
    }
    ~Family() { lwc_string_unref(names[0]); }

    void load(const QString &directory, const char *file, int weight, bool italic = false, uint32_t order = 0)
    {
        QFile source(directory + "/" + file);
        REQUIRE(source.open(QIODevice::ReadOnly));
        QByteArray data = source.readAll();
        font_variant_id id = {style.font_set, order, alias.data(), uint8_t(CSS_FONT_WEIGHT_100 + weight / 100 - 1),
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

    void check(int requested, int expected, bool italic = false, int width_class = 5)
    {
        for (int repeat = 0; repeat < 2; repeat++) {
            QFont selected = font(requested, italic);
            QRawFont raw = QRawFont::fromFont(selected);
            QByteArray os2 = raw.fontTable("OS/2");
            REQUIRE(os2.size() >= 8);
            // Generated advances identify the physical resource even when its
            // native weight metadata is normalized to a different CSS descriptor.
            QByteArray hmtx = raw.fontTable("hmtx");
            REQUIRE(hmtx.size() >= 2);
            int actual = (qFromBigEndian<quint16>(hmtx.constData()) - 400) * 2;
            if (actual != expected)
                std::fprintf(stderr, "%s %d italic=%d: expected %d, got %d (%s)\n", alias.constData(),
                    requested, italic, expected, actual, raw.styleName().toUtf8().constData());
            REQUIRE(actual == expected);
            REQUIRE(qFromBigEndian<quint16>(os2.constData() + 6) == width_class);
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
    // The default CSS stretch descriptor overrides the file's width metadata.
    sparse.check(800, 900, false, 3);
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
    REQUIRE(!QFontDatabase::families().contains("Wisp Normal Test"));
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
    QFile variable_source(directory + "/variable.ttf");
    REQUIRE(variable_source.open(QIODevice::ReadOnly));
    REQUIRE(QFontDatabase::addApplicationFontFromData(variable_source.readAll()) >= 0);
    QFont native("Wisp Variable Test", -1, 600);
    native.setPixelSize(16);
    REQUIRE(QFontMetrics(selected).horizontalAdvance("MMMM") == QFontMetrics(native).horizontalAdvance("MMMM"));

    // Installed fonts retain native width matching; web-face descriptors are
    // a separate path. The condensed native 900 must not replace normal 700.
    for (const char *file : {"weight-500.ttf", "weight-700.ttf", "condensed-900.ttf"})
        REQUIRE(QFontDatabase::addApplicationFont(directory + "/" + file) >= 0);
    Family installed("Wisp Weight Test");
    installed.check(800, 700);

    Family regular("Isolated Regular"), bold("Isolated Bold"), remapped("Remapped Descriptor");
    regular.load(directory, "normal-400.ttf", 400);
    bold.load(directory, "normal-700.ttf", 400);
    regular.check(400, 400);
    regular.check(700, 400); // The other CSS family's physical bold is ineligible.
    bold.check(400, 700); // CSS normal points to a physically bold resource.
    // Qt's missing-glyph fallback also sees the CSS request, not the physical
    // resource's original weight/style. Do not force native bold into QFont.
    REQUIRE(bold.font(400).weight() == QFont::Normal);
    remapped.load(directory, "weight-300.ttf", 700);
    remapped.load(directory, "weight-900.ttf", 400);
    remapped.check(700, 300);
    remapped.check(400, 900);

    Family swapped_style("Swapped CSS Styles");
    swapped_style.load(directory, "italic-500.ttf", 400);
    swapped_style.load(directory, "weight-700.ttf", 400, true);
    swapped_style.check(400, 500);
    swapped_style.check(400, 700, true);
    REQUIRE(!swapped_style.font(400).italic());
    REQUIRE(regular.font(400, true).italic()); // Synthesis uses the CSS descriptor.

    Family collision_a("Collision A"), collision_b("Collision B"), lower_case("collision a");
    collision_a.load(directory, "isolation-a.ttf", 400);
    const qreal a_width = QFontMetricsF(collision_a.font(400)).horizontalAdvance("MMMM");
    collision_b.load(directory, "isolation-b.ttf", 400);
    const qreal b_width = QFontMetricsF(collision_b.font(400)).horizontalAdvance("MMMM");
    REQUIRE(b_width > a_width);
    REQUIRE(QFontMetricsF(collision_a.font(400)).horizontalAdvance("MMMM") == a_width);
    REQUIRE(QFontMetricsF(lower_case.font(400)).horizontalAdvance("MMMM") == a_width);
    REQUIRE(QFont::substitutes(collision_a.alias).isEmpty());
    for (const char *format : {"isolation-a.woff", "isolation-a.woff2"}) {
        collision_a.load(directory, format, 400);
        REQUIRE(QFontMetricsF(collision_a.font(400)).horizontalAdvance("MMMM") == a_width);
        REQUIRE(QFontMetricsF(collision_b.font(400)).horizontalAdvance("MMMM") == b_width);
    }
    Family cff("CFF Resource");
    cff.load(directory, "isolation-cff.otf", 400);
    REQUIRE(QFontMetricsF(cff.font(400)).horizontalAdvance("MMMM") == a_width);
    // Two documents can bind the same CSS name and native names to different
    // bytes. Use identical family-array pointers to exercise the cache scope key.
    Family page_a("Shared Page Family", 10), page_b("Shared Page Family", 11);
    page_b.style.families = page_a.names;
    page_a.load(directory, "isolation-a.ttf", 400);
    page_b.load(directory, "isolation-b.ttf", 400);
    const QString private_a = page_a.font(400).family();
    for (int repeat = 0; repeat < 3; repeat++) {
        REQUIRE(QFontMetricsF(page_a.font(400)).horizontalAdvance("MMMM") == a_width);
        REQUIRE(QFontMetricsF(page_b.font(400)).horizontalAdvance("MMMM") == b_width);
        auto system_style = page_a.style;
        system_style.font_set = 0;
        REQUIRE(!nsqt_capture_font(&system_style).family().startsWith("WispWeb"));
    }
    nsqt_layout_table->release_font_set(10);
    REQUIRE(QFontDatabase::families().contains(private_a)); // The earlier Collision A binding still owns it.
    REQUIRE(!page_a.font(400).family().startsWith("WispWeb"));
    REQUIRE(QFontMetricsF(page_b.font(400)).horizontalAdvance("MMMM") == b_width);
    nsqt_layout_table->release_font_set(11);

    Family ordered("Out of order downloads", 12);
    ordered.load(directory, "isolation-b.ttf", 400, false, 1);
    ordered.load(directory, "isolation-a.ttf", 400, false, 0);
    REQUIRE(QFontMetricsF(ordered.font(400)).horizontalAdvance("MMMM") == b_width);
    nsqt_layout_table->release_font_set(12);
    nsoption_finalise(nullptr, nullptr);
    std::puts("Qt font weights: CSS fallback, exact weights, style/width, cache reload, painting and variable fonts passed.");
    return EXIT_SUCCESS;
}
