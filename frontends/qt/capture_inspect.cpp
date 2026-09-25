/* SPDX-License-Identifier: GPL-2.0-only */
#include "qt/capture_inspect.h"

#include <QFile>
#include <QFontInfo>
#include <QJsonArray>
#include <QVector>

extern "C" {
// These C-only headers use parameter names/qualifiers reserved in C++.
#define namespace dom_namespace
#define restrict __restrict
#include <dom/dom.h>
#include "wisp/content.h"
#include "wisp/content/hlcache.h"
#include "wisp/content/handlers/html/box.h"
#include "wisp/content/handlers/html/box_inspect.h"
#include "wisp/content/handlers/html/html_save.h"
#include "wisp/content/handlers/html/private.h"
#include "wisp/plot_style.h"
#include "content/handlers/css/dump.h"
#include "content/handlers/html/font.h"
#undef restrict
#undef namespace
}

static QString domName(dom_node *node)
{
    dom_string *name = nullptr;
    if (!node || dom_node_get_node_name(node, &name) != DOM_NO_ERR || !name) {
        return {};
    }
    const QString result = QString::fromUtf8(dom_string_data(name), dom_string_byte_length(name)).toLower();
    dom_string_unref(name);
    return result;
}

static QString attribute(dom_node *node, const char *name)
{
    dom_node_type type;
    if (!node || dom_node_get_node_type(node, &type) != DOM_NO_ERR || type != DOM_ELEMENT_NODE) {
        return {};
    }
    dom_string *key = nullptr, *value = nullptr;
    if (dom_string_create(reinterpret_cast<const uint8_t *>(name), strlen(name), &key) != DOM_NO_ERR) {
        return {};
    }
    dom_element_get_attribute(node, key, &value);
    dom_string_unref(key);
    const QString result = value ? QString::fromUtf8(dom_string_data(value), dom_string_byte_length(value)) : QString();
    if (value) {
        dom_string_unref(value);
    }
    return result;
}

// Structural identity without process-specific pointers. Sibling indices count
// nodes with the same name, so whitespace text nodes do not renumber elements.
static QJsonObject identity(dom_node *node)
{
    QStringList path;
    dom_node *current = node ? dom_node_ref(node) : nullptr;
    while (current) {
        const QString name = domName(current);
        if (name == "#document") {
            dom_node_unref(current);
            break;
        }
        int index = 1;
        dom_node_type current_type;
        dom_node_get_node_type(current, &current_type);
        dom_node *sibling = nullptr;
        dom_node_get_previous_sibling(current, &sibling);
        while (sibling) {
            dom_node_type sibling_type;
            dom_node_get_node_type(sibling, &sibling_type);
            if (sibling_type == current_type && domName(sibling) == name) {
                ++index;
            }
            dom_node *previous = nullptr;
            dom_node_get_previous_sibling(sibling, &previous);
            dom_node_unref(sibling);
            sibling = previous;
        }
        path.prepend(name + "[" + QString::number(index) + "]");
        dom_node *parent = nullptr;
        dom_node_get_parent_node(current, &parent);
        dom_node_unref(current);
        current = parent;
    }
    return {{"tag", domName(node)}, {"id", attribute(node, "id")}, {"class", attribute(node, "class")},
        {"path", node ? "/" + path.join('/') : QString()}};
}

static QJsonObject rectangle(int x, int y, int width, int height)
{
    return {{"x", x}, {"y", y}, {"width", width}, {"height", height}};
}

static QJsonObject summary(struct box *box)
{
    static const QStringList types{"block", "inline-container", "inline", "table", "table-row", "table-cell",
        "table-row-group", "float-left", "float-right", "inline-block", "br", "text", "inline-end", "none", "flex",
        "inline-flex", "grid", "inline-grid"};
    int x, y;
    box_coords(box, &x, &y); // Core coordinates include containing blocks and ancestor scrolling.
    const int pw = box->padding[LEFT] + box->width + box->padding[RIGHT];
    const int ph = box->padding[TOP] + box->height + box->padding[BOTTOM];
    auto *owner = box;
    while (owner && !owner->node) {
        owner = owner->parent;
    }
    return {{"node", identity(box->node)}, {"owner_node", identity(owner ? owner->node : nullptr)},
        {"box_type", types.value(box->type, "unknown")},
        {"content", rectangle(x + box->padding[LEFT], y + box->padding[TOP], box->width, box->height)},
        {"padding_box", rectangle(x, y, pw, ph)},
        {"border_box",
            rectangle(x - box->border[LEFT].width, y - box->border[TOP].width,
                pw + box->border[LEFT].width + box->border[RIGHT].width,
                ph + box->border[TOP].width + box->border[BOTTOM].width)}};
}

