/* SPDX-License-Identifier: GPL-2.0-or-later
 * CSSOM View measurements of the current document layout. */
#include "dom.h"
#include <limits.h>
#include <math.h>
#include <wisp/content.h>
#include <wisp/content/content.h>
#include <wisp/browser_window.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include "content/handlers/html/box_construct.h"
#include "content/handlers/html/box_geometry.h"
#include "desktop/scrollbar.h"

/* No box from another/inert document, or from a detached subtree, is usable.
 * Flush writes in this JS entry before asking the engine to update layout. */
static nserror geometry_box(JSContext *ctx, dom_node *node, struct box **box)
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
    nserror err = geometry_box(ctx, node->node, &box);
    if (err != NSERROR_OK)
        return err == NSERROR_NOMEM ? JS_ThrowOutOfMemory(ctx) : JS_ThrowInternalError(ctx, "Layout unavailable");
    if (box == NULL || (box->type == BOX_INLINE && !(box->flags & IS_REPLACED)))
        return JS_NewInt32(ctx, 0);

    jsthread *thread = JS_GetContextOpaque(ctx);
    html_content *html = thread->doc_priv;
    dom_node *root = NULL, *body = NULL;
    dom_document_get_document_element(html->document, &root);
    if (html->base.quirks)
        dom_html_document_get_body(html->document, &body);
    bool viewport = (!html->base.quirks && node->node == root) || (html->base.quirks && node->node == body);
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
    nserror err = geometry_box(ctx, node->node, &box);
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
    nserror err = geometry_box(ctx, node->node, &box);
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
        int x, y;
        browser_window_get_scroll(html->bw, &x, &y);
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
    JS_FreeValue(ctx, global);
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
