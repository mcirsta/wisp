/* SPDX-License-Identifier: GPL-2.0-or-later
 * CSSOM View measurements of the current document layout. */
#include "dom.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#include <wisp/content.h>
#include <wisp/content/content.h>
#include <wisp/browser_window.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/box_geometry.h"
#include "content/handlers/html/view_events.h"
#include "desktop/scrollbar.h"

/* No box from another/inert document, or from a detached subtree, is usable.
 * Flush writes in this JS entry before asking the engine to update layout. */
nserror qjs_dom_geometry_box(JSContext *ctx, dom_node *node, struct box **box)
{
    *box = NULL;
    jsthread *thread = JS_GetContextOpaque(ctx);
    html_content *html = thread->doc_priv;
    if (html == NULL || !qjs_dom_is_active_document(ctx, node))
        return NSERROR_OK;
    bool connected = false;
    if (dom_node_contains(html->document, node, &connected) != DOM_NO_ERR)
        return NSERROR_DOM;
    if (!connected)
        return NSERROR_OK;
    qjs_dom_flush(thread);
    nserror err = html_update_layout(html);
    if (err == NSERROR_OK)
        *box = box_for_node(node);
    return err;
}

static JSValue client_size(JSContext *ctx, JSValueConst object, int vertical)
{
    struct qjs_dom_node *node = qjs_dom_get(ctx, object, DOM_ELEMENT_NODE);
    if (node == NULL)
        return JS_EXCEPTION;
    struct box *box;
    nserror err = qjs_dom_geometry_box(ctx, node->node, &box);
    if (err != NSERROR_OK)
        return err == NSERROR_NOMEM ? JS_ThrowOutOfMemory(ctx) : JS_ThrowInternalError(ctx, "Layout unavailable");
    if (box == NULL || (box->type == BOX_INLINE && !(box->flags & IS_REPLACED)))
        return JS_NewInt32(ctx, 0);

    jsthread *thread = JS_GetContextOpaque(ctx);
    html_content *html = thread->doc_priv;
    dom_node *root = NULL, *body = NULL;
    dom_document_quirks_mode mode;
    dom_exception dom_err = dom_document_get_quirks_mode(html->document, &mode);
    if (dom_err != DOM_NO_ERR)
        return qjs_dom_exception(ctx, dom_err);
    bool quirks = mode == DOM_DOCUMENT_QUIRKS_MODE_FULL;
    dom_document_get_document_element(html->document, &root);
    if (quirks)
        dom_html_document_get_body(html->document, &body);
    bool viewport = (!quirks && node->node == root) || (quirks && node->node == body);
    dom_node_unref(root);
    dom_node_unref(body);

    double pixels;
    if (viewport) {
        pixels = FIXTOFLT(vertical ? html->media.height : html->media.width);
    } else {
        int start = vertical ? TOP : LEFT, end = vertical ? BOTTOM : RIGHT;
        pixels = (double)(vertical ? box->height : box->width) + box->padding[start] + box->padding[end];
        uint8_t overflow = vertical ? css_computed_overflow_x(box->style) : css_computed_overflow_y(box->style);
        bool scrollbar = overflow == CSS_OVERFLOW_SCROLL ||
            (overflow == CSS_OVERFLOW_AUTO && (vertical ? box_hscrollbar_present(box) : box_vscrollbar_present(box)));
        if (scrollbar)
            pixels = fmax(0, pixels - SCROLLBAR_WIDTH);
        pixels *= 96.0 / FIXTOFLT(html->unit_len_ctx.device_dpi);
    }
    return JS_NewInt32(ctx, (int32_t)fmin(INT_MAX, fmax(0, round(pixels))));
}

