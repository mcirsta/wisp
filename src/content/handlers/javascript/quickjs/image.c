/* SPDX-License-Identifier: GPL-2.0-or-later
 * Reflected sources and element-owned image request state. */
#include "dom.h"
#include "event_target.h"
#include <inttypes.h>
#include <stdio.h>
#include "content/handlers/html/image.h"
#include "content/handlers/html/hyperlink.h"
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsurl.h>

static struct qjs_dom_node *image_receiver(JSContext *ctx, JSValueConst object)
{
    struct qjs_dom_node *node = qjs_dom_get(ctx, object, DOM_ELEMENT_NODE);
    if (node != NULL && (!qjs_dom_is_html(node->node) || !html_image_is_element(node->node))) {
        JS_ThrowTypeError(ctx, "HTMLImageElement receiver required");
        return NULL;
    }
    return node;
}

enum image_property {
    IMAGE_SRC,
    IMAGE_SRCSET,
    IMAGE_SIZES,
    IMAGE_COMPLETE,
    IMAGE_NATURAL_WIDTH,
    IMAGE_NATURAL_HEIGHT,
    IMAGE_CURRENT
};

static dom_string *image_attribute(int property)
{
    return property == IMAGE_SRC   ? corestring_dom_src
        : property == IMAGE_SRCSET ? corestring_dom_srcset
                                   : corestring_dom_sizes;
}

static JSValue image_get(JSContext *ctx, JSValueConst object, int property)
{
    struct qjs_dom_node *node = image_receiver(ctx, object);
    if (node == NULL)
        return JS_EXCEPTION;
    jsthread *thread = JS_GetContextOpaque(ctx);
    html_content *html = thread->doc_priv;
    if (property <= IMAGE_SIZES) {
        dom_string *text = NULL;
        dom_exception err = dom_element_get_attribute(node->node, image_attribute(property), &text);
        if (err != DOM_NO_ERR)
            return qjs_dom_exception(ctx, err);
        if (property != IMAGE_SIZES && text != NULL) {
            JSValue raw = qjs_dom_from_string(ctx, text, false);
            dom_string_unref(text);
            if (JS_IsException(raw))
                return JS_EXCEPTION;
            text = qjs_dom_usv_string(ctx, raw);
            JS_FreeValue(ctx, raw);
            if (text == NULL)
                return JS_EXCEPTION;
        }
        JSValue value;
        nsurl *url = NULL;
        if (property == IMAGE_SRC && text != NULL) {
            nsurl *base = html->base_url;
            dom_document *owner = NULL;
            dom_string *uri = NULL;
            nsurl *other = NULL;
            if (!qjs_dom_is_active_document(ctx, node->node)) {
                dom_node_get_owner_document(node->node, &owner);
                dom_document_get_uri(owner, &uri);
                if (uri != NULL)
                    nsurl_create(dom_string_data(uri), &other);
                base = other;
            }
            if (base != NULL)
                html_link_url(text, base, &url);
            if (other != NULL)
                nsurl_unref(other);
            dom_string_unref(uri);
            dom_node_unref(owner);
        }
        value = url != NULL ? JS_NewString(ctx, nsurl_access(url)) : qjs_dom_from_string(ctx, text, false);
        if (url != NULL)
            nsurl_unref(url);
        dom_string_unref(text);
        return value;
    }
    struct html_image_state state;
    html_image_get_state(html, node->node, &state);
    switch (property) {
    case IMAGE_COMPLETE:
        return JS_NewBool(ctx, state.complete);
    case IMAGE_NATURAL_WIDTH:
        return JS_NewUint32(ctx, state.natural_width);
    case IMAGE_NATURAL_HEIGHT:
        return JS_NewUint32(ctx, state.natural_height);
    default:
        return JS_NewString(ctx, state.current_src != NULL ? nsurl_access(state.current_src) : "");
    }
}

static JSValue image_set(JSContext *ctx, JSValueConst object, JSValueConst value, int property)
{
    if (image_receiver(ctx, object) == NULL)
        return JS_EXCEPTION;
    dom_string *text = property == IMAGE_SIZES ? qjs_dom_string(ctx, value, false) : qjs_dom_usv_string(ctx, value);
    if (text == NULL)
        return JS_EXCEPTION;
    struct qjs_dom_node *node = image_receiver(ctx, object);
    if (node == NULL) {
        dom_string_unref(text);
        return JS_EXCEPTION;
    }
    dom_string *name = image_attribute(property);
    dom_exception err = dom_element_set_attribute(node->node, name, text);
    dom_string_unref(text);
    qjs_dom_changed(ctx, node, "image.source", name, err);
    return err == DOM_NO_ERR ? JS_UNDEFINED : qjs_dom_exception(ctx, err);
}

