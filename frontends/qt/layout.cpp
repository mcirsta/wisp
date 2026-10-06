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
#include <QByteArray>
#include <QCache>
#include <QCryptographicHash>
#include <QFontDatabase>
#include <QPainter>
#include <QRawFont>
#include <QUuid>
#include <QTextBoundaryFinder>
#include <QWidget>
#include <QtEndian>
#include <QtMath>
#include <cstring>
#include <memory>
#include <stddef.h>
#include <utility>

#if QT_CONFIG(fontconfig)
#include <fontconfig/fontconfig.h>
#endif

extern "C" {

#include <libcss/properties.h>
#include "utils/errors.h"
#include "utils/log.h"
#include "utils/nsoption.h"
#include "utils/utf8.h"
#include <wisp/ns_inttypes.h>
#include "wisp/layout.h"
#include "wisp/plot_style.h"
#include "content/handlers/html/font_face.h"
}

#include "qt/layout.h"
#include "qt/webfont.h"

#ifdef WISP_QT_CAPTURE
#include "qt/capture_inspect.h"
#endif

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

/* QFont's pixel size is integral. Keep the residual scale in all text
 * operations, with CSS points converted at 96dpi and SVG already in pixels. */
static qreal font_pixel_size(const struct plot_font_style *style)
{
    if (style->size < 0)
        return 1.0;
    qreal pixels = qreal(style->size) / PLOT_STYLE_SCALE;
    return (style->flags & FONTF_SIZE_PIXELS) ? pixels : pixels * 4 / 3;
}

static qreal font_scale(const struct plot_font_style *style)
{
    qreal pixels = font_pixel_size(style);
    return pixels / qMax(1, int(pixels));
}

/* At zero size glyphs have no advance, but absolute letter spacing remains.
 * Count graphemes rather than UTF-8 bytes or UTF-16 code units. Qt cannot
 * represent a zero-pixel font or spacing divided by a zero residual scale. */
static qreal text_advance(const QFontMetricsF &metrics, qreal scale, int spacing, const QString &text)
{
    if (scale != 0)
        return metrics.horizontalAdvance(text) * scale;
    /* Negative spacing cannot give a zero-size run a negative used width. */
    if (spacing <= 0)
        return 0;

    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, text);
    int count = 0;
    while (boundaries.toNextBoundary() != -1)
        count++;
    return qreal(count) * spacing;
}

/* CSS Fonts matching order, rather than Qt's nearest-weight distance.
 * The first component orders search groups, the second weights within a group.
 * https://www.w3.org/TR/css-fonts-4/#font-style-matching */
static std::pair<int, int> font_weight_rank(int requested, int available)
{
    if (requested < 400)
        return available <= requested ? std::make_pair(0, requested - available) :
                                        std::make_pair(1, available - requested);
    if (requested > 500)
        return available >= requested ? std::make_pair(0, available - requested) :
                                        std::make_pair(1, requested - available);
    if (available >= requested && available <= 500)
        return {0, available - requested};
    if (available < requested)
        return {1, requested - available};
    return {2, available - 500};
}

/* Immutable native resources can be shared; the CSS bindings below cannot.
 * The weak index retains neither downloaded bytes nor unused native fonts. */
struct WebFontResource {
    QString family;
    int font_id;
    ~WebFontResource()
    {
        QFontDatabase::removeApplicationFont(font_id);
    }
};
static QHash<QByteArray, std::weak_ptr<const WebFontResource>> web_font_resources;

struct WebFontFace {
    std::shared_ptr<const WebFontResource> resource;
    int weight; // CSS descriptor, independent of the file's weight/style.
    uint8_t style;
    uint32_t order;
};

using WebFontFamilies = QHash<QString, QList<WebFontFace>>;
static QHash<uint64_t, WebFontFamilies> web_fonts;

static int font_style_rank(uint8_t requested, uint8_t available)
{
    if (requested == available)
        return 0;
    if (requested == CSS_FONT_STYLE_NORMAL)
        return available == CSS_FONT_STYLE_OBLIQUE ? 1 : 2;
    return available == CSS_FONT_STYLE_NORMAL ? 2 : 1;
}

/* CSS Fonts 3 4.2/4.4/5.2: match only the faces assigned to this CSS family,
 * using their descriptors, not the naming/style metadata in the downloaded file.
 * This runs on a QFont cache miss, not on each measurement or paint operation. */
static const WebFontFace *web_font_face(uint64_t font_set, const QString &family, int weight, uint8_t style)
{
    auto set = web_fonts.constFind(font_set);
    if (set == web_fonts.cend())
        return nullptr;
    auto found = set->constFind(family.toCaseFolded());
    if (found == set->cend())
        return nullptr;
    const WebFontFace *best = nullptr;
    std::pair<int, std::pair<int, int>> best_rank;
    for (const WebFontFace &face : *found) {
        const auto rank = std::make_pair(font_style_rank(style, face.style), font_weight_rank(weight, face.weight));
        if (best == nullptr || rank < best_rank || (rank == best_rank && face.order >= best->order)) {
            best = &face;
            best_rank = rank;
        }
    }
    return best;
}

