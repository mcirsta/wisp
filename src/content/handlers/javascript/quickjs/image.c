/* SPDX-License-Identifier: GPL-2.0-or-later
 * Reflected sources and element-owned image request state. */
#include "dom.h"
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

enum image_property { IMAGE_SRC, IMAGE_SRCSET, IMAGE_SIZES, IMAGE_COMPLETE, IMAGE_WIDTH, IMAGE_HEIGHT, IMAGE_CURRENT };

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
    case IMAGE_WIDTH:
        return JS_NewUint32(ctx, state.natural_width);
    case IMAGE_HEIGHT:
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

int qjs_dom_install_images(JSContext *ctx, JSValue *prototypes)
{
#define IMAGE_GET(name, magic)                                                                                         \
    JS_CGETSET_MAGIC_DEF2(name, image_get, NULL, magic, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
#define IMAGE_ACCESS(name, magic)                                                                                      \
    JS_CGETSET_MAGIC_DEF2(name, image_get, image_set, magic, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE)
    const JSCFunctionListEntry entries[] = {IMAGE_ACCESS("src", IMAGE_SRC), IMAGE_ACCESS("srcset", IMAGE_SRCSET),
        IMAGE_ACCESS("sizes", IMAGE_SIZES), IMAGE_GET("complete", IMAGE_COMPLETE),
        IMAGE_GET("naturalWidth", IMAGE_WIDTH), IMAGE_GET("naturalHeight", IMAGE_HEIGHT),
        IMAGE_GET("currentSrc", IMAGE_CURRENT)};
    return qjs_define_properties(ctx, prototypes[QJS_HTML_IMAGE_ELEMENT], entries, sizeof(entries) / sizeof(*entries));
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