static JSValue bounding_rect(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    struct qjs_dom_node *node = qjs_dom_get(ctx, object, DOM_ELEMENT_NODE);
    if (node == NULL)
        return JS_EXCEPTION;
    struct box *box;
    nserror err = qjs_dom_geometry_box(ctx, node->node, &box);
    if (err != NSERROR_OK)
        return err == NSERROR_NOMEM ? JS_ThrowOutOfMemory(ctx) : JS_ThrowInternalError(ctx, "Layout unavailable");
    struct box_rect bounds = {0};
    double scale = 1;
    if (box != NULL) {
        jsthread *thread = JS_GetContextOpaque(ctx);
        html_content *html = thread->doc_priv;
        box_geometry_bounds(html, box, true, &bounds);
        scale = 96.0 / FIXTOFLT(html->unit_len_ctx.device_dpi);
    }
    return qjs_rect_new(
        ctx, bounds.x0 * scale, bounds.y0 * scale, (bounds.x1 - bounds.x0) * scale, (bounds.y1 - bounds.y0) * scale);
}

static bool html_tag(dom_node *node, dom_html_element_type expected)
{
    dom_html_element_type type;
    return dom_html_element_get_tag_type(node, &type) == DOM_NO_ERR && type == expected;
}

/* Walk DOM ancestry: positioned inline ancestors are flattened in the box
 * tree. A returned node is referenced, including for table/body fallbacks. */
static dom_node *offset_parent(dom_node *node, struct box *box)
{
    if (box == NULL || box->parent == NULL || html_tag(node, DOM_HTML_ELEMENT_TYPE_BODY))
        return NULL;
    bool fixed = css_computed_position(box->style) == CSS_POSITION_FIXED;
    if (fixed && (box->abs_containing_block == NULL || (box->abs_containing_block->flags & IS_VIEWPORT)))
        return NULL;
    bool is_static = css_computed_position(box->style) == CSS_POSITION_STATIC;
    dom_node *parent = NULL;
    dom_node_get_parent_node(node, &parent);
    while (parent != NULL) {
        struct box *ancestor = box_for_node(parent);
        if (ancestor != NULL && ancestor->style != NULL &&
            (box_has_transform(ancestor) ||
                (!fixed &&
                    (css_computed_position(ancestor->style) != CSS_POSITION_STATIC ||
                        html_tag(parent, DOM_HTML_ELEMENT_TYPE_BODY) ||
                        (is_static &&
                            (html_tag(parent, DOM_HTML_ELEMENT_TYPE_TABLE) ||
                                html_tag(parent, DOM_HTML_ELEMENT_TYPE_TD) ||
                                html_tag(parent, DOM_HTML_ELEMENT_TYPE_TH)))))))
            return parent;
        dom_node *next = NULL;
        dom_node_get_parent_node(parent, &next);
        dom_node_unref(parent);
        parent = next;
    }
    return NULL;
}

enum offset_property { OFFSET_WIDTH, OFFSET_HEIGHT, OFFSET_TOP, OFFSET_LEFT, OFFSET_PARENT };