/* Called only on a QFont cache miss, after pixel sizing. Keep Qt's family,
 * style and width choice; correct missing weights within that set of faces. */
static void match_font_weight(QFont &font)
{
    QRawFont current = QRawFont::fromFont(font);
    QByteArray os2 = current.fontTable("OS/2");
    if (os2.size() < 8)
        return;

    /* QFontInfo::weight() and QRawFont::weight() can both echo the request.
     * OS/2 contains the static face's actual weight and width class. */
    int requested = font.weight();
    int current_weight = qFromBigEndian<quint16>(os2.constData() + 4);
    int width_class = qFromBigEndian<quint16>(os2.constData() + 6);
    if (current_weight < 1 || current_weight > 1000 || width_class < 1 || width_class > 9 ||
        current_weight == requested || !current.fontTable("fvar").isEmpty())
        return; // Exact matches need no search; leave variable ranges to Qt.

    QString family = QFontInfo(font).family();
    QFont::Style face_style = QFontDatabase::font(family, current.styleName(), 12).style();
    int chosen = current_weight;
    auto best_rank = font_weight_rank(requested, chosen);
    for (const QString &name : QFontDatabase::styles(family)) {
        QFont candidate = QFontDatabase::font(family, name, 12);
        if (candidate.style() != face_style || font_weight_rank(requested, candidate.weight()) >= best_rank)
            continue;

        candidate.setPixelSize(font.pixelSize());
        QRawFont raw = QRawFont::fromFont(candidate);
        QByteArray candidate_os2 = raw.fontTable("OS/2");
        if (candidate_os2.size() < 8 || qFromBigEndian<quint16>(candidate_os2.constData() + 6) != width_class)
            continue;
        if (!raw.fontTable("fvar").isEmpty())
            return; // A named instance is not the complete set of variable weights.
        int weight = qFromBigEndian<quint16>(candidate_os2.constData() + 4);
        if (weight >= 1 && weight <= 1000 && font_weight_rank(requested, weight) < best_rank) {
            chosen = weight;
            best_rank = font_weight_rank(requested, weight);
        }
    }
    /* If the same physical face wins, retain the original request so Qt can
     * still synthesize bold/italic where the family has no such face. */
    if (chosen != current_weight)
        font.setWeight(QFont::Weight(chosen));
}

/* Resolve a platform family without accepting Qt's fallback for an unknown
 * name. The caller checks the CSS web-face registry before taking this path. */
