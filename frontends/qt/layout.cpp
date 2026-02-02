/*
 * Copyright 2023 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Implementation of netsurf layout operations for qt.
 */

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QWidget>
#include <stddef.h>

extern "C" {

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/nsoption.h"
#include "utils/utf8.h"
#include <neosurf/ns_inttypes.h>
#include "neosurf/layout.h"
#include "neosurf/plot_style.h"
}

#include "qt/layout.h"
#include "qt/misc.h"

/* Standard DPI assumption: CSS uses 96 DPI, points are 1/72 inch.
 * To convert points to pixels: pixels = points × (96/72) = points × 4/3
 */

/**
 * Get a top-level widget to use as QPaintDevice for font metrics.
 * Returns nullptr if no window exists yet (early startup).
 * Using an existing widget ensures font metrics match actual rendering.
 */
static QPaintDevice *get_metrics_device(void)
{
    auto widgets = QApplication::topLevelWidgets();
    if (!widgets.isEmpty()) {
        return widgets.first();
    }
    return nullptr;
}

/**
 * constructs a qfont from a nsfont
 *
 * First checks if any of the CSS font-family names are available,
 * then falls back to generic families if none match.
 */
static QFont *new_qfont_fstyle(const struct plot_font_style *fstyle)
{
    QFont *nfont;
    const char *family = NULL;
    bool italic = false;
    QFontDatabase fontdb;

    /* First, try to find a matching font from the CSS font-family list */
    if (fstyle->families != NULL) {
        lwc_string *const *families = fstyle->families;
        while (*families != NULL) {
            const char *family_name = lwc_string_data(*families);
            QString qfamily = QString::fromUtf8(family_name);

            /* Check if this font has a substitution registered
             * (for web fonts loaded via @font-face)
             */
            QStringList subs = QFont::substitutes(qfamily);
            if (!subs.isEmpty()) {
                family = family_name;
                NSLOG(netsurf, INFO, "Rendering text: web font '%s' -> '%s' (AVAILABLE)", family_name,
                    subs.first().toUtf8().constData());
                break;
            }

            /* Check if this font family is available in the system
             */
            /* Check if this font family is available in the system
             * We check strict existence first, then try to resolve aliases (e.g. Helvetica -> Noto Sans)
             */
            if (QFontDatabase::hasFamily(qfamily) || QFontDatabase::families().contains(qfamily, Qt::CaseInsensitive)) {
                family = family_name;
                NSLOG(netsurf, DEBUG, "Using CSS font-family: %s", family_name);
                break;
            }

            /* Special handling for Helvetica Neue which is often compact - prefer condensed variants */
            if (qfamily.compare("Helvetica Neue", Qt::CaseInsensitive) == 0) {
                if (QFontDatabase::hasFamily("DejaVu Sans Condensed")) {
                    family = "DejaVu Sans Condensed";
                    NSLOG(netsurf, DEBUG, "Using specific alias: 'Helvetica Neue' -> 'DejaVu Sans Condensed'");
                    break;
                }
            }

            /* Check if Qt/FontConfig maps this to a valid installed font (alias) */
            QFont aliasFont(qfamily);
            QFontInfo aliasInfo(aliasFont);
            if (QFontDatabase::hasFamily(aliasInfo.family())) {
                family = family_name;
                NSLOG(netsurf, INFO, "Using CSS font-family alias: '%s' -> '%s'", family_name,
                    aliasInfo.family().toUtf8().constData());
                break;
            }

            /* Font not found - log at INFO for web font debugging
             */
            NSLOG(netsurf, INFO, "Rendering text: font '%s' NOT FOUND, trying next", family_name);
            /* Mark that we had a font miss - repaint needed when
             * font loads */
            extern bool font_miss_occurred;
            font_miss_occurred = true;
            families++;
        }
    }

    /* Fall back to generic families if no specific font was found */
    if (family == NULL) {
        switch (fstyle->family) {
        case PLOT_FONT_FAMILY_SERIF:
            family = nsoption_charp(font_serif);
            break;

        case PLOT_FONT_FAMILY_MONOSPACE:
            family = nsoption_charp(font_mono);
            break;

        case PLOT_FONT_FAMILY_CURSIVE:
            family = nsoption_charp(font_cursive);
            break;

        case PLOT_FONT_FAMILY_FANTASY:
            family = nsoption_charp(font_fantasy);
            break;

        case PLOT_FONT_FAMILY_SANS_SERIF:
        default:
            family = nsoption_charp(font_sans);
            break;
        }
    }

    if (fstyle->flags & FONTF_ITALIC) {
        italic = true;
    }

    nfont = new QFont(family, -1, fstyle->weight, italic);

    /* Calculate pixel size from fstyle->size.
     * If FONTF_SIZE_PIXELS is set (SVG), size is already in pixels * PLOT_STYLE_SCALE.
     * Otherwise (HTML/CSS), size is in points and needs conversion.
     */
    int pixelSize;
    if (fstyle->flags & FONTF_SIZE_PIXELS) {
        /* SVG: size is pixels * PLOT_STYLE_SCALE, use directly */
        pixelSize = fstyle->size / PLOT_STYLE_SCALE;
    } else {
        /* HTML/CSS: size is points * PLOT_STYLE_SCALE, convert to pixels.
         * Standard conversion: pixels = points × (96/72) = points × 4/3
         * With PLOT_STYLE_SCALE: pixelSize = (fstyle->size × 4) / (PLOT_STYLE_SCALE × 3)
         */
        pixelSize = (fstyle->size * 4) / (PLOT_STYLE_SCALE * 3);
    }
    nfont->setPixelSize(pixelSize > 0 ? pixelSize : 1);


    if (fstyle->flags & FONTF_SMALLCAPS) {
        nfont->setCapitalization(QFont::SmallCaps);
    }

    return nfont;
}


