/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_QT_WEBFONT_H
#define WISP_QT_WEBFONT_H

#include <QByteArray>
#include <QFont>

/* QFont matches registered names, not a resource handle. Give each downloaded
 * face a private identity at that API boundary, without changing the CSS names.
 * The in-memory names and style metadata describe the CSS face, so Qt fallback
 * and synthesis use CSS style too. Glyphs, advances and shaping tables survive.
 * FreeType decodes containers; Qt shapes and paints. */
QByteArray nsqt_webfont_data(const QByteArray &source, const QByteArray &family, int weight, QFont::Style style);

#endif