static QJsonObject describe(struct box *box, const css_unit_ctx *units)
{
    QJsonObject result = summary(box);
    result["margin_trbl"] = QJsonArray{box->margin[TOP], box->margin[RIGHT], box->margin[BOTTOM], box->margin[LEFT]};
    result["padding_trbl"] = QJsonArray{
        box->padding[TOP], box->padding[RIGHT], box->padding[BOTTOM], box->padding[LEFT]};
    result["visible"] = box_visible(box);
    result["text"] = box->text ? QString::fromUtf8(box->text, qMin(box->length, size_t(200))) : QString();
    if (box->abs_containing_block) {
        result["absolute_containing_block"] = summary(box->abs_containing_block);
    }
    QJsonArray parents, children;
    for (auto *parent = box->parent; parent && parents.size() < 12; parent = parent->parent) {
        parents.append(summary(parent));
    }
    int child_count = 0;
    for (auto *child = box->children; child; child = child->next) {
        if (++child_count <= 64) {
            children.append(summary(child));
        }
    }
    result["ancestors"] = parents;
    result["children"] = children;
    result["child_count"] = child_count;
    if (box->style) {
        // Reuse the existing serializer; avoid maintaining a second CSS serializer.
        FILE *stream = tmpfile();
        if (stream) {
            nscss_dump_computed_style(stream, box->style);
            if (fflush(stream) == 0 && fseek(stream, 0, SEEK_SET) == 0) {
                QFile file;
                if (file.open(stream, QIODevice::ReadOnly)) {
                    result["computed_style_dump"] = QString::fromUtf8(file.readAll());
                    file.close(); // QFile does not own the FILE* by default.
                }
            }
            fclose(stream);
        }
        // The legacy dump predates flex/grid. Include useful modern layout values.
        css_fixed grow = 0, shrink = 0;
        css_computed_flex_grow(box->style, &grow);
        css_computed_flex_shrink(box->style, &shrink);
        result["layout_style"] = QJsonObject{
            {"flex-direction",
                QStringList{"inherit", "row", "row-reverse", "column", "column-reverse"}.value(
                    css_computed_flex_direction(box->style))},
            {"flex-wrap",
                QStringList{"inherit", "nowrap", "wrap", "wrap-reverse"}.value(css_computed_flex_wrap(box->style))},
            {"flex-grow", FIXTOFLT(grow)}, {"flex-shrink", FIXTOFLT(shrink)},
            {"justify-content",
                QStringList{
                    "inherit", "flex-start", "flex-end", "center", "space-between", "space-around", "space-evenly"}
                    .value(css_computed_justify_content(box->style))},
            {"align-items",
                QStringList{"inherit", "stretch", "flex-start", "flex-end", "center", "baseline"}.value(
                    css_computed_align_items(box->style))},
            {"grid-auto-flow",
                QStringList{"inherit", "row", "column", "row dense", "column dense"}.value(
                    css_computed_grid_auto_flow(box->style))}};
        plot_font_style_t style;
        font_plot_style_from_css(units, box->style, &style);
        const QFont font = nsqt_capture_font(&style);
        result["font"] = QJsonObject{{"selected_family", font.family()}, {"resolved_family", QFontInfo(font).family()},
            {"pixel_size", font.pixelSize()}, {"weight", font.weight()}, {"italic", font.italic()}};
    }
    return result;
}

QJsonObject nsqt_capture_inspect(struct hlcache_handle *content, const QStringList &ids, const QPoint &point)
{
    QJsonObject result{
        {"schema_version", 1}, {"coordinate_space", "unscaled document layout pixels; before transforms"}};
    if (!content || content_get_type(content) != CONTENT_HTML) {
        result["status"] = "html_unavailable";
        return result;
    }
    auto *html = reinterpret_cast<html_content *>(hlcache_handle_get_content(content));
    auto *root = html_get_box_tree(content);
    if (!root) {
        result["status"] = "layout_unavailable";
        return result;
    }
    result["status"] = "ok";
    QJsonArray elements;
    for (const QString &id : ids) {
        const QByteArray utf8 = id.toUtf8();
        dom_string *key = nullptr;
        dom_element *element = nullptr;
        if (dom_string_create(reinterpret_cast<const uint8_t *>(utf8.constData()), utf8.size(), &key) == DOM_NO_ERR) {
            dom_document_get_element_by_id(html_get_document(content), key, &element);
            dom_string_unref(key);
        }
        QJsonArray matches;
        int count = 0;
        if (element) {
            QVector<struct box *> pending{root};
            while (!pending.isEmpty()) {
                auto *candidate = pending.takeLast();
                if (candidate->node == reinterpret_cast<dom_node *>(element)) {
                    if (++count <= 64) {
                        matches.append(describe(candidate, &html->unit_len_ctx));
                    }
                }
                for (auto *child = candidate->last; child; child = child->prev) {
                    pending.append(child);
                }
            }
            dom_node_unref(element);
        }
        elements.append(QJsonObject{{"id", id},
            {"status",
                !element    ? "not_found"
                    : count ? "ok"
                            : "no_box"},
            {"box_count", count}, {"boxes", matches}});
    }
    result["elements"] = elements;
    if (point.x() >= 0) {
        QJsonArray hits;
        int x = 0, y = 0, count = 0;
        auto *box = root;
        while ((box = box_at_point(&html->unit_len_ctx, box, point.x(), point.y(), &x, &y))) {
            if (box_visible(box) && ++count <= 64) {
                hits.append(describe(box, &html->unit_len_ctx));
            }
        }
        result["point"] = QJsonObject{{"x", point.x()}, {"y", point.y()}, {"hit_count", count}, {"hits", hits}};
    }
    return result;
}
