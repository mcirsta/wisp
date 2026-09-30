/* SPDX-License-Identifier: GPL-2.0-only */
#include "qt/capture_inspect.h"

#include <QFile>
#include <QFontInfo>
#include <QJsonArray>
#include <QRawFont>
#include <QVector>
#include <QtEndian>

extern "C" {
// These C-only headers use parameter names/qualifiers reserved in C++.
#define namespace dom_namespace
#define restrict __restrict
#include <dom/dom.h>
#include "wisp/content.h"
#include "wisp/browser_window.h"
#include "wisp/content/hlcache.h"
#include "wisp/content/handlers/html/box.h"
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/box_boundary.h"
#include "wisp/content/handlers/html/box_inspect.h"
#include "wisp/content/handlers/html/html_save.h"
#include "wisp/content/handlers/html/private.h"
#include "wisp/plot_style.h"
#include "content/handlers/css/dump.h"
#include "content/handlers/html/font.h"
#include "desktop/scrollbar.h"
#include "utils/talloc.h"
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

static QJsonObject describe(struct box *box, const html_content *html)
{
    const css_unit_ctx *units = &html->unit_len_ctx;
    QJsonObject result = summary(box);
    result["owns_region"] = (box->flags & OWNS_REGION) != 0;
    result["text_offset"] = double(box->byte_offset);
    result["scrollbars"] = QJsonObject{{"horizontal", box->scroll_x != nullptr}, {"vertical", box->scroll_y != nullptr},
        {"x", scrollbar_get_offset(box->scroll_x)}, {"y", scrollbar_get_offset(box->scroll_y)}};
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    if (!html->restyle.css_only && !html->restyle.force_full)
        result["box_address"] = QString::number(reinterpret_cast<quintptr>(box), 16);
    if (box->flags & OWNS_REGION) {
        const char *reason;
        html_box_check_boundary(html, box, nullptr, &reason);
        result["box_boundary"] = reason;
    }
#endif
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
                QStringList{"inherit", "stretch", "flex-start", "flex-end", "center", "baseline", "", "normal"}.value(
                    css_computed_align_items(box->style))},
            {"grid-auto-flow",
                QStringList{"inherit", "row", "column", "row dense", "column dense"}.value(
                    css_computed_grid_auto_flow(box->style))}};
        plot_font_style_t style;
        font_plot_style_from_css(units, box->style, &style);
        const QFont font = nsqt_capture_font(&style);
        const QRawFont face = QRawFont::fromFont(font);
        const bool variable = !face.fontTable("fvar").isEmpty();
        const QByteArray os2 = face.fontTable("OS/2");
        result["font"] = QJsonObject{{"selected_family", font.family()}, {"resolved_family", QFontInfo(font).family()},
            {"pixel_size", font.pixelSize()},
            {"effective_pixel_size", font.pixelSize() * nsqt_capture_font_scale(&style)},
            {"weight", style.weight}, {"selected_weight", font.weight()},
            {"resolved_style", face.styleName()}, {"variable", variable},
            {"face_weight", !variable && os2.size() >= 6 ? QJsonValue(qFromBigEndian<quint16>(os2.constData() + 4)) :
                                                         QJsonValue(QJsonValue::Null)},
            {"italic", font.italic()}};
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
    result["resource_count"] = int(html->num_objects);
    result["active_resources"] = int(html->base.active - html->scripts_active);
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    if (!html->restyle.css_only && !html->restyle.force_full)
        result["tree_allocations"] = double(talloc_total_blocks(html->bctx));
#endif
#ifdef WISP_PROFILE_RESTYLE
    const auto &p = html->restyle_profile;
    if (p.total_ns != 0) {
        result["restyle_profile"] = QJsonObject{{"update_mode", p.update_mode ? p.update_mode : "unknown"},
            {"fallback_reason", p.fallback_reason ? p.fallback_reason : "none"},
            {"changed_controls", int(p.changed_controls)}, {"dirty_roots", int(p.dirty_roots)},
            {"invalidated_nodes", int(p.invalidated_nodes)}, {"reused_styles", int(p.reused_styles)},
            {"temporary_bytes", double(p.temporary_bytes)}, {"cloned_result_bytes", double(p.cloned_result_bytes)},
            {"style_references", int(p.style_references)}, {"restarts", int(p.restarts)}, {"invalidate_ms", p.invalidate_ns / 1e6},
            {"detach_ms", p.detach_ns / 1e6}, {"css_ms", p.css_ns / 1e6}, {"normalise_ms", p.normalise_ns / 1e6},
            {"boxes_ms", (p.construction_ns - p.css_ns - p.normalise_ns) / 1e6},
            {"construction_ms", p.construction_ns / 1e6},
            {"construction_wait_ms", (p.construction_wall_ns - p.construction_ns) / 1e6},
            {"layout_ms", p.layout_ns / 1e6}, {"cleanup_ms", p.cleanup_ns / 1e6}, {"total_ms", p.total_ns / 1e6},
            {"layout_cpu_ms", p.layout_cpu_ns ? QJsonValue(p.layout_cpu_ns / 1e6) : QJsonValue(QJsonValue::Null)},
            {"layout_reset_ms", p.layout_reset_ns / 1e6},
            {"intrinsic_inline_calculations", int(p.intrinsic_inline_calculations)},
            {"intrinsic_inline_reuses", int(p.intrinsic_inline_reuses)},
            {"inline_layout_calculations", int(p.inline_layout_calculations)},
            {"inline_layout_reuses", int(p.inline_layout_reuses)},
            {"inline_layout_width_misses", int(p.inline_layout_width_misses)},
            {"inline_layout_float_misses", int(p.inline_layout_float_misses)},
            {"inline_layout_intrinsic_misses", int(p.inline_layout_intrinsic_misses)},
            {"publication_ms", p.publication_ns / 1e6},
            {"paint_ms", p.paint_ns / 1e6},
            {"through_last_paint_ms", p.last_paint_ns ? (p.last_paint_ns - p.start_ns) / 1e6 : 0},
            {"styled_elements", int(p.styled_elements)}, {"construction_slices", int(p.construction_slices)},
            {"paint_calls", int(p.paint_calls)}};
    }
