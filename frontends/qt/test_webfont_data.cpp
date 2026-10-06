/* SPDX-License-Identifier: GPL-2.0-only */
#include <QApplication>
#include <QFile>
#include <QHash>
#include <QFontDatabase>
#include <QRawFont>
#include <QPainterPath>
#include <QtEndian>
#include <cstdio>
#include <cstdlib>
#include "webfont.h"

#define REQUIRE(expr)                                                                                                  \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr);                                                    \
            std::exit(EXIT_FAILURE);                                                                                   \
        }                                                                                                              \
    } while (0)

static QHash<quint16, QString> windows_names(const QRawFont &font)
{
    const QByteArray names = font.fontTable("name");
    const int strings = qFromBigEndian<quint16>(names.constData() + 4);
    QHash<quint16, QString> result;
    for (int i = 0; i < qFromBigEndian<quint16>(names.constData() + 2); ++i) {
        const char *record = names.constData() + 6 + i * 12;
        if (qFromBigEndian<quint16>(record) != 3)
            continue;
        int length = qFromBigEndian<quint16>(record + 8);
        int offset = strings + qFromBigEndian<quint16>(record + 10);
        REQUIRE(offset + length <= names.size());
        QString value;
        for (int c = 0; c < length; c += 2)
            value.append(QChar(qFromBigEndian<quint16>(names.constData() + offset + c)));
        result.insert(qFromBigEndian<quint16>(record + 6), value);
    }
    return result;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    REQUIRE(argc == 2);
    const QString directory = QString::fromLocal8Bit(argv[1]);
    for (const char *file : {"normal-400.ttf", "normal-700.ttf", "italic-500.ttf", "variable.ttf", "isolation-a.woff",
             "isolation-a.woff2", "isolation-cff.otf"}) {
        std::fprintf(stderr, "font container: %s\n", file);
        QFile input(directory + "/" + file);
        REQUIRE(input.open(QIODevice::ReadOnly));
        const QByteArray source = input.readAll();
        QRawFont original(source, 16);
        REQUIRE(original.isValid());
        const QByteArray os2 = original.fontTable("OS/2");
        REQUIRE(os2.size() >= 8);
        const int weight = qFromBigEndian<quint16>(os2.constData() + 4);
        const QByteArray output = nsqt_webfont_data(source, "WispPrivateFace", weight, original.style());
        REQUIRE(!output.isEmpty());
        QRawFont isolated(output, 16);
        REQUIRE(original.isValid() && isolated.isValid());
        REQUIRE(isolated.familyName() == "WispPrivateFace");
        REQUIRE(original.style() == isolated.style());
        REQUIRE(original.weight() == isolated.weight());
        REQUIRE(original.ascent() == isolated.ascent());
        REQUIRE(original.descent() == isolated.descent());
        const auto before_names = windows_names(original), after_names = windows_names(isolated);
        for (auto it = before_names.cbegin(); it != before_names.cend(); ++it) {
            if (it.key() >= 256) {
                if (it->startsWith("WispVariableTest-"))
                    REQUIRE(after_names.value(it.key()).startsWith("WispPrivateFace-"));
                else
                    REQUIRE(after_names.value(it.key()) == *it); // Axis/style display names.
            }
        }
        const auto glyphs = original.glyphIndexesForString("A M m");
        REQUIRE(glyphs == isolated.glyphIndexesForString("A M m"));
        REQUIRE(original.advancesForGlyphIndexes(glyphs) == isolated.advancesForGlyphIndexes(glyphs));
        for (quint32 glyph : glyphs)
            REQUIRE(original.pathForGlyph(glyph) == isolated.pathForGlyph(glyph));

        // Verify every retained font table, not a hand-picked list. Only the
        // names and head checksum are allowed to change. Directory offsets and
        // checksums must describe a complete valid SFNT on every input format.
        quint32 sum = 0;
        REQUIRE(output.size() % 4 == 0);
        for (qsizetype i = 0; i < output.size(); i += 4)
            sum += qFromBigEndian<quint32>(output.constData() + i);
        REQUIRE(sum == 0xb1b0afba);
        const int count = qFromBigEndian<quint16>(output.constData() + 4);
        for (int i = 0; i < count; ++i) {
            const char *record = output.constData() + 12 + 16 * i;
            QByteArray tag(record, 4);
            auto before = original.fontTable(tag.constData());
            auto after = isolated.fontTable(tag.constData());
            if (tag == "head") {
                before.replace(8, 4, QByteArray(4, '\0'));
                after.replace(8, 4, QByteArray(4, '\0'));
            }
            if (tag != "name")
                REQUIRE(before == after);
        }
        int id = QFontDatabase::addApplicationFontFromData(output);
        REQUIRE(id >= 0);
        REQUIRE(QFontDatabase::applicationFontFamilies(id).contains("WispPrivateFace"));
        REQUIRE(QFontDatabase::removeApplicationFont(id));
        for (int length : {0, 1, 4, 12, 24})
            REQUIRE(nsqt_webfont_data(source.left(length), "WispInvalidFace", 400, QFont::StyleNormal).isEmpty());
    }
    REQUIRE(nsqt_webfont_data(QByteArray(100, '\xff'), "WispInvalidFace", 400, QFont::StyleNormal).isEmpty());
    std::puts("Web-font containers: identities isolated; outlines, metrics, styles and tables preserved.");
    return EXIT_SUCCESS;
}