static JSValue offset_get(JSContext *ctx, JSValueConst object, int property)
{
    struct qjs_dom_node *node = qjs_dom_get(ctx, object, DOM_ELEMENT_NODE);
    if (node == NULL)
        return JS_EXCEPTION;
    if (!qjs_dom_is_html(node->node))
        return JS_ThrowTypeError(ctx, "HTMLElement receiver required");
    struct box *box;
    nserror err = qjs_dom_geometry_box(ctx, node->node, &box);
    if (err != NSERROR_OK)
        return err == NSERROR_NOMEM ? JS_ThrowOutOfMemory(ctx) : JS_ThrowInternalError(ctx, "Layout unavailable");
    dom_node *parent = property >= OFFSET_TOP ? offset_parent(node->node, box) : NULL;
    if (property == OFFSET_PARENT) {
        JSValue owner = qjs_dom_owner(ctx, object, node);
        JSValue result = JS_IsException(owner) ? JS_EXCEPTION : qjs_dom_wrap(ctx, owner, parent);
        JS_FreeValue(ctx, owner);
        dom_node_unref(parent);
        return result;
    }
    double pixels = 0;
    if (box != NULL) {
        jsthread *thread = JS_GetContextOpaque(ctx);
        html_content *html = thread->doc_priv;
        if (property <= OFFSET_HEIGHT) {
            struct box_rect bounds;
            box_geometry_bounds(html, box, false, &bounds);
            pixels = property == OFFSET_WIDTH ? bounds.x1 - bounds.x0 : bounds.y1 - bounds.y0;
        } else if (!html_tag(node->node, DOM_HTML_ELEMENT_TYPE_BODY)) {
            int x, y, px = 0, py = 0;
            box_coords_unscrolled(box, &x, &y);
            if (parent != NULL)
                box_coords_unscrolled(box_for_node(parent), &px, &py);
            pixels = property == OFFSET_LEFT ? (double)x - box->border[LEFT].width - px
                                             : (double)y - box->border[TOP].width - py;
        }
        pixels *= 96.0 / FIXTOFLT(html->unit_len_ctx.device_dpi);
    }
    dom_node_unref(parent);
    return JS_NewInt32(ctx, (int32_t)fmin(INT_MAX, fmax(INT_MIN, round(pixels))));
}

enum window_measurement { INNER_WIDTH, INNER_HEIGHT, SCROLL_X, SCROLL_Y, PAGE_X_OFFSET, PAGE_Y_OFFSET };
static const char *const window_names[] = {
    "innerWidth", "innerHeight", "scrollX", "scrollY", "pageXOffset", "pageYOffset"};

static bool window_receiver(JSContext *ctx, JSValueConst object)
{
    JSValue global = JS_GetGlobalObject(ctx);
    bool valid = JS_IsUndefined(object) || JS_IsNull(object) || JS_IsStrictEqual(ctx, object, global);
    JS_FreeValue(ctx, global);
    if (!valid)
        JS_ThrowTypeError(ctx, "Window receiver required");
    return valid;
}

static JSValue window_measure(JSContext *ctx, JSValueConst object, int property)
{
    if (!window_receiver(ctx, object))
        return JS_EXCEPTION;
    jsthread *thread = JS_GetContextOpaque(ctx);
    html_content *html = thread && !thread->closed ? thread->doc_priv : NULL;
    if (html == NULL)
        return JS_NewInt32(ctx, 0);
    qjs_dom_flush(thread);
    nserror err = html_update_layout(html);
    if (err != NSERROR_OK)
        return err == NSERROR_NOMEM ? JS_ThrowOutOfMemory(ctx) : JS_ThrowInternalError(ctx, "Layout unavailable");
    double pixels;
    if (property <= INNER_HEIGHT) {
        unsigned width = 0, height = 0;
        union content_msg_data msg = {
            .getdims = {.viewport_width = &width, .viewport_height = &height, .include_scrollbars = true}};
        content_broadcast(&html->base, CONTENT_MSG_GETDIMS, &msg);
        pixels = property == INNER_WIDTH ? width : height;
    } else {
        double x, y;
        browser_window_get_scroll_position(html->bw, &x, &y);
        pixels = property == SCROLL_X || property == PAGE_X_OFFSET ? x : y;
    }
    pixels *= 96.0 / FIXTOFLT(html->unit_len_ctx.device_dpi);
    return property <= INNER_HEIGHT ? JS_NewInt32(ctx, (int32_t)fmin(INT_MAX, fmax(0, round(pixels))))
                                    : JS_NewFloat64(ctx, pixels);
}

/* Web IDL [Replaceable]: assignment replaces this Window property. It neither
 * resizes nor scrolls the native viewport. */
static JSValue window_measure_set(JSContext *ctx, JSValueConst object, JSValueConst value, int property)
{
    if (!window_receiver(ctx, object))
        return JS_EXCEPTION;
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = JS_DefinePropertyValueStr(ctx, global, window_names[property], JS_DupValue(ctx, value), JS_PROP_C_W_E);
    JS_FreeValue(ctx, global);
    return ret < 0 ? JS_EXCEPTION : JS_UNDEFINED;
}