#define PFCACHE_ENTRIES 16 /* number of cache slots */

/* cached font entry*/
struct pfcache_entry {
    struct plot_font_style style;
    QFont *qfont;
    unsigned int age;
    int hit;
};

/**
 * get a qt font object for a given netsurf font style
 *
 * This implents a trivial LRU cache for font entries
 */
static QFont *nsfont_style_to_font(const struct plot_font_style *fstyle)
{
    int idx;
    int oldest_idx = 0;
    static struct {
        unsigned int age;
        struct pfcache_entry entries[PFCACHE_ENTRIES];
    } pfcache;

    for (idx = 0; idx < PFCACHE_ENTRIES; idx++) {
        if ((pfcache.entries[idx].qfont != NULL) && (pfcache.entries[idx].style.family == fstyle->family) &&
            (pfcache.entries[idx].style.size == fstyle->size) &&
            (pfcache.entries[idx].style.weight == fstyle->weight) &&
            (pfcache.entries[idx].style.flags == fstyle->flags)) {
            /* found matching existing font */
            pfcache.entries[idx].hit++;
            pfcache.entries[idx].age = ++pfcache.age;
            return new QFont(*pfcache.entries[idx].qfont);
        }
        if (pfcache.entries[idx].age < pfcache.entries[oldest_idx].age) {
            oldest_idx = idx;
        }
    }

    /* no existing entry, replace oldest */

    NSLOG(netsurf, DEEPDEBUG, "evicting slot %d age %d after %d hits", oldest_idx, pfcache.entries[oldest_idx].age,
        pfcache.entries[oldest_idx].hit);

    if (pfcache.entries[oldest_idx].qfont != NULL) {
        delete pfcache.entries[oldest_idx].qfont;
    }

    pfcache.entries[oldest_idx].qfont = new_qfont_fstyle(fstyle);

    pfcache.entries[oldest_idx].style.family = fstyle->family;
    pfcache.entries[oldest_idx].style.size = fstyle->size;
    pfcache.entries[oldest_idx].style.weight = fstyle->weight;
    pfcache.entries[oldest_idx].style.flags = fstyle->flags;

    pfcache.entries[oldest_idx].hit = 0;
    pfcache.entries[oldest_idx].age = ++pfcache.age;

    return new QFont(*pfcache.entries[oldest_idx].qfont);
}

/**
 * Find the position in a string where an x coordinate falls.
 *
 * \param[in] metrics qt font metrics to measure with.
 * \param[in] string UTF-8 string to measure
 * \param[in] length length of string, in bytes
 * \param[in] x coordinate to search for
 * \param[out] string_idx updated to offset in string of actual_x, [0..length]
 * \param[out] actual_x updated to x coordinate of character closest to x or
 * full length if string_idx is 0
 * \return NSERROR_OK and string_idx and actual_x updated or appropriate error
 * code on faliure
 */
