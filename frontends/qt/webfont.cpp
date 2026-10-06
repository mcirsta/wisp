/* SPDX-License-Identifier: GPL-2.0-only */
#include "webfont.h"

#include <QMap>
#include <QSet>
#include <QtEndian>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

namespace
{
constexpr qsizetype max_font_bytes = 64 * 1024 * 1024;

quint16 read16(const QByteArray &data, qsizetype offset)
{
    return qFromBigEndian<quint16>(data.constData() + offset);
}

void write16(QByteArray &data, qsizetype offset, quint16 value)
{
    qToBigEndian(value, data.data() + offset);
}

void write32(QByteArray &data, qsizetype offset, quint32 value)
{
    qToBigEndian(value, data.data() + offset);
}

/* Named variable instances may carry their own PostScript name IDs. These are
 * identities too; axis and display-style names are deliberately left alone. */
bool instance_names(const QByteArray &variations, QSet<quint16> &names)
{
    if (variations.isEmpty())
        return true;
    if (variations.size() < 16)
        return false;
    const quint64 axes = read16(variations, 8), axis_size = read16(variations, 10);
    const quint64 count = read16(variations, 12), size = read16(variations, 14);
    const quint64 start = read16(variations, 4) + axes * axis_size;
    const quint64 ps_offset = 4 + axes * 4;
    if (size < ps_offset || start + count * size > quint64(variations.size()))
        return false;
    if (size >= ps_offset + 2) {
        for (quint64 i = 0; i < count; ++i) {
            quint16 id = read16(variations, start + i * size + ps_offset);
            if (id != 6 && id != 0xffff && (id < 256 || id > 32767))
                return false;
            if (id != 0xffff)
                names.insert(id);
        }
    }
    return true;
}

/* Change only identity records, including localized/typographic names and the
 * variable-font PostScript prefix. Keep license, axis and instance display names.
 * OpenType name records use offsets relative to stringOffset, also in format 1. */
bool rename_font(QByteArray &names, const QByteArray &family, const QSet<quint16> &instances)
{
    if (names.size() < 6 || read16(names, 0) > 1)
        return false;
    const int count = read16(names, 2), strings = read16(names, 4);
    if (6 + count * 12 > names.size() || strings < 6 + count * 12 || strings > names.size())
        return false;
    QByteArray unicode;
    for (char c : family) {
        unicode.append('\0');
        unicode.append(c);
    }
    const qsizetype latin_offset = names.size() - strings;
    const qsizetype unicode_offset = latin_offset + family.size();
    if (unicode_offset + unicode.size() > 65535)
        return false;
    names.append(family);
    names.append(unicode);
    for (int i = 0; i < count; ++i) {
        const int record = 6 + i * 12;
        switch (read16(names, record + 6)) {
        case 1:
        case 3:
        case 4:
        case 6:
        case 16:
        case 18:
        case 21:
        case 25:
            break;
        default:
            if (!instances.contains(read16(names, record + 6)))
                continue;
        }
        const int platform = read16(names, record), encoding = read16(names, record + 2);
        const bool utf16 = platform == 0 || platform == 3 || (platform == 2 && encoding == 1);
        if (instances.contains(read16(names, record + 6)) && read16(names, record + 6) != 6) {
            const QByteArray instance = family + "-" + QByteArray::number(read16(names, record + 6));
            if (instance.size() > 63)
                return false;
            QByteArray encoded;
            for (char c : instance) {
                if (utf16)
                    encoded.append('\0');
                encoded.append(c);
            }
            if (names.size() - strings + encoded.size() > 65535)
                return false;
            write16(names, record + 8, encoded.size());
            write16(names, record + 10, names.size() - strings);
            names.append(encoded);
        } else {
            write16(names, record + 8, utf16 ? unicode.size() : family.size());
            write16(names, record + 10, utf16 ? unicode_offset : latin_offset);
        }
    }
    return true;
}

quint32 checksum(const QByteArray &data)
{
    quint32 sum = 0;
    for (qsizetype i = 0; i < data.size(); i += 4) {
        quint32 word = 0;
        for (int byte = 0; byte < 4; ++byte)
            word = (word << 8) | (i + byte < data.size() ? quint8(data[i + byte]) : 0);
        sum += word;
    }
    return sum;
}

QByteArray isolated_face(FT_Face face, const QByteArray &family, int weight, QFont::Style style)
{
    FT_ULong count;
    if (!FT_IS_SFNT(face) || FT_Sfnt_Table_Info(face, 0, nullptr, &count) || count == 0 || count > 4095)
        return {};
    QMap<quint32, QByteArray> tables;
    qsizetype total = 12 + count * 16;
    for (FT_ULong i = 0; i < count; ++i) {
        FT_ULong tag, length;
        if (FT_Sfnt_Table_Info(face, i, &tag, &length) || length > FT_ULong(max_font_bytes - total))
            return {};
        // Renaming invalidates a digital signature; do not retain a false signature.
        if (tag == FT_MAKE_TAG('D', 'S', 'I', 'G'))
            continue;
        total += (length + 3) & ~FT_ULong(3);
        if (total > max_font_bytes)
            return {};
        QByteArray table(length, '\0');
        if (FT_Load_Sfnt_Table(face, tag, 0, reinterpret_cast<FT_Byte *>(table.data()), &length))
            return {};
        tables.insert(tag, table);
    }
    const auto name = FT_MAKE_TAG('n', 'a', 'm', 'e'), head = FT_MAKE_TAG('h', 'e', 'a', 'd');
    QSet<quint16> instances;
    if (!instance_names(tables.value(FT_MAKE_TAG('f', 'v', 'a', 'r')), instances) ||
        !rename_font(tables[name], family, instances) || tables[head].size() < 54)
        return {};
    // CSS descriptors override the resource's style metadata. Normalizing this
    // at registration lets QFont keep the CSS request for missing-glyph fallback
    // and synthetic styles, without changing the selected resource's outlines.
    QByteArray &os2 = tables[FT_MAKE_TAG('O', 'S', '/', '2')];
    if (os2.size() < 64)
        return {};
    const bool bold = weight >= 700, slanted = style != QFont::StyleNormal;
    write16(os2, 4, weight);
    quint16 selection = read16(os2, 62) & ~(1 | 32 | 64 | 512);
    selection |= (slanted ? 1 : 0) | (bold ? 32 : 0) | (!slanted && weight == 400 ? 64 : 0);
    if (style == QFont::StyleOblique && read16(os2, 0) >= 4)
        selection |= 512;
    write16(os2, 62, selection);
    write16(tables[head], 44, (read16(tables[head], 44) & ~3) | (bold ? 1 : 0) | (slanted ? 2 : 0));
    auto post = tables.find(FT_MAKE_TAG('p', 'o', 's', 't'));
    if (!slanted && post != tables.end() && post->size() >= 8)
        write32(*post, 4, 0);
    write32(tables[head], 8, 0);

    // Repack one SFNT face, retaining all other tables (including variations).
    // This also handles WOFF/WOFF2 and the first face of collections via FreeType.
    count = tables.size();
    QByteArray result(12 + count * 16, '\0');
    const bool cff = tables.contains(FT_MAKE_TAG('C', 'F', 'F', ' ')) ||
        tables.contains(FT_MAKE_TAG('C', 'F', 'F', '2'));
    write32(result, 0, cff ? FT_MAKE_TAG('O', 'T', 'T', 'O') : 0x00010000);
    write16(result, 4, count);
    int power = 1, selector = 0;
    while (power * 2 <= int(count)) {
        power *= 2;
        ++selector;
    }
    write16(result, 6, power * 16);
    write16(result, 8, selector);
    write16(result, 10, count * 16 - power * 16);
    int record = 12, head_offset = 0;
    for (auto it = tables.cbegin(); it != tables.cend(); ++it, record += 16) {
        if (result.size() + it->size() + 3 > max_font_bytes)
            return {};
        if (it.key() == head)
            head_offset = result.size();
        write32(result, record, it.key());
        write32(result, record + 4, checksum(*it));
        write32(result, record + 8, result.size());
        write32(result, record + 12, it->size());
        result.append(*it);
        while (result.size() % 4)
            result.append('\0');
    }
    write32(result, head_offset + 8, 0xb1b0afba - checksum(result));
    return result;
}
} // namespace

QByteArray nsqt_webfont_data(const QByteArray &source, const QByteArray &family, int weight, QFont::Style style)
{
    if (source.isEmpty() || source.size() > max_font_bytes || family.isEmpty() || family.size() > 63)
        return {};
    FT_Library library;
    if (FT_Init_FreeType(&library))
        return {};
    FT_Face face;
    QByteArray result;
    if (!FT_New_Memory_Face(library, reinterpret_cast<const FT_Byte *>(source.constData()), source.size(), 0, &face)) {
        result = isolated_face(face, family, weight, style);
        FT_Done_Face(face);
    }
    FT_Done_FreeType(library);
    return result;
}