struct scroll_options {
    double x, y;
    bool has_x, has_y;
};

static int scroll_coordinate(JSContext *ctx, JSValueConst value, double *coordinate)
{
    if (JS_ToFloat64(ctx, coordinate, value) < 0)
        return -1;
    if (!isfinite(*coordinate))
        *coordinate = 0;
    return 0;
}

/* Web IDL overload resolution and dictionary conversion happen before reading
 * the viewport. Getters/valueOf can mutate layout, scroll, or retire this realm. */
static int scroll_options(JSContext *ctx, int argc, JSValueConst *argv, struct scroll_options *options)
{
    if (argc >= 2) {
        options->has_x = options->has_y = true;
        return scroll_coordinate(ctx, argv[0], &options->x) < 0 ? -1
            : scroll_coordinate(ctx, argv[1], &options->y);
    }
    if (argc == 0 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]))
        return 0;
    if (!JS_IsObject(argv[0])) {
        JS_ThrowTypeError(ctx, "ScrollToOptions requires an object");
        return -1;
    }
    /* Inherited dictionary members first, then members in lexical order. */
    JSValue behavior = JS_GetPropertyStr(ctx, argv[0], "behavior");
    if (JS_IsException(behavior))
        return -1;
    if (!JS_IsUndefined(behavior)) {
        size_t length;
        const char *text = JS_ToCStringLen(ctx, &length, behavior);
        bool converted = text != NULL;
        bool valid = text != NULL && ((length == 4 && memcmp(text, "auto", 4) == 0) ||
            (length == 6 && memcmp(text, "smooth", 6) == 0) ||
            (length == 7 && memcmp(text, "instant", 7) == 0));
        JS_FreeCString(ctx, text);
        JS_FreeValue(ctx, behavior);
        if (!valid) {
            if (converted)
                JS_ThrowTypeError(ctx, "Invalid scroll behavior");
            return -1;
        }
    } else {
        JS_FreeValue(ctx, behavior);
    }
    const char *names[] = {"left", "top"};
    for (unsigned i = 0; i < 2; i++) {
        JSValue value = JS_GetPropertyStr(ctx, argv[0], names[i]);
        if (JS_IsException(value))
            return -1;
        bool present = !JS_IsUndefined(value);
        int ret = present ? scroll_coordinate(ctx, value, i == 0 ? &options->x : &options->y) : 0;
        JS_FreeValue(ctx, value);
        if (ret < 0)
            return -1;
        if (i == 0)
            options->has_x = present;
        else
            options->has_y = present;
    }
    return 0;
}

/* CSSOM View, published 2025-09-16: scrollTo is equivalent to scroll.
 * Wisp does not honor smooth scrolling, as permitted by section 3.1. All
 * accepted behaviors use the same native instant scroll, never a no-op. */
static JSValue window_scroll(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv, int relative)
{
    if (!window_receiver(ctx, object))
        return JS_EXCEPTION;
    struct scroll_options options = {0};
    if (scroll_options(ctx, argc, argv, &options) < 0)
        return JS_EXCEPTION;
    jsthread *thread = JS_GetContextOpaque(ctx);
    html_content *html = thread && !thread->closed ? thread->doc_priv : NULL;
    if (html == NULL || html->bw == NULL)
        return JS_UNDEFINED;
    qjs_dom_flush(thread);
    nserror err = html_update_layout(html);
    if (err != NSERROR_OK)
        return err == NSERROR_NOMEM ? JS_ThrowOutOfMemory(ctx) : JS_ThrowInternalError(ctx, "Layout unavailable");
    double before_x, before_y, after_x, after_y;
    browser_window_get_scroll_position(html->bw, &before_x, &before_y);
    double css_to_device = FIXTOFLT(html->unit_len_ctx.device_dpi) / 96.0;
    double x = options.has_x ? options.x * css_to_device : (relative ? 0 : before_x);
    double y = options.has_y ? options.y * css_to_device : (relative ? 0 : before_y);
    if (relative) {
        x += before_x;
        y += before_y;
    }
    err = browser_window_scroll_to(html->bw, x, y);
    if (err != NSERROR_OK)
        return JS_ThrowInternalError(ctx, "Viewport scrolling unavailable");
    browser_window_get_scroll_position(html->bw, &after_x, &after_y);
    qjs_trace(ctx, "viewport.scroll", "relative=%d x=%g y=%g actual_x=%g actual_y=%g", relative,
        x / css_to_device, y / css_to_device, after_x / css_to_device, after_y / css_to_device);
    if (before_x != after_x || before_y != after_y)
        html_queue_scroll_end(html);
    return JS_UNDEFINED;
}