static nserror
layout_position(QFontMetrics &metrics, const char *string, size_t length, int x, size_t *string_idx, int *actual_x)
{
    int full_x;
    int measured_x;
    size_t str_len;

    /* deal with empty string */
    if (length == 0) {
        *string_idx = 0;
        *actual_x = 0;
        return NSERROR_OK;
    }

    /* deal with negative or zero available width  */
    if (x <= 0) {
        *string_idx = 0;
        /* Use QString for correct UTF-8 byte interpretation */
        *actual_x = metrics.horizontalAdvance(QString::fromUtf8(string, length));
        return NSERROR_OK;
    }

    /* Use QString for proper UTF-8 handling */
    QString qstr = QString::fromUtf8(string, length);
    int char_count = qstr.length();

    /* if it is assumed each character of the string will occupy more than a
     * pixel then do not attempt measure excessively long strings
     */
    int chars_to_measure = (x > 0 && char_count > x) ? x : char_count;

    /* Convert character count back to byte position */
    str_len = qstr.left(chars_to_measure).toUtf8().length();

    full_x = metrics.horizontalAdvance(qstr.left(chars_to_measure));
    if (full_x < x) {
        /* whole string fits */
        *string_idx = length;
        *actual_x = full_x;
        return NSERROR_OK;
    }

    /* Start with an estimate based on character count */
    int current_chars = chars_to_measure * x / (full_x > 0 ? full_x : 1);
    if (current_chars < 1)
        current_chars = 1;
    if (current_chars > char_count)
        current_chars = char_count;

    str_len = qstr.left(current_chars).toUtf8().length();
    measured_x = metrics.horizontalAdvance(qstr.left(current_chars));

    if (measured_x == 0) {
        *string_idx = 0;
        *actual_x = full_x;
        return NSERROR_OK;
    }

    if (measured_x >= x) {
        /* too long try fewer chars - step back by UTF-8 characters */
        while (measured_x >= x && current_chars > 0) {
            current_chars--;
            if (current_chars == 0) {
                /* cannot fit a single character */
                measured_x = full_x;
                break;
            }
            str_len = qstr.left(current_chars).toUtf8().length();
            measured_x = metrics.horizontalAdvance(qstr.left(current_chars));
        }
    } else {
        /* too short try more chars until overflowing */
        int n_measured_x = measured_x;
        while (n_measured_x < x && current_chars < char_count) {
            size_t n_str_len = qstr.left(current_chars + 1).toUtf8().length();
            n_measured_x = metrics.horizontalAdvance(qstr.left(current_chars + 1));
            if (n_measured_x < x) {
                measured_x = n_measured_x;
                current_chars++;
                str_len = n_str_len;
            }
        }
    }

    *string_idx = str_len;
    *actual_x = measured_x;

    return NSERROR_OK;
}


/**
 * Measure the width of a string.
 *
 * \param[in] fstyle plot style for this text
 * \param[in] string UTF-8 string to measure
 * \param[in] length length of string, in bytes
 * \param[out] width updated to width of string[0..length)
 * \return NSERROR_OK and width updated or appropriate error
 *          code on faliure
 */
static nserror nsqt_layout_width(const struct plot_font_style *fstyle, const char *string, size_t length, int *width)
{
    QFont *font = nsfont_style_to_font(fstyle);
    /* Use top-level widget as device for accurate metrics matching rendering */
    QPaintDevice *device = get_metrics_device();
    QFontMetrics metrics = device ? QFontMetrics(*font, device) : QFontMetrics(*font);
    /* IMPORTANT: Use QString::fromUtf8 to correctly interpret byte length as UTF-8.
     * Qt's horizontalAdvance(char*, len) treats len as CHARACTER count, not byte count! */
    QString qstr = QString::fromUtf8(string, length);
    *width = metrics.horizontalAdvance(qstr);

    delete font;
    NSLOG(netsurf, DEEPDEBUG, "fstyle: %p string:\"%.*s\", length: %" PRIsizet ", width: %dpx", fstyle, (int)length,
        string, length, *width);
    return NSERROR_OK;
}


/**
 * Find the position in a string where an x coordinate falls.
 *
 * \param[in] fstyle style for this text
 * \param[in] string UTF-8 string to measure
 * \param[in] length length of string, in bytes
 * \param[in] x coordinate to search for
 * \param[out] string_idx updated to offset in string of actual_x, [0..length]
 * \param[out] actual_x updated to x coordinate of character closest to x
 * \return NSERROR_OK and string_idx and actual_x updated or appropriate error
 * code on faliure
 */