#endif
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
        QJsonObject state;
        if (element) {
            const QString tag = domName(reinterpret_cast<dom_node *>(element));
            if (tag == "input") {
                bool checked = false;
                dom_html_input_element_get_checked(reinterpret_cast<dom_html_input_element *>(element), &checked);
                state["checked"] = checked;
                dom_string *value = nullptr;
                dom_html_input_element_get_value(reinterpret_cast<dom_html_input_element *>(element), &value);
                if (value) {
                    state["value"] = QString::fromUtf8(dom_string_data(value), dom_string_byte_length(value));
                    dom_string_unref(value);
                }
            } else if (tag == "textarea") {
                dom_string *value = nullptr;
                dom_html_text_area_element_get_value(reinterpret_cast<dom_html_text_area_element *>(element), &value);
                if (value) {
                    state["value"] = QString::fromUtf8(dom_string_data(value), dom_string_byte_length(value));
                    dom_string_unref(value);
                }
            }
        }
        int count = 0;
        if (element) {
            QVector<struct box *> pending{root};
            while (!pending.isEmpty()) {
                auto *candidate = pending.takeLast();
                if (candidate->node == reinterpret_cast<dom_node *>(element)) {
                    if (++count <= 64) {
                        matches.append(describe(candidate, html));
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
            {"box_count", count}, {"boxes", matches}, {"state", state}});
    }
    result["elements"] = elements;
    if (point.x() >= 0) {
        QJsonArray hits;
        int x = 0, y = 0, count = 0;
        auto *box = root;
        while ((box = box_at_point(&html->unit_len_ctx, box, point.x(), point.y(), &x, &y))) {
            if (box_visible(box) && ++count <= 64) {
                hits.append(describe(box, html));
            }
        }
        result["point"] = QJsonObject{{"x", point.x()}, {"y", point.y()}, {"hit_count", count}, {"hits", hits}};
    }
    return result;
}

// Capture-only interaction hooks use the same core input path as the Qt widget.
void nsqt_capture_configure_restyle(struct hlcache_handle *content, bool force, bool restart, bool css_only, bool fail,
    bool no_intrinsic_reuse, bool reuse_inline_layout)
{
    Q_UNUSED(restart);
    Q_UNUSED(fail);
    Q_UNUSED(no_intrinsic_reuse);
    Q_UNUSED(reuse_inline_layout);
#ifdef WISP_ENABLE_PARTIAL_UPDATES
    if (content && content_get_type(content) == CONTENT_HTML) {
        auto *html = reinterpret_cast<html_content *>(hlcache_handle_get_content(content));
        html->restyle.force_full = force;
        html->restyle.css_only = css_only;
        html->restyle.no_intrinsic_reuse = no_intrinsic_reuse;
        html->restyle.reuse_inline_layout = reuse_inline_layout;
#ifdef WISP_PROFILE_RESTYLE
        html->restyle.force_restart = restart;
        html->restyle.fail_retained_layout = fail;
#endif
    }
#else
    Q_UNUSED(content);
    Q_UNUSED(force);
    Q_UNUSED(css_only);
#endif
}

bool nsqt_capture_settled(struct hlcache_handle *content)
{
    if (!content || content_get_type(content) != CONTENT_HTML)
        return true;
    auto *html = reinterpret_cast<html_content *>(hlcache_handle_get_content(content));
    return !html->restyling && !html->restyle_pending && html->base.active == html->scripts_active;
}

static void captureContentChanged(struct content *, content_msg, const union content_msg_data *, void *)
{
}

struct content *nsqt_capture_watch_content(struct hlcache_handle *handle)
{
    auto *content = hlcache_handle_get_content(handle);
    return content && content_add_user(content, captureContentChanged, nullptr) ? content : nullptr;
}

void nsqt_capture_unwatch_content(struct content *content)
{
    content_remove_user(content, captureContentChanged, nullptr);
}