int qjs_dom_install_geometry(JSContext *ctx, JSValue *prototypes)
{
    if (qjs_init_rect(ctx) < 0)
        return -1;
    JSValue global = JS_GetGlobalObject(ctx);
    for (int i = 0; i < (int)(sizeof(window_names) / sizeof(*window_names)); i++) {
        JSValue get = JS_NewCFunctionMagic(
            ctx, (JSCFunctionMagic *)window_measure, window_names[i], 0, JS_CFUNC_getter_magic, i);
        JSValue set = JS_NewCFunctionMagic(
            ctx, (JSCFunctionMagic *)window_measure_set, window_names[i], 1, JS_CFUNC_setter_magic, i);
        JSAtom name = JS_NewAtom(ctx, window_names[i]);
        if (JS_IsException(get) || JS_IsException(set) || name == JS_ATOM_NULL) {
            JS_FreeValue(ctx, get);
            JS_FreeValue(ctx, set);
            JS_FreeAtom(ctx, name);
            JS_FreeValue(ctx, global);
            return -1;
        }
        int ret = JS_DefinePropertyGetSet(ctx, global, name, get, set, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, name);
        if (ret < 0) {
            JS_FreeValue(ctx, global);
            return -1;
        }
    }
    const char *methods[] = {"scroll", "scrollTo", "scrollBy"};
    int ret = 0;
    for (unsigned i = 0; i < sizeof(methods) / sizeof(*methods); i++) {
        JSValue function = JS_NewCFunctionMagic(ctx, window_scroll, methods[i], 0, JS_CFUNC_generic_magic, i == 2);
        if (JS_IsException(function) || JS_DefinePropertyValueStr(ctx, global, methods[i], function, JS_PROP_C_W_E) < 0) {
            ret = -1;
            break;
        }
    }
    JS_FreeValue(ctx, global);
    if (ret < 0)
        return -1;
    static const JSCFunctionListEntry properties[] = {
        JS_CGETSET_MAGIC_DEF2("clientWidth", client_size, NULL, 0, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2("clientHeight", client_size, NULL, 1, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CFUNC_DEF2("getBoundingClientRect", 0, bounding_rect, JS_PROP_C_W_E),
    };
    static const JSCFunctionListEntry offsets[] = {
        JS_CGETSET_MAGIC_DEF2("offsetWidth", offset_get, NULL, OFFSET_WIDTH, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2(
            "offsetHeight", offset_get, NULL, OFFSET_HEIGHT, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2("offsetTop", offset_get, NULL, OFFSET_TOP, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2("offsetLeft", offset_get, NULL, OFFSET_LEFT, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2(
            "offsetParent", offset_get, NULL, OFFSET_PARENT, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    };
    if (JS_SetPropertyFunctionList(ctx, prototypes[QJS_HTML_ELEMENT], offsets, sizeof(offsets) / sizeof(*offsets)) < 0)
        return -1;
    return JS_SetPropertyFunctionList(
        ctx, prototypes[QJS_ELEMENT], properties, sizeof(properties) / sizeof(properties[0]));
}