static nserror nsqt_layout_position(
    const struct plot_font_style *fstyle, const char *string, size_t length, int x, size_t *string_idx, int *actual_x)
{
    QFont *font = nsfont_style_to_font(fstyle);
    /* Use top-level widget as device for accurate metrics matching rendering */
    QPaintDevice *device = get_metrics_device();
    QFontMetrics metrics = device ? QFontMetrics(*font, device) : QFontMetrics(*font);
    nserror res;

    res = layout_position(metrics, string, length, x, string_idx, actual_x);

    delete font;

    NSLOG(netsurf, DEEPDEBUG,
        "fstyle: %p string:\"%.*s\", length: %" PRIsizet ", "
        "search_x: %dpx, offset: %" PRIsizet ", actual_x: %dpx",
        fstyle, (int)*string_idx, string, length, x, *string_idx, *actual_x);
    return res;
}


/**
 * Find where to split a string to make it fit a width.
 *
 * \param[in] fstyle style for this text
 * \param[in] string UTF-8 string to measure
 * \param[in] length length of string, in bytes
 * \param[in] split width to split on
 * \param[out] string_idx updated to index in string of actual_x, [1..length]
 * \param[out] actual_x updated to x coordinate of character closest to x
 * \return NSERROR_OK or appropriate error code on faliure
 *
 * On exit, string_idx indicates first character after split point.
 *
 * \note string_idx of 0 must never be returned.
 *
 *   Returns:
 *     string_idx giving split point closest to x, where actual_x < x
 *   else
 *     string_idx giving split point closest to x, where actual_x >= x
 *
 * Returning string_idx == length means no split possible
 */
static nserror nsqt_layout_split(const struct plot_font_style *fstyle, const char *string, size_t length, int split,
    size_t *string_idx, int *actual_x)
{
    nserror res;
    QFont *font = nsfont_style_to_font(fstyle);
    /* Use top-level widget as device for accurate metrics matching rendering */
    QPaintDevice *device = get_metrics_device();
    QFontMetrics metrics = device ? QFontMetrics(*font, device) : QFontMetrics(*font);
    size_t split_len;
    int split_x;
    size_t str_len;
    int full_width = metrics.horizontalAdvance(QString::fromUtf8(string, length));

    res = layout_position(metrics, string, length, split, &split_len, &split_x);
    if (res != NSERROR_OK) {
        delete font;
        return res;
    }

    if ((split_len < 1) || (split_len >= length)) {
        *string_idx = length;
        *actual_x = split_x;
        goto nsqt_layout_split_done;
    }

    if (string[split_len] == ' ') {
        /* string broke on boundary - but check if next word also fits */
        size_t next_space = split_len + 1;
        while ((next_space < length) && (string[next_space] != ' ')) {
            next_space++;
        }
        /* include the space after next word if it exists */
        if (next_space < length && string[next_space] == ' ') {
            next_space++;
        }
        int next_width = metrics.horizontalAdvance(QString::fromUtf8(string, next_space));
        if (next_width <= split) {
            /* next word also fits! use it */
            *string_idx = next_space;
            *actual_x = next_width;
        } else {
            *string_idx = split_len;
            *actual_x = split_x;
        }
        goto nsqt_layout_split_done;
    }

    /* attempt to break string */
    str_len = split_len;

    /* walk backwards through string looking for space to break on */
    while ((string[str_len] != ' ') && (str_len > 0)) {
        str_len--;
    }

    /* walk forwards through string looking for space if back failed */
    if (str_len == 0) {
        str_len = split_len;
        while ((str_len < length) && (string[str_len] != ' ')) {
            str_len++;
        }
    }
    /* include breaking character in match */
    if ((str_len < length) && (string[str_len] == ' ')) {
        str_len++;
    }

    /* After finding space, GREEDILY try to include more words that fit */
    while (str_len < length) {
        size_t word_end = str_len;
        /* Find the end of the next word (before any trailing space) */
        while ((word_end < length) && (string[word_end] != ' ')) {
            word_end++;
        }

        /* Measure width WITHOUT the trailing space (space can be trimmed at line end)
         * IMPORTANT: Use QString::fromUtf8 to correctly interpret byte length as UTF-8 */
        QString qstr = QString::fromUtf8(string, word_end);
        int word_width = metrics.horizontalAdvance(qstr);

        /* Calculate position including space for when we accept the word */
        size_t next_pos = word_end;
        if (next_pos < length && string[next_pos] == ' ') {
            next_pos++;
        }

        if (word_width <= split) {
            /* This word fits! Include it (with trailing space) and try the next one */
            str_len = next_pos;
        } else {
            /* This word doesn't fit, stop here */
            break;
        }
    }

    *string_idx = str_len;
    *actual_x = metrics.horizontalAdvance(QString::fromUtf8(string, str_len));

nsqt_layout_split_done:
    delete font;
    NSLOG(netsurf, DEEPDEBUG,
        "fstyle: %p string:\"%.*s\", length: %" PRIsizet ", "
        "split: %dpx, offset: %" PRIsizet ", actual_x: %dpx",
        fstyle, (int)*string_idx, string, length, split, *string_idx, *actual_x);

    return res;
}