QJsonObject nsqt_capture_restyle_lifecycle(struct content *content, bool slow)
{
    Q_UNUSED(content);
    Q_UNUSED(slow);
#if defined(WISP_ENABLE_PARTIAL_UPDATES) && defined(WISP_PROFILE_RESTYLE)
    if (content && content->handler->type() == CONTENT_HTML) {
        auto *html = reinterpret_cast<html_content *>(content);
        html->restyle.slow_restyle = slow;
        return {{"settled", !html->restyling && !html->restyle_pending && html->base.active == html->scripts_active},
            {"locked", html->base.locked}, {"building", html->restyling}, {"partial", html->restyle.partial},
            {"window_attached", html->bw != nullptr}, {"conversion", html->box_conversion_context != nullptr},
            {"old_tree", html->restyle_old_bctx != nullptr}, {"table_entries", double(html->restyle.count)},
            {"retained_build", !html->restyle.css_only && html->restyling &&
                html->box_conversion_context != nullptr && html->restyle_old_bctx == nullptr},
            {"active_roots", int(html->restyle.active.count)}, {"pending_roots", int(html->restyle.pending.count)},
            {"reused_styles", int(html->restyle_profile.reused_styles)},
            {"styled_elements", int(html->restyle_profile.styled_elements)},
            {"error", html->base.status == CONTENT_STATUS_ERROR}, {"restarts", int(html->restyle_profile.restarts)}};
    }
#endif
    return {};
}

void nsqt_capture_scroll(struct hlcache_handle *content, int x, int y)
{
    union content_msg_data data = {};
    data.scroll.x0 = x;
    data.scroll.y0 = y;
    content_broadcast(hlcache_handle_get_content(content), CONTENT_MSG_SCROLL, &data);
}

bool nsqt_capture_scroll_element(struct hlcache_handle *content, const QString &id, int x, int y)
{
    if (!content || content_get_type(content) != CONTENT_HTML)
        return false;
    const QByteArray utf8 = id.toUtf8();
    dom_string *key = nullptr;
    dom_node *node = nullptr;
    if (dom_string_create(reinterpret_cast<const uint8_t *>(utf8.constData()), utf8.size(), &key) != DOM_NO_ERR)
        return false;
    dom_document_get_element_by_id(html_get_document(content), key, &node);
    dom_string_unref(key);
    auto *box = node ? box_for_node(node) : nullptr;
    const bool found = box && (x == 0 || box->scroll_x) && (y == 0 || box->scroll_y);
    if (found) {
        /* Use the normal scrollbar API, including clamping and redraw callbacks.
         * Do not create scrollbars just to make an off-screen test pass. */
        if (box->scroll_x)
            scrollbar_set(box->scroll_x, x, false);
        if (box->scroll_y)
            scrollbar_set(box->scroll_y, y, false);
    }
    dom_node_unref(node);
    return found;
}

bool nsqt_capture_click(struct browser_window *bw, const QString &id, bool label)
{
    auto *content = browser_window_get_content(bw);
    if (!content || content_get_type(content) != CONTENT_HTML)
        return false;
    dom_document *doc = html_get_document(content);
    dom_node *target = nullptr;
    dom_string *key = nullptr;
    const QByteArray utf8 = id.toUtf8();
    if (dom_string_create(reinterpret_cast<const uint8_t *>(utf8.constData()), utf8.size(), &key) != DOM_NO_ERR)
        return false;
    if (label) {
        dom_string *tag = nullptr;
        dom_nodelist *labels = nullptr;
        dom_string_create(reinterpret_cast<const uint8_t *>("label"), 5, &tag);
        if (tag && dom_document_get_elements_by_tag_name(doc, tag, &labels) == DOM_NO_ERR) {
            uint32_t count = 0;
            dom_nodelist_get_length(labels, &count);
            for (uint32_t i = 0; i < count; i++) {
                dom_nodelist_item(labels, i, &target);
                if (attribute(target, "for") == id && box_for_node(target))
                    break;
                dom_node_unref(target);
                target = nullptr;
            }
            dom_nodelist_unref(labels);
        }
        dom_string_unref(tag);
    } else {
        dom_document_get_element_by_id(doc, key, &target);
    }
    dom_string_unref(key);
    auto *box = target ? box_for_node(target) : nullptr;
    bool found = box && box_visible(box);
    if (found) {
        int x, y, sx, sy, width, height;
        box_coords(box, &x, &y);
        x += box->padding[LEFT] + qMax(1, box->width / 2);
        y += box->padding[TOP] + qMax(1, box->height / 2);
        browser_window_get_scroll(bw, &sx, &sy);
        browser_window_get_dimensions(bw, &width, &height);
        if (x < sx || x >= sx + width || y < sy || y >= sy + height)
            nsqt_capture_scroll(content, qMax(0, x - width / 2), qMax(0, y - height / 2));
        browser_window_mouse_click(bw, BROWSER_MOUSE_PRESS_1, x, y);
        browser_window_mouse_click(bw, BROWSER_MOUSE_CLICK_1, x, y);
        browser_window_mouse_track(bw, (browser_mouse_state)0, x, y);
    }
    dom_node_unref(target);
    return found;
}