static QString resolve_font_family(const QString &family, bool italic)
{
#if QT_CONFIG(fontconfig)
    /* hasFamily() also applies platform aliases, including weak fallbacks.
     * Check the actual names; Qt adds " [foundry]" for duplicate families. */
    const QStringList installed = QFontDatabase::families();
    auto is_installed = [&installed](const QString &name) {
        for (const QString &entry : installed) {
            if (entry.compare(name, Qt::CaseInsensitive) == 0)
                return true;
            int foundry = entry.lastIndexOf(" [");
            if (foundry >= 0 && entry.endsWith(']') &&
                QStringView(entry).left(foundry).compare(name, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    };
#else
    auto is_installed = [](const QString &name) { return QFontDatabase::hasFamily(name); };
#endif
    for (const QString &substitute : QFont::substitutes(family)) {
        if (is_installed(substitute))
            return substitute;
    }
    if (is_installed(family))
        return family;

#if QT_CONFIG(fontconfig)
    /* Fontconfig distinguishes strong aliases (e.g. Arial -> Liberation Sans)
     * from weak fallback lists appended even for a completely unknown family.
     * Only a strong, available alias may displace the next CSS family. */
    FcPattern *pattern = FcPatternCreate();
    if (pattern == nullptr)
        return {};
    QString resolved;
    QByteArray name = family.toUtf8();
    if (FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(name.constData())) &&
        FcPatternAddInteger(pattern, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN) &&
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern)) {
        FcValue value;
        FcValueBinding binding;
        for (int i = 0; FcPatternGetWithBinding(pattern, FC_FAMILY, i, &value, &binding) == FcResultMatch; ++i) {
            if (binding != FcValueBindingStrong || value.type != FcTypeString)
                continue;
            QString candidate = QString::fromUtf8(reinterpret_cast<const char *>(value.u.s));
            if (is_installed(candidate)) {
                resolved = candidate;
                break;
            }
        }
        /* Qt splits duplicate families by foundry and can pick an italic-only
         * foundry for an upright request. Preserve Fontconfig's style match
         * when qualifying such an alias; Qt still selects the font weight. */
        if (!resolved.isEmpty() && !installed.contains(resolved, Qt::CaseInsensitive)) {
            FcDefaultSubstitute(pattern);
            FcResult result;
            FcPattern *match = FcFontMatch(nullptr, pattern, &result);
            if (match != nullptr) {
                FcChar8 *matched_family, *foundry;
                if (FcPatternGetString(match, FC_FAMILY, 0, &matched_family) == FcResultMatch &&
                    resolved.compare(QString::fromUtf8(reinterpret_cast<const char *>(matched_family)),
                        Qt::CaseInsensitive) == 0 &&
                    FcPatternGetString(match, FC_FOUNDRY, 0, &foundry) == FcResultMatch) {
                    QString qualified =
                        resolved + " [" + QString::fromUtf8(reinterpret_cast<const char *>(foundry)) + "]";
                    if (installed.contains(qualified, Qt::CaseInsensitive))
                        resolved = qualified;
                }
                FcPatternDestroy(match);
            }
        }
    }
    FcPatternDestroy(pattern);
    return resolved;
#else
    (void)italic;
    return {};
#endif
}

/* Family resolution is independent of size and weight. Keep both successful
 * lookups and missing names, bounded separately from the complete font cache. */
static QCache<std::pair<QString, bool>, QString> font_family_cache(64);
static QMetaObject::Connection font_database_connection;
static void nsqt_font_cache_clear(void);
static void nsqt_font_cache_fini(void);

static QString available_font_family(const QString &family, bool italic)
{
    const auto key = std::make_pair(family, italic);
    if (const QString *cached = font_family_cache.object(key))
        return *cached;
    QString resolved = resolve_font_family(family, italic);
    font_family_cache.insert(key, new QString(resolved));
    return resolved;
}

/* Try CSS families in order, then the configured generic family. */
static QFont new_qfont_fstyle(const struct plot_font_style *fstyle)
{
    QString family;
    const uint8_t style = fstyle->flags & FONTF_ITALIC ? CSS_FONT_STYLE_ITALIC
        : fstyle->flags & FONTF_OBLIQUE                ? CSS_FONT_STYLE_OBLIQUE
                                                       : CSS_FONT_STYLE_NORMAL;
    bool italic = style != CSS_FONT_STYLE_NORMAL;
    const WebFontFace *face = nullptr;

    /* First, try to find a matching font from the CSS font-family list */
    if (fstyle->families != NULL) {
        lwc_string *const *families = fstyle->families;
        while (*families != NULL) {
            const char *family_name = lwc_string_data(*families);
            const QString css_family = QString::fromUtf8(family_name);
            face = web_font_face(fstyle->font_set, css_family, fstyle->weight, style);
            family = face ? face->resource->family : available_font_family(css_family, italic);
            if (!family.isEmpty()) {
                NSLOG(wisp, DEBUG, "Using CSS font-family: '%s' -> '%s'", family_name, qPrintable(family));
                break;
            }

            /* Font not found - log at INFO for web font debugging
             */
            NSLOG(wisp, INFO, "Rendering text: font '%s' NOT FOUND, trying next", family_name);
            families++;
        }
    }

    /* Fall back to generic families if no specific font was found */
    if (family.isEmpty()) {
        switch (fstyle->family) {
        case PLOT_FONT_FAMILY_SERIF:
            family = QString::fromUtf8(nsoption_charp(font_serif));
            break;

        case PLOT_FONT_FAMILY_MONOSPACE:
            family = QString::fromUtf8(nsoption_charp(font_mono));
            break;

        case PLOT_FONT_FAMILY_CURSIVE:
            family = QString::fromUtf8(nsoption_charp(font_cursive));
            break;

        case PLOT_FONT_FAMILY_FANTASY:
            family = QString::fromUtf8(nsoption_charp(font_fantasy));
            break;

        case PLOT_FONT_FAMILY_SANS_SERIF:
        default:
            family = QString::fromUtf8(nsoption_charp(font_sans));
            break;
        }
    }

    QFont nfont(family, -1, fstyle->weight, italic);

    nfont.setPixelSize(qMax(1, int(font_pixel_size(fstyle))));
    if (face == nullptr) {
        match_font_weight(nfont);
    }

    if (fstyle->flags & FONTF_SMALLCAPS) {
        nfont.setCapitalization(QFont::SmallCaps);
    }

    /* The painter scales glyphs, but absolute CSS letter spacing stays in pixels. */
    if (fstyle->letter_spacing != 0 && fstyle->size != 0) {
        nfont.setLetterSpacing(QFont::AbsoluteSpacing, qreal(fstyle->letter_spacing) / font_scale(fstyle));
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

static struct {
    unsigned int age;
    struct pfcache_entry entries[PFCACHE_ENTRIES];
} pfcache;

/* One LRU on the Qt GUI thread, shared across pages and layout passes. Store final
 * callback results, leaving Qt's shaping and our fitting/rounding unchanged.
 * The cost includes owned UTF-8 and an allowance for key/hash/font bookkeeping;
 * it is a memory budget estimate, not an exact allocator footprint. Very long
 * runs bypass the cache so one paragraph cannot displace many short labels. */
enum class TextOperation { Width, Split };
struct TextMeasurementKey {
    QFont font;
    QByteArray text;
    qreal dpi;
    int size, flags, spacing, available;
    TextOperation operation;

    bool operator==(const TextMeasurementKey &other) const
    {
        return font == other.font && text == other.text && dpi == other.dpi && size == other.size &&
            flags == other.flags && spacing == other.spacing && available == other.available &&
            operation == other.operation;
    }
};

static size_t qHash(const TextMeasurementKey &key, size_t seed = 0)
{
    return qHashMulti(
        seed, key.font, key.text, key.dpi, key.size, key.flags, key.spacing, key.available, int(key.operation));
}

struct TextMeasurementResult {
    size_t offset;
    int width;
};
static QCache<TextMeasurementKey, TextMeasurementResult> text_cache(512 * 1024);
static quint64 text_cache_generation;
#ifdef WISP_QT_CAPTURE
static quint64 text_cache_hits[2], text_cache_misses[2];
#endif

class TextMeasurement
{
public:
    TextMeasurement(const QFont &font, const QFontMetricsF &metrics, const plot_font_style *style, const char *text,
        size_t length, TextOperation operation, int available, quint64 generation)
        : key{font, {}, metrics.fontDpi(), style->size, int(style->flags), style->letter_spacing, available, operation},
          generation(generation), eligible(length <= 4096 && text_cache.maxCost() != 0)
    {
        /* Borrow for lookup only. Insertion below must copy the bytes: a box's
         * text may change or disappear before the next layout callback. */
        if (eligible)
            key.text = QByteArray::fromRawData(text, qsizetype(length));
    }

    bool find(int *width, size_t *offset = nullptr) const
    {
        const auto *result = eligible ? text_cache.object(key) : nullptr;
#ifdef WISP_QT_CAPTURE
        (result ? text_cache_hits : text_cache_misses)[int(key.operation)]++;
#endif
        if (!result)
            return false;
        *width = result->width;
        if (offset)
            *offset = result->offset;
        return true;
    }

    void insert(int width, size_t offset = 0)
    {
        /* Lazy font resolution can synchronously invalidate the font cache. */
        if (!eligible || generation != text_cache_generation)
            return;
        qsizetype cost = sizeof(TextMeasurementKey) + sizeof(TextMeasurementResult) + 128 + key.text.size() + 1;
        if (cost > text_cache.maxCost())
            return;
        key.text = QByteArray(key.text.constData(), key.text.size());
        text_cache.insert(key, new TextMeasurementResult{offset, width}, cost);
    }

private:
    TextMeasurementKey key;
    quint64 generation;
    bool eligible;
};

static void nsqt_font_cache_init()
{
    // Generic-only pages must also invalidate when installed fonts change.
    if (!font_database_connection) {
        /* Release our QFonts while Qt's font backend still exists. Static
         * caches otherwise retain shared font engines past QApplication. */
        qAddPostRoutine(nsqt_font_cache_fini);
        font_database_connection =
            QObject::connect(qApp, &QGuiApplication::fontDatabaseChanged, qApp, nsqt_font_cache_clear);
    }
}

/**
 * get a qt font object for a given netsurf font style
 *
 * This implents a trivial LRU cache for font entries
 */
static QFont nsfont_style_to_font(const struct plot_font_style *fstyle)
{
    nsqt_font_cache_init();
    int idx;
    int oldest_idx = 0;

    for (idx = 0; idx < PFCACHE_ENTRIES; idx++) {
        if ((pfcache.entries[idx].qfont != NULL) && (pfcache.entries[idx].style.families == fstyle->families) &&
            (pfcache.entries[idx].style.font_set == fstyle->font_set) &&
            (pfcache.entries[idx].style.family == fstyle->family) &&
            (pfcache.entries[idx].style.size == fstyle->size) &&
            (pfcache.entries[idx].style.weight == fstyle->weight) &&
            (pfcache.entries[idx].style.flags == fstyle->flags) &&
            (pfcache.entries[idx].style.letter_spacing == fstyle->letter_spacing)) {
            /* found matching existing font */
            pfcache.entries[idx].hit++;
            pfcache.entries[idx].age = ++pfcache.age;

            QFont cached_font = *pfcache.entries[idx].qfont;
            NSLOG(wisp, DEEPDEBUG, "CACHE HIT - generic_family=%d (Resolved by Qt to: '%s')", fstyle->family,
                QFontInfo(cached_font).family().toUtf8().constData());
            return cached_font;
        }
        if (pfcache.entries[idx].age < pfcache.entries[oldest_idx].age) {
            oldest_idx = idx;
        }
    }

    /* no existing entry, replace oldest */

    NSLOG(wisp, DEEPDEBUG, "evicting slot %d age %d after %d hits", oldest_idx, pfcache.entries[oldest_idx].age,
        pfcache.entries[oldest_idx].hit);

    if (pfcache.entries[oldest_idx].qfont != NULL) {
        delete pfcache.entries[oldest_idx].qfont;
        /* A font-database notification may clear caches while resolving the replacement. */
        pfcache.entries[oldest_idx].qfont = NULL;
    }

    pfcache.entries[oldest_idx].qfont = new QFont(new_qfont_fstyle(fstyle));

    pfcache.entries[oldest_idx].style.families = fstyle->families;
    pfcache.entries[oldest_idx].style.font_set = fstyle->font_set;
    pfcache.entries[oldest_idx].style.family = fstyle->family;
    pfcache.entries[oldest_idx].style.size = fstyle->size;
    pfcache.entries[oldest_idx].style.weight = fstyle->weight;
    pfcache.entries[oldest_idx].style.flags = fstyle->flags;
    pfcache.entries[oldest_idx].style.letter_spacing = fstyle->letter_spacing;

    pfcache.entries[oldest_idx].hit = 0;
    pfcache.entries[oldest_idx].age = ++pfcache.age;

    QFont created_font = *pfcache.entries[oldest_idx].qfont;
    NSLOG(wisp, DEEPDEBUG, "CACHE MISS - Created fresh QFont, generic_family=%d (Resolved by Qt to: '%s')",
        fstyle->family, QFontInfo(created_font).family().toUtf8().constData());
    return created_font;
}

#ifdef WISP_QT_CAPTURE
qreal nsqt_capture_font_scale(const struct plot_font_style *style)
{
    return font_scale(style);
}

QFont nsqt_capture_font(const struct plot_font_style *style)
{
    // Use the same selection, substitutions and pixel sizing as text painting.
    return nsfont_style_to_font(style);
}

QJsonObject nsqt_capture_text_cache_stats()
{
    return {{"width_hits", qint64(text_cache_hits[0])}, {"width_misses", qint64(text_cache_misses[0])},
        {"split_hits", qint64(text_cache_hits[1])}, {"split_misses", qint64(text_cache_misses[1])},
        {"entries", text_cache.size()}, {"cost_bytes", text_cache.totalCost()}, {"budget_bytes", text_cache.maxCost()}};
}

void nsqt_capture_text_cache_limit(qsizetype bytes)
{
    text_cache.clear();
    text_cache_generation++;
    text_cache.setMaxCost(qMax(qsizetype(0), bytes));
    text_cache_hits[0] = text_cache_hits[1] = text_cache_misses[0] = text_cache_misses[1] = 0;
}
#endif

/**
 * Release frontend font ownership before Qt tears down its font backend.
 *
 * Also used for invalidation. Shutdown must not log or notify the HTML engine,
 * which NS_Application has already finalised before Qt calls post routines.
 */
static void nsqt_font_cache_release(void)
{
    text_cache.clear();
    text_cache_generation++;
    font_family_cache.clear();
    for (int i = 0; i < PFCACHE_ENTRIES; i++) {
        if (pfcache.entries[i].qfont != NULL) {
            delete pfcache.entries[i].qfont;
            pfcache.entries[i].qfont = NULL;
        }
        pfcache.entries[i].age = 0;
    }
    pfcache.age = 0;
}

static void nsqt_font_cache_clear(void)
{
    NSLOG(wisp, INFO, "Purging Qt font caches after a font database or alias change");
    nsqt_font_cache_release();
    html_font_face_notify_changed();
}

static void nsqt_font_cache_fini(void)
{
    // Removing Qt's application fonts during teardown must not notify the core.
    QObject::disconnect(font_database_connection);
    font_database_connection = {};
    nsqt_font_cache_release();
    web_fonts.clear();
    web_font_resources.clear();
}

/* A new CSS binding to an existing native resource changes only this document's
 * selection. Measurement keys already include the resolved QFont, so reusable
 * metrics/glyph engines and other documents' selections remain valid. */
static void nsqt_font_cache_clear_set(uint64_t font_set)
{
    for (auto &entry : pfcache.entries) {
        if (entry.qfont != nullptr && entry.style.font_set == font_set) {
            delete entry.qfont;
            entry.qfont = nullptr;
            entry.age = 0;
        }
    }
    html_font_face_notify_changed();
}

/* All requests are prefixes of the same string with unchanged font metrics,
 * scale and spacing. Length is therefore sufficient as a key. Keep the full
 * width and four recent prefixes for this callback only; no font/text lifetime
 * or invalidation state escapes it. Shape each uncached prefix independently,
 * just as before, so kerning and contextual shaping remain unchanged. */
struct PrefixWidths {
    const QFontMetricsF &metrics;
    qreal scale;
    int spacing;
    qsizetype full_length;
    struct Entry {
        qsizetype length = -1;
        qreal width = 0;
    } entries[5];
    unsigned next = 1;

    qreal measure(const QString &prefix)
    {
        for (const Entry &entry : entries) {
            if (entry.length == prefix.size())
                return entry.width;
        }
        qreal width = text_advance(metrics, scale, spacing, prefix);
        unsigned slot = prefix.size() == full_length ? 0 : next;
        entries[slot] = {prefix.size(), width};
        if (slot != 0)
            next = next == 4 ? 1 : next + 1;
        return width;
    }
};

/**
 * Find the position in a string where an x coordinate falls.
 *
 * \param[in] widths prefix measurements for this string and font.
 * \param[in] qstr decoded string to measure
 * \param[in] length length of string, in bytes
 * \param[in] x coordinate to search for
 * \param[out] string_idx updated to offset in string of actual_x, [0..length]
 * \param[out] actual_x updated to x coordinate of character closest to x or
 * full length if string_idx is 0
 * \return NSERROR_OK and string_idx and actual_x updated or appropriate error
 * code on faliure
 */
static nserror
layout_position(PrefixWidths &widths, const QString &qstr, size_t length, int x,
    size_t *string_idx, int *actual_x)
{
    qreal full_x;
    qreal measured_x;
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
        *actual_x = qRound(widths.measure(qstr));
        return NSERROR_OK;
    }

    int char_count = qstr.length();

    /* if it is assumed each character of the string will occupy more than a
     * pixel then do not attempt measure excessively long strings
     */
    int chars_to_measure = (x > 0 && char_count > x) ? x : char_count;

    /* Convert character count back to byte position */
    str_len = qstr.left(chars_to_measure).toUtf8().length();

    full_x = widths.measure(qstr.left(chars_to_measure));
    if (full_x < x) {
        /* whole string fits */
        *string_idx = length;
        *actual_x = qRound(full_x);
        return NSERROR_OK;
    }

    /* Start with an estimate based on character count */
    int current_chars = chars_to_measure * x / (full_x > 0 ? full_x : 1);
    if (current_chars < 1)
        current_chars = 1;
    if (current_chars > char_count)
        current_chars = char_count;

    str_len = qstr.left(current_chars).toUtf8().length();
    measured_x = widths.measure(qstr.left(current_chars));

    if (measured_x == 0) {
        *string_idx = 0;
        *actual_x = qRound(full_x);
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
            measured_x = widths.measure(qstr.left(current_chars));
        }
    } else {
        /* too short try more chars until overflowing */
        qreal n_measured_x = measured_x;
        while (n_measured_x < x && current_chars < char_count) {
            size_t n_str_len = qstr.left(current_chars + 1).toUtf8().length();
            n_measured_x = widths.measure(qstr.left(current_chars + 1));
            if (n_measured_x < x) {
                measured_x = n_measured_x;
                current_chars++;
                str_len = n_str_len;
            }
        }
    }

    *string_idx = str_len;
    *actual_x = qRound(measured_x);

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
    quint64 generation = text_cache_generation;
    QFont font = nsfont_style_to_font(fstyle);
    /* Use top-level widget as device for accurate metrics matching rendering */
    QPaintDevice *device = get_metrics_device();
    QFontMetricsF metrics(font, device);
    TextMeasurement cached(font, metrics, fstyle, string, length, TextOperation::Width, 0, generation);
    if (cached.find(width)) {
        return NSERROR_OK;
    }
    /* IMPORTANT: Use QString::fromUtf8 to correctly interpret byte length as UTF-8.
     * Qt's horizontalAdvance(char*, len) treats len as CHARACTER count, not byte count! */
    QString qstr = QString::fromUtf8(string, length);
    qreal advance = text_advance(metrics, font_scale(fstyle), fstyle->letter_spacing, qstr);
    /* The core skips splitting a box when its integer width fits. Round HTML
     * advances up so a fractional overflow cannot bypass that check. Collapsed
     * spaces are positioning advances between boxes, not text to fit: retain
     * their nearest-pixel gaps. SVG anchoring also uses the nearest pixel. */
    bool positioning = (fstyle->flags & FONTF_SIZE_PIXELS) || (length == 1 && string[0] == ' ');
    *width = positioning ? qRound(advance) : qCeil(advance);
    cached.insert(*width);

    NSLOG(wisp, DEEPDEBUG, "fstyle: %p string:\"%.*s\", length: %" PRIsizet ", width: %dpx", fstyle, (int)length,
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
    QFont font = nsfont_style_to_font(fstyle);
    /* Use top-level widget as device for accurate metrics matching rendering */
    QPaintDevice *device = get_metrics_device();
    QFontMetricsF metrics(font, device);
    nserror res;

    QString qstr = QString::fromUtf8(string, length);
    PrefixWidths widths{metrics, font_scale(fstyle), fstyle->letter_spacing, qstr.size()};
    res = layout_position(widths, qstr, length, x, string_idx, actual_x);


    NSLOG(wisp, DEEPDEBUG,
        "fstyle: %p string:\"%.*s\", length: %" PRIsizet ", "
        "search_x: %dpx, offset: %" PRIsizet ", actual_x: %dpx",
        fstyle, (int)*string_idx, string, length, x, *string_idx, *actual_x);
    return res;
}


/* Advance over count space-delimited words, reducing count if the text ends.
 * Break positions are UTF-8 byte offsets before the space; a terminal position
 * includes any trailing space, matching the whole-string width contract. */
static size_t advance_words(const char *string, size_t length, size_t start, size_t &count)
{
    for (size_t i = 0; i < count; i++) {
        const char *space = static_cast<const char *>(std::memchr(string + start, ' ', length - start));
        size_t end = space == nullptr ? length : size_t(space - string);
        if (end == length) {
            count = i + 1;
            return end;
        }
        if (i + 1 == count)
            return end;
        start = end + 1;
    }
    return length;
}

/* Find a fitting prefix without shaping the entire remaining paragraph for
 * every line. Grow the word-count bracket, then narrow it. Shape each candidate
 * as a complete string: adding individual word advances would lose kerning and
 * contextual shaping, and horizontalAdvance(full_text, length) still analyses
 * full_text. No decoded text or measurements outlive this callback. */
static void layout_split_words(const QFontMetricsF &metrics, qreal scale, int spacing,
    const char *string, size_t length, int split, size_t *string_idx, int *actual_x)
{
    auto measure = [&](size_t end) {
        return text_advance(metrics, scale, spacing, QString::fromUtf8(string, end));
    };
    size_t low_end = 0, low_words = 0, high_words = 0, step = 1;
    qreal low_width = 0;
    for (;;) {
        size_t count = step;
        size_t end = advance_words(string, length, low_words == 0 ? 0 : low_end + 1, count);
        qreal width = measure(end);
        high_words = low_words + count;
        if (width > split) {
            if (low_words == 0) {
                /* Even an overlong first word must make progress. */
                low_end = end;
                low_width = width;
            }
            break;
        }
        low_end = end;
        low_width = width;
        low_words = high_words;
        if (end == length)
            break;
        step *= 2;
    }
    while (high_words > low_words + 1) {
        size_t count = (high_words - low_words) / 2;
        size_t end = advance_words(string, length, low_end + 1, count);
        qreal width = measure(end);
        if (width <= split) {
            low_end = end;
            low_width = width;
            low_words += count;
        } else {
            high_words = low_words + count;
        }
    }
    *string_idx = low_end;
    *actual_x = qCeil(low_width);
}

/* Preserve position-based fitting for runs outside the word search's
 * assumptions, notably negative spacing: a later prefix may be narrower. */
static nserror layout_split_with_position(const QFontMetricsF &metrics, qreal scale, int spacing,
    const char *string, size_t length, int split, size_t *string_idx, int *actual_x)
{
    size_t split_len;
    int split_x;
    size_t str_len;
    size_t last_word_end = 0; /* Both offset and width exclude the breaking space. */
    QString qstr = QString::fromUtf8(string, length);
    PrefixWidths widths{metrics, scale, spacing, qstr.size()};
    qreal full_width = widths.measure(qstr);

    if (length == 0 || full_width <= split) {
        *string_idx = length;
        *actual_x = qCeil(full_width);
        return NSERROR_OK;
    }

    nserror res = layout_position(widths, qstr, length, split, &split_len, &split_x);
    if (res != NSERROR_OK)
        return res;

    /* If the position estimate reaches the end despite overflowing (very
     * narrow glyphs), restart at the first word. A zero estimate likewise
     * searches forward for a complete word, even if that word cannot fit. */
    if (split_len >= length)
        split_len = 0;

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

    /* After finding space, GREEDILY try to include more words that fit.
     * We measure WITHOUT trailing space since spaces are trimmed at line end. */
    /* Initialize last_word_end to position BEFORE trailing space */
    last_word_end = str_len;
    if (last_word_end > 0 && string[last_word_end - 1] == ' ') {
        last_word_end--;
    }
    while (str_len < length) {
        size_t word_end = str_len;
        /* Find the end of the next word (before any trailing space) */
        while ((word_end < length) && (string[word_end] != ' ')) {
            word_end++;
        }

        /* Measure width WITHOUT trailing space - space is trimmed at line end */
        qreal word_width = widths.measure(QString::fromUtf8(string, word_end));

        if (word_width <= split) {
            /* This word fits! Update last_word_end to this position */
            last_word_end = word_end;
            /* Include trailing space in str_len so it's consumed for next iteration */
            str_len = word_end;
            if (str_len < length && string[str_len] == ' ') {
                str_len++;
            }
        } else {
            /* This word doesn't fit, stop here */
            break;
        }
    }

    /* The core consumes text[*string_idx] when it is a space and records its
     * advance separately. Including that space in the offset but not the width
     * loses the gap when retained text boxes are placed together on reflow. */
    *string_idx = last_word_end;
    *actual_x = qCeil(widths.measure(QString::fromUtf8(string, last_word_end)));

    return NSERROR_OK;
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
 *     string_idx giving split point closest to x, where actual_x <= x
 *   else
 *     string_idx giving split point closest to x, where actual_x > x
 *
 * Returning string_idx == length means no split possible
 */
static nserror nsqt_layout_split(const struct plot_font_style *fstyle, const char *string, size_t length, int split,
    size_t *string_idx, int *actual_x)
{
    nserror res = NSERROR_OK;
    quint64 generation = text_cache_generation;
    QFont font = nsfont_style_to_font(fstyle);
    /* Use top-level widget as device for accurate metrics matching rendering */
    QPaintDevice *device = get_metrics_device();
    QFontMetricsF metrics(font, device);
    TextMeasurement cached(font, metrics, fstyle, string, length, TextOperation::Split, split, generation);
    if (cached.find(actual_x, string_idx)) {
        return NSERROR_OK;
    }
    qreal scale = font_scale(fstyle);
    if (length > 0 && string[0] != ' ' && fstyle->letter_spacing >= 0 && scale > 0) {
        layout_split_words(metrics, scale, fstyle->letter_spacing, string, length, split, string_idx, actual_x);
    } else {
        res = layout_split_with_position(
            metrics, scale, fstyle->letter_spacing, string, length, split, string_idx, actual_x);
    }
    if (res != NSERROR_OK) {
        return res;
    }
    cached.insert(*actual_x, *string_idx);
    NSLOG(wisp, DEEPDEBUG,
        "fstyle: %p string:\"%.*s\", length: %" PRIsizet ", "
        "split: %dpx, offset: %" PRIsizet ", actual_x: %dpx",
        fstyle, (int)*string_idx, string, length, split, *string_idx, *actual_x);

    return res;
}


/* exported interface documented in qt/layout.h */
nserror
nsqt_layout_plot(QPainter *painter, const struct plot_font_style *fstyle, int x, int y, const char *text, size_t length)
{
    if (fstyle->size == 0)
        return NSERROR_OK;

    QColor strokecolour(
        fstyle->foreground & 0xFF, (fstyle->foreground & 0xFF00) >> 8, (fstyle->foreground & 0xFF0000) >> 16);
    QPen pen(strokecolour);
    QFont font = nsfont_style_to_font(fstyle);

    NSLOG(wisp, DEEPDEBUG, "fstyle: %p string:\"%.*s\", length: %" PRIsizet ", width: %dpx", fstyle, (int)length, text,
        length, QFontMetrics(font, painter->device()).horizontalAdvance(text, length));

    painter->setPen(pen);
    painter->setFont(font);

    qreal scale = font_scale(fstyle);
    if (scale != 1.0) {
        painter->save();
        painter->translate(x, y);
        painter->scale(scale, scale);
        painter->drawText(0, 0, QString::fromUtf8(text, length));
        painter->restore();
    } else {
        painter->drawText(x, y, QString::fromUtf8(text, length));
    }

    return NSERROR_OK;
}


/**
 * Load font data into Qt's font database.
 *
 * This function is called from the core when a web font is downloaded
 * via gui_layout_table.load_font_data.
 */
static nserror nsqt_load_font_data(const struct font_variant_id *id, const uint8_t *data, size_t size)
{
    if (data == nullptr || size == 0 || size > 64 * 1024 * 1024 || id == nullptr || id->family_name == nullptr ||
        id->font_set == 0)
        return NSERROR_INVALID;
    int weight = 400;
    if (id->weight == CSS_FONT_WEIGHT_BOLD)
        weight = 700;
    else if (id->weight >= CSS_FONT_WEIGHT_100 && id->weight <= CSS_FONT_WEIGHT_900)
        weight = (id->weight - CSS_FONT_WEIGHT_100 + 1) * 100;
    const QFont::Style style = id->style == CSS_FONT_STYLE_ITALIC ? QFont::StyleItalic
        : id->style == CSS_FONT_STYLE_OBLIQUE                     ? QFont::StyleOblique
                                                                  : QFont::StyleNormal;
    nsqt_font_cache_init();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(data), qsizetype(size)));
    const char descriptors[] = {char(weight / 100), char(style)};
    hash.addData(QByteArrayView(descriptors, sizeof(descriptors)));
    const QByteArray key = hash.result();
    auto resource = web_font_resources.value(key).lock();
    if (!resource) {
        const QByteArray private_name = "WispWeb" + QUuid::createUuid().toByteArray(QUuid::Id128);
        QByteArray fontData = nsqt_webfont_data(
            QByteArray::fromRawData(reinterpret_cast<const char *>(data), size), private_name, weight, style);
        if (fontData.isEmpty())
            return NSERROR_INVALID;
        const int fontId = QFontDatabase::addApplicationFontFromData(fontData);
        if (fontId == -1)
            return NSERROR_INVALID;
        const QString family = QString::fromLatin1(private_name);
        if (!QFontDatabase::applicationFontFamilies(fontId).contains(family)) {
            QFontDatabase::removeApplicationFont(fontId);
            return NSERROR_INVALID;
        }
        resource = std::shared_ptr<const WebFontResource>(new WebFontResource{family, fontId});
        web_font_resources.insert(key, resource);
    }
    web_fonts[id->font_set][QString::fromUtf8(id->family_name).toCaseFolded()].append(
        {std::move(resource), weight, id->style, id->order});
    nsqt_font_cache_clear_set(id->font_set);

    return NSERROR_OK;
}


static nserror nsqt_font_metrics(const struct plot_font_style *fstyle, int *ascent, int *descent)
{
    QFont font = nsfont_style_to_font(fstyle);
    QFontMetricsF metrics(font, get_metrics_device());
    qreal scale = font_scale(fstyle);
    *ascent = qRound(metrics.ascent() * scale);
    *descent = qRound(metrics.descent() * scale);
    return NSERROR_OK;
}

static void nsqt_release_font_set(uint64_t font_set)
{
    auto found = web_fonts.find(font_set);
    if (found == web_fonts.end())
        return;
    // Remove the selection namespace before the last resource owner causes a
    // font-database notification. Other documents keep shared resources alive.
    WebFontFamilies families = std::move(*found);
    web_fonts.erase(found);
    nsqt_font_cache_clear_set(font_set);
    families.clear();
    for (auto resource = web_font_resources.begin(); resource != web_font_resources.end();) {
        if (resource->expired())
            resource = web_font_resources.erase(resource);
        else
            ++resource;
    }
}

static struct gui_layout_table layout_table = {
    .width = nsqt_layout_width,
    .position = nsqt_layout_position,
    .split = nsqt_layout_split,
    .load_font_data = nsqt_load_font_data,
    .release_font_set = nsqt_release_font_set,
    .font_metrics = nsqt_font_metrics,
};

struct gui_layout_table *nsqt_layout_table = &layout_table;