static JSValue image_dimension_get(JSContext *ctx, JSValueConst object, int height)
{
    struct qjs_dom_node *node = image_receiver(ctx, object);
    if (node == NULL)
        return JS_EXCEPTION;
    struct box *box;
    nserror result = qjs_dom_geometry_box(ctx, node->node, &box);
    if (result != NSERROR_OK)
        return result == NSERROR_NOMEM ? JS_ThrowOutOfMemory(ctx) : JS_ThrowInternalError(ctx, "Layout unavailable");
    jsthread *thread = JS_GetContextOpaque(ctx);
    uint32_t value;
    dom_exception err = html_image_get_dimension(thread->doc_priv, node->node, box, height, &value);
    return err == DOM_NO_ERR ? JS_NewUint32(ctx, value) : qjs_dom_exception(ctx, err);
}

static JSValue image_dimension_write(JSContext *ctx, JSValueConst object, uint32_t value, int height)
{
    struct qjs_dom_node *node = image_receiver(ctx, object);
    if (node == NULL)
        return JS_EXCEPTION;
    char number[11];
    int length = snprintf(number, sizeof(number), "%" PRIu32, value);
    dom_string *text = NULL, *name = height ? corestring_dom_height : corestring_dom_width;
    dom_exception err = dom_string_create((const uint8_t *)number, length, &text);
    if (err == DOM_NO_ERR)
        err = dom_element_set_attribute(node->node, name, text);
    dom_string_unref(text);
    qjs_dom_changed(ctx, node, "image.dimension", name, err);
    return err == DOM_NO_ERR ? JS_UNDEFINED : qjs_dom_exception(ctx, err);
}

static JSValue image_dimension_set(JSContext *ctx, JSValueConst object, JSValueConst value, int height)
{
    if (image_receiver(ctx, object) == NULL)
        return JS_EXCEPTION;
    uint32_t number;
    if (JS_ToUint32(ctx, &number, value) < 0)
        return JS_EXCEPTION;
    /* ReflectSetter unsigned long uses the default zero outside 0..2^31-1.
     * The Image factory sets the attributes directly and has no such clamp. */
    return image_dimension_write(ctx, object, number <= INT32_MAX ? number : 0, height);
}

static JSValue image_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    uint32_t dimensions[2] = {0};
    bool present[2] = {argc > 0 && !JS_IsUndefined(argv[0]), argc > 1 && !JS_IsUndefined(argv[1])};
    for (unsigned i = 0; i < 2; i++)
        if (present[i] && JS_ToUint32(ctx, &dimensions[i], argv[i]) < 0)
            return JS_EXCEPTION;
    /* Web IDL converts arguments before GetPrototypeFromConstructor. Both may
     * execute script, so obtain/revalidate the native document afterwards. */
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto))
        return proto;
    JSValue global = JS_GetGlobalObject(ctx), document = JS_GetPropertyStr(ctx, global, "document");
    JS_FreeValue(ctx, global);
    struct qjs_dom_node *owner = JS_IsException(document) ? NULL : qjs_dom_get(ctx, document, DOM_DOCUMENT_NODE);
    dom_node *native = NULL;
    JSValue image = JS_EXCEPTION;
    if (owner != NULL) {
        dom_string *name = NULL;
        dom_exception err = dom_string_create((const uint8_t *)"img", 3, &name);
        if (err == DOM_NO_ERR)
            err = dom_document_create_element(owner->node, name, &native);
        dom_string_unref(name);
        image = err == DOM_NO_ERR ? qjs_dom_wrap(ctx, document, native) : qjs_dom_exception(ctx, err);
    }
    dom_node_unref(native);
    JS_FreeValue(ctx, document);
    if (!JS_IsException(image) && JS_IsObject(proto) && JS_SetPrototype(ctx, image, proto) < 0) {
        JS_FreeValue(ctx, image);
        image = JS_EXCEPTION;
    }
    JS_FreeValue(ctx, proto);
    for (unsigned i = 0; i < 2 && !JS_IsException(image); i++) {
        if (!present[i])
            continue;
        JSValue result = image_dimension_write(ctx, image, dimensions[i], i);
        if (JS_IsException(result)) {
            JS_FreeValue(ctx, image);
            image = JS_EXCEPTION;
        }
        JS_FreeValue(ctx, result);
    }
    return image;
}