/* exported interface documented in qt/layout.h */
nserror
nsqt_layout_plot(QPainter *painter, const struct plot_font_style *fstyle, int x, int y, const char *text, size_t length)
{
    QColor strokecolour(
        fstyle->foreground & 0xFF, (fstyle->foreground & 0xFF00) >> 8, (fstyle->foreground & 0xFF0000) >> 16);
    QPen pen(strokecolour);
    QFont *font = nsfont_style_to_font(fstyle);

    NSLOG(netsurf, DEEPDEBUG, "fstyle: %p string:\"%.*s\", length: %" PRIsizet ", width: %dpx", fstyle, (int)length,
        text, length, QFontMetrics(*font, painter->device()).horizontalAdvance(text, length));

    painter->setPen(pen);
    painter->setFont(*font);

    painter->drawText(x, y, QString::fromUtf8(text, length));

    delete font;
    return NSERROR_OK;
}


/**
 * Flag to track if a font repaint is already scheduled.
 * This coalesces multiple font loads into a single repaint.
 */
static bool font_repaint_pending = false;

/**
 * Flag to track if any font lookup failed (text rendered with fallback).
 * Repaint is only needed if this is true.
 */
bool font_miss_occurred = false;

/**
 * Callback triggered after font loads to repaint all windows (FOUT strategy).
 */
static void font_repaint_callback(void *p)
{
    (void)p;
    font_repaint_pending = false;
    font_miss_occurred = false;
    NSLOG(netsurf, INFO, "Font loaded, triggering global repaint");
    for (QWidget *w : QApplication::topLevelWidgets()) {
        w->update();
    }
}

/**
 * Load font data into Qt's font database.
 *
 * This function is called from the core when a web font is downloaded
 * via gui_layout_table.load_font_data.
 */
static nserror nsqt_load_font_data(const char *family_name, const uint8_t *data, size_t size)
{
    QByteArray fontData(reinterpret_cast<const char *>(data), size);

    int fontId = QFontDatabase::addApplicationFontFromData(fontData);
    if (fontId == -1) {
        NSLOG(netsurf, WARNING, "Failed to load font '%s' into Qt", family_name);
        return NSERROR_INVALID;
    }

    QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    NSLOG(netsurf, INFO, "Loaded font '%s' into Qt as font ID %d (families: %s)", family_name, fontId,
        families.join(", ").toUtf8().constData());

    /* Register CSS→Qt font name substitution using Qt's built-in system */
    if (!families.isEmpty()) {
        QString cssName = QString::fromUtf8(family_name);
        QString qtName = families.first();

        /* Tell Qt: when asked for CSS name, use Qt name instead */
        QFont::insertSubstitution(cssName, qtName);
        NSLOG(netsurf, INFO, "Registered font substitution: '%s' -> '%s'", family_name, qtName.toUtf8().constData());

        /* FOUT: Schedule a global repaint so text re-renders with new
         * font. Only schedule if:
         * 1. A font miss occurred (text was rendered with fallback)
         * 2. Not already pending (to coalesce multiple font loads)
         */
        if (font_miss_occurred && !font_repaint_pending) {
            font_repaint_pending = true;
            nsqt_schedule(100, font_repaint_callback, NULL);
            NSLOG(netsurf, INFO, "Font miss detected, scheduling repaint");
        } else if (!font_miss_occurred) {
            NSLOG(netsurf, INFO, "No font miss - skipping repaint");
        }
    }

    return NSERROR_OK;
}


static struct gui_layout_table layout_table = {
    .width = nsqt_layout_width,
    .position = nsqt_layout_position,
    .split = nsqt_layout_split,
    .load_font_data = nsqt_load_font_data,
};

struct gui_layout_table *nsqt_layout_table = &layout_table;