static JSValue image_handler_get(JSContext *ctx, JSValueConst object, int kind)
{
    return image_receiver(ctx, object) == NULL ? JS_EXCEPTION : qjs_event_handler_get(ctx, object, kind);
}

static JSValue image_handler_set(JSContext *ctx, JSValueConst object, JSValueConst value, int kind)
{
    return image_receiver(ctx, object) == NULL ? JS_EXCEPTION : qjs_event_handler_set(ctx, object, value, kind);
}

int qjs_dom_install_images(JSContext *ctx, JSValue *prototypes)
{
#define IMAGE_GET(name, magic)                                                                                         \
    JS_CGETSET_MAGIC_DEF2(name, image_get, NULL, magic, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
#define IMAGE_ACCESS(name, magic)                                                                                      \
    JS_CGETSET_MAGIC_DEF2(name, image_get, image_set, magic, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
    const JSCFunctionListEntry entries[] = {IMAGE_ACCESS("src", IMAGE_SRC), IMAGE_ACCESS("srcset", IMAGE_SRCSET),
        IMAGE_ACCESS("sizes", IMAGE_SIZES), IMAGE_GET("complete", IMAGE_COMPLETE),
        IMAGE_GET("naturalWidth", IMAGE_NATURAL_WIDTH), IMAGE_GET("naturalHeight", IMAGE_NATURAL_HEIGHT),
        IMAGE_GET("currentSrc", IMAGE_CURRENT),
        JS_CGETSET_MAGIC_DEF2(
            "width", image_dimension_get, image_dimension_set, 0, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2(
            "height", image_dimension_get, image_dimension_set, 1, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2("onload", image_handler_get, image_handler_set, QJS_HANDLER_LOAD,
            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
        JS_CGETSET_MAGIC_DEF2("onerror", image_handler_get, image_handler_set, QJS_HANDLER_ERROR,
            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)};
    JSValue proto = prototypes[QJS_HTML_IMAGE_ELEMENT];
    if (qjs_define_properties(ctx, proto, entries, sizeof(entries) / sizeof(*entries)) < 0)
        return -1;
    JSValue ctor = JS_NewCFunction2(ctx, image_constructor, "Image", 0, JS_CFUNC_constructor, 0);
    if (JS_IsException(ctor))
        return -1;
    /* The factory shares HTMLImageElement.prototype without replacing that
     * prototype's constructor property with Image. */
    if (JS_DefinePropertyValueStr(ctx, ctor, "prototype", JS_DupValue(ctx, proto), 0) < 0) {
        JS_FreeValue(ctx, ctor);
        return -1;
    }
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = JS_DefinePropertyValueStr(ctx, global, "Image", ctor, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeValue(ctx, global);
    return ret;
#undef IMAGE_ACCESS
#undef IMAGE_GET
}

static JSValue image_update_job(JSContext *ctx, int argc, JSValueConst *argv)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct qjs_dom_node *node = qjs_dom_get(ctx, argv[0], DOM_ELEMENT_NODE);
    uint32_t revision;
    if (node == NULL || JS_ToUint32(ctx, &revision, argv[1]) < 0)
        return JS_EXCEPTION;
    html_image_update_at_checkpoint(thread->doc_priv, node->node, revision);
    return JS_UNDEFINED;
}

void js_queue_image_update(jsthread *thread, dom_node *node, uint32_t revision)
{
    if (thread->closed || thread->heap->execution_depth == 0)
        return;
    JSContext *ctx = thread->ctx;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue document = JS_GetPropertyStr(ctx, global, "document");
    JSValue args[] = {
        JS_IsException(document) ? JS_EXCEPTION : qjs_dom_wrap(ctx, document, node), JS_NewUint32(ctx, revision)};
    if (!JS_IsException(args[0]))
        JS_EnqueueJob(ctx, image_update_job, 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, document);
    JS_FreeValue(ctx, global);
}
