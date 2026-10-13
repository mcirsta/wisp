/* SPDX-License-Identifier: GPL-2.0-or-later
 * QuickJS-owned libdom wrappers. The old Duktape bindings informed the libdom
 * calls; ownership, GC tracing and exception handling are QuickJS-native.
 */
#include "dom.h"
#include "document.h"
#include "event_target.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/utils/nsoption.h>
#include "content/handlers/html/restyle.h"

struct qjs_dom_document {
    struct qjs_dom_node base;
    JSValue cache, prototypes[QJS_DOM_PROTOTYPES];
    struct qjs_dom_node *nodes;
    uint64_t next_id;
};

static const char *const exception_names[] = {NULL, "IndexSizeError", "DOMStringSizeError", "HierarchyRequestError",
    "WrongDocumentError", "InvalidCharacterError", "NoDataAllowedError", "NoModificationAllowedError", "NotFoundError",
    "NotSupportedError", "InUseAttributeError", "InvalidStateError", "SyntaxError", "InvalidModificationError",
    "NamespaceError", "InvalidAccessError", "ValidationError", "TypeMismatchError", "SecurityError", "NetworkError",
    "AbortError", "URLMismatchError", "QuotaExceededError", "TimeoutError", "InvalidNodeTypeError", "DataCloneError"};

JSValue qjs_dom_exception(JSContext *ctx, dom_exception error)
{
    if (error == DOM_NO_MEM_ERR)
        return JS_ThrowOutOfMemory(ctx);
    const char *name = (unsigned)error < sizeof(exception_names) / sizeof(*exception_names) ? exception_names[error]
                                                                                            : NULL;
    if (name == NULL)
        return JS_ThrowInternalError(ctx, "libdom error %u", error);
    return JS_ThrowDOMException(ctx, name, "%s", name);
}

static void release_native(struct qjs_dom_node *node)
{
    if (node->class_tokens != NULL) {
        dom_tokenlist_unref(node->class_tokens);
        node->class_tokens = NULL;
    }
    dom_node_unref(node->node);
    node->node = NULL;
}

static void release_nodes(struct qjs_dom_document *document)
{
    if (document->base.node != NULL)
        dom_document_set_event_parent((dom_document *)document->base.node, NULL);
    /* Release native DOM references while the HTML owner is still alive. JS
     * wrappers can survive until GC, but closed-document operations are rejected. */
    for (struct qjs_dom_node *node = document->nodes; node != NULL; node = node->next) {
        release_native(node);
    }
    release_native(&document->base);
}

void qjs_dom_close(jsthread *thread)
{
    if (thread->dom_document != NULL)
        release_nodes(thread->dom_document);
    thread->dom_document = NULL;
    thread->dom_changes = 0;
}

static void node_finalizer(JSRuntime *rt, JSValue object)
{
    struct qjs_dom_node *node = JS_GetOpaque(object, JS_GetClassID(object));
    if (node == NULL)
        return;
    if (node->previous != NULL) {
        *node->previous = node->next;
        if (node->next != NULL)
            node->next->previous = node->previous;
    }
    if (node->type == DOM_DOCUMENT_NODE) {
        struct qjs_dom_document *doc = (struct qjs_dom_document *)node;
        release_nodes(doc);
        /* GC can finalize either side of the wrapper/document cycle first. */
        for (struct qjs_dom_node *child = doc->nodes; child != NULL; child = child->next) {
            child->previous = NULL;
            child->document = NULL;
        }
        JS_FreeValueRT(rt, doc->cache);
        for (unsigned i = 0; i < QJS_DOM_PROTOTYPES; i++)
            JS_FreeValueRT(rt, doc->prototypes[i]);
    }
    release_native(node);
    JS_FreeValueRT(rt, node->event_target);
    JS_FreeValueRT(rt, node->class_list);
    JS_FreeValueRT(rt, node->style);
    for (unsigned i = 0; i < 2; i++)
        JS_FreeValueRT(rt, node->child_lists[i]);
    JS_FreeValueRT(rt, node->owner);
    js_free_rt(rt, node);
}

static void node_mark(JSRuntime *rt, JSValueConst object, JS_MarkFunc *mark)
{
    struct qjs_dom_node *node = JS_GetOpaque(object, JS_GetClassID(object));
    if (node == NULL)
        return;
    JS_MarkValue(rt, node->owner, mark);
    JS_MarkValue(rt, node->event_target, mark);
    JS_MarkValue(rt, node->class_list, mark);
    JS_MarkValue(rt, node->style, mark);
    for (unsigned i = 0; i < 2; i++)
        JS_MarkValue(rt, node->child_lists[i], mark);
    if (node->type == DOM_DOCUMENT_NODE) {
        struct qjs_dom_document *doc = (struct qjs_dom_document *)node;
        JS_MarkValue(rt, doc->cache, mark);
        for (unsigned i = 0; i < QJS_DOM_PROTOTYPES; i++)
            JS_MarkValue(rt, doc->prototypes[i], mark);
    }
}

struct qjs_dom_node *qjs_dom_get(JSContext *ctx, JSValueConst object, dom_node_type type)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL) {
        JS_ThrowTypeError(ctx, "DOM receiver has no document context");
        return NULL;
    }
    struct qjs_dom_node *node = JS_GetOpaque2(ctx, object, thread->heap->node_class);
    if (node == NULL)
        return NULL;
    if (type != 0 && node->type != type) {
        JS_ThrowTypeError(ctx, "Incorrect DOM receiver");
        return NULL;
    }
    /* Cross-document wrappers are not exposed yet. Never let a borrowed method
     * attribute a foreign document's mutation to this document's renderer. */
    if (node->document != thread->dom_document) {
        qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        return NULL;
    }
    if (thread->closed || thread->doc_priv == NULL) {
        qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        return NULL;
    }
    if (node->type == DOM_DOCUMENT_NODE && node->node == NULL) {
        html_content *html = thread->doc_priv;
        node->node = dom_node_ref(html->document);
        if (node->node != NULL)
            qjs_events_connect_document(ctx, (dom_document *)node->node);
    }
    if (node->node == NULL) {
        qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        return NULL;
    }
    return node;
}

JSValue qjs_dom_owner(JSContext *ctx, JSValueConst object, struct qjs_dom_node *node)
{
    return JS_DupValue(ctx, node->type == DOM_DOCUMENT_NODE ? object : node->owner);
}

static bool node_namespace_is(dom_node *node, const char *name)
{
    dom_string *namespace = NULL;
    if (dom_node_get_namespace(node, &namespace) != DOM_NO_ERR)
        return false;
    bool result = namespace != NULL && dom_string_byte_length(namespace) == strlen(name) &&
        memcmp(dom_string_data(namespace), name, strlen(name)) == 0;
    dom_string_unref(namespace);
    return result;
}

bool qjs_dom_is_html(dom_node *node)
{
    return node_namespace_is(node, "http://www.w3.org/1999/xhtml");
}

bool qjs_dom_is_svg(dom_node *node)
{
    return node_namespace_is(node, "http://www.w3.org/2000/svg");
}

static void trace_node(JSContext *ctx, struct qjs_dom_node *node)
{
    if (nsoptions == NULL || !nsoption_bool(js_trace))
        return;
    dom_string *name = NULL, *id = NULL;
    dom_node_get_node_name(node->node, &name);
    if (node->type == DOM_ELEMENT_NODE && qjs_dom_is_html(node->node))
        dom_html_element_get_id(node->node, &id);
    size_t name_length = name != NULL ? dom_string_byte_length(name) : 0;
    size_t id_length = id != NULL ? dom_string_byte_length(id) : 0;
    qjs_trace(ctx, "dom.node", "node=%" PRIu64 " type=%d name=%.*s id=%.*s", node->id, node->type,
        (int)(name_length > 96 ? 96 : name_length), name != NULL ? dom_string_data(name) : "",
        (int)(id_length > 96 ? 96 : id_length), id != NULL ? dom_string_data(id) : "");
    dom_string_unref(name);
    dom_string_unref(id);
}

JSValue qjs_dom_wrap(JSContext *ctx, JSValueConst owner, dom_node *native)
{
    if (native == NULL)
        return JS_NULL;
    struct qjs_dom_node *base = qjs_dom_get(ctx, owner, DOM_DOCUMENT_NODE);
    if (base == NULL)
        return JS_EXCEPTION;
    if (native == base->node)
        return JS_DupValue(ctx, owner);
    struct qjs_dom_document *doc = (struct qjs_dom_document *)base;
    char key[2 * sizeof(uintptr_t) + 1];
    snprintf(key, sizeof(key), "%" PRIxPTR, (uintptr_t)native);
    JSValue cached = JS_GetPropertyStr(ctx, doc->cache, key);
    if (!JS_IsUndefined(cached))
        return cached;
    dom_node_type type;
    dom_exception err = dom_node_get_node_type(native, &type);
    if (err != DOM_NO_ERR)
        return qjs_dom_exception(ctx, err);
    enum qjs_dom_prototype kind = QJS_NODE;
    switch (type) {
    case DOM_ELEMENT_NODE:
        kind = qjs_dom_is_html(native) ? QJS_HTML_ELEMENT : QJS_ELEMENT;
        if (kind == QJS_ELEMENT && qjs_dom_is_svg(native))
            kind = QJS_SVG_ELEMENT;
        break;
    case DOM_TEXT_NODE:
        kind = QJS_TEXT;
        break;
    case DOM_COMMENT_NODE:
        kind = QJS_COMMENT;
        break;
    case DOM_DOCUMENT_TYPE_NODE:
        kind = QJS_DOCUMENT_TYPE;
        break;
    case DOM_DOCUMENT_FRAGMENT_NODE:
        kind = QJS_DOCUMENT_FRAGMENT;
        break;
    default:
        break;
    }
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSValue object = JS_NewObjectProtoClass(ctx, doc->prototypes[kind], thread->heap->node_class);
    if (JS_IsException(object))
        return object;
    struct qjs_dom_node *node = js_mallocz(ctx, sizeof(*node));
    if (node == NULL) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    node->node = dom_node_ref(native);
    node->type = type;
    node->id = ++doc->next_id;
    node->owner = JS_DupValue(ctx, owner);
    node->event_target = JS_UNDEFINED;
    node->class_list = node->style = JS_UNDEFINED;
    node->child_lists[0] = node->child_lists[1] = JS_UNDEFINED;
    node->document = doc;
    JS_SetOpaque(object, node);
    if (JS_SetPropertyStr(ctx, doc->cache, key, JS_DupValue(ctx, object)) < 0) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    node->next = doc->nodes;
    node->previous = &doc->nodes;
    if (node->next != NULL)
        node->next->previous = &node->next;
    doc->nodes = node;
    trace_node(ctx, node);
    return object;
}

/* Conversion helpers preserve exceptions and byte lengths (including non-ASCII
 * and embedded NULs). No safe/stringifying fallback may swallow an interrupt. */
dom_string *qjs_dom_string(JSContext *ctx, JSValueConst value, bool nullable)
{
    size_t length = 0;
    const char *text = nullable && (JS_IsNull(value) || JS_IsUndefined(value)) ? ""
                                                                               : JS_ToCStringLen(ctx, &length, value);
    if (text == NULL)
        return NULL;
    dom_string *result = NULL;
    dom_exception err = dom_string_create((const uint8_t *)text, length, &result);
    if (!(nullable && (JS_IsNull(value) || JS_IsUndefined(value))))
        JS_FreeCString(ctx, text);
    if (err != DOM_NO_ERR)
        qjs_dom_exception(ctx, err);
    return result;
}

JSValue qjs_dom_from_string(JSContext *ctx, dom_string *value, bool nullable)
{
    if (value == NULL)
        return nullable ? JS_NULL : JS_NewString(ctx, "");
    return JS_NewStringLen(ctx, dom_string_data(value), dom_string_byte_length(value));
}

void qjs_dom_changed(
    JSContext *ctx, struct qjs_dom_node *node, const char *operation, dom_string *attribute, dom_exception error)
{
    qjs_dom_changed_target(ctx, node, node->node, operation, attribute, error);
}

void qjs_dom_changed_target(JSContext *ctx, struct qjs_dom_node *node, dom_node *target,
    const char *operation, dom_string *attribute, dom_exception error)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    size_t length = attribute != NULL ? dom_string_byte_length(attribute) : 0;
    qjs_trace(ctx, "dom.mutation", "node=%" PRIu64 " type=%d operation=%s attribute=%.*s result=%u", node->id,
        node->type, operation, (int)(length > 128 ? 128 : length), attribute != NULL ? dom_string_data(attribute) : "",
        error);
    if (thread == NULL || thread->closed || thread->doc_priv == NULL)
        return;
    html_content *html = thread->doc_priv;
    bool connected = false;
    /* Fail conservatively: an allocation failure may have changed part of the
     * subtree. Detached-node edits do not affect the rendered document. */
    if (dom_node_contains(html->document, target, &connected) != DOM_NO_ERR)
        connected = true;
    if (!connected)
        return;
    html_restyle_note_dom_change(html);
    if (thread->dom_changes++ == 0)
        thread->dom_first_entry = thread->trace_entry;
}

void qjs_dom_flush(jsthread *thread)
{
    if (thread->dom_changes == 0)
        return;
    uint64_t count = thread->dom_changes;
    thread->dom_changes = 0;
    if (thread->closed || thread->doc_priv == NULL)
        return;
    html_content *html = thread->doc_priv;
    /* A parser-blocking script before conversion is already covered by the
     * initial build. Mutations during a yielded build need a following refresh. */
    bool rebuild = html->had_initial_layout || html->box_conversion_context != NULL;
    qjs_trace(thread->ctx, "render.request", "first_entry=%" PRIu64 " changes=%" PRIu64 " mode=%s",
        thread->dom_first_entry, count, rebuild ? "full-restyle" : "initial-build");
    if (rebuild)
        html_request_restyle(html);
}

static JSValue illegal_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    return JS_ThrowTypeError(ctx, "Constructor is not supported");
}

int qjs_init_document(JSContext *ctx)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSClassID local_node = 0;
    JSClassID *id = thread != NULL ? &thread->heap->node_class : &local_node;
    /* Node inherits EventTarget, including in isolated binding initialization. */
    if (qjs_init_event_target(ctx) < 0)
        return -1;
    if (*id == 0) {
        JS_NewClassID(JS_GetRuntime(ctx), id);
        JSClassDef definition = {.class_name = "Node", .finalizer = node_finalizer, .gc_mark = node_mark};
        if (JS_NewClass(JS_GetRuntime(ctx), *id, &definition) < 0) {
            *id = 0;
            return -1;
        }
    }
    JSValue object = JS_NewObjectClass(ctx, *id);
    if (JS_IsException(object))
        return -1;
    struct qjs_dom_document *doc = js_mallocz(ctx, sizeof(*doc));
    if (doc == NULL) {
        JS_FreeValue(ctx, object);
        return -1;
    }
    doc->base.type = DOM_DOCUMENT_NODE;
    doc->base.owner = JS_UNDEFINED;
    doc->base.event_target = JS_UNDEFINED;
    doc->base.class_list = doc->base.style = JS_UNDEFINED;
    doc->base.child_lists[0] = doc->base.child_lists[1] = JS_UNDEFINED;
    doc->base.id = doc->next_id = 1;
    doc->base.document = doc;
    JS_SetOpaque(object, doc);
    doc->cache = JS_NewObjectProto(ctx, JS_NULL);
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = -1;
    if (JS_IsException(doc->cache))
        goto out;
    static const struct {
        const char *name;
        int parent;
    } interfaces[] = {{"Node", -1}, {"Element", QJS_NODE}, {"HTMLElement", QJS_ELEMENT}, {"Document", QJS_NODE},
        {"HTMLDocument", QJS_DOCUMENT}, {"CharacterData", QJS_NODE}, {"Text", QJS_CHARACTER_DATA},
        {"Comment", QJS_CHARACTER_DATA}, {"DocumentType", QJS_NODE}, {"DocumentFragment", QJS_NODE}, {"SVGElement", QJS_ELEMENT}};
    for (unsigned i = 0; i < QJS_DOM_PROTOTYPES; i++) {
        JSValue parent = interfaces[i].parent < 0 ? JS_UNDEFINED : doc->prototypes[interfaces[i].parent];
        if (interfaces[i].parent < 0) {
            JSValue ctor = JS_GetPropertyStr(ctx, global, "EventTarget");
            if (JS_IsException(ctor))
                goto out;
            parent = JS_GetPropertyStr(ctx, ctor, "prototype");
            JS_FreeValue(ctx, ctor);
            if (JS_IsException(parent))
                goto out;
        }
        JSValue proto = JS_NewObjectProto(ctx, parent);
        if (interfaces[i].parent < 0)
            JS_FreeValue(ctx, parent);
        doc->prototypes[i] = proto;
        JSValue ctor = JS_NewCFunction2(ctx, illegal_constructor, interfaces[i].name, 0, JS_CFUNC_constructor, 0);
        if (JS_IsException(proto) || JS_IsException(ctor) || JS_SetConstructor(ctx, ctor, proto) < 0) {
            JS_FreeValue(ctx, ctor);
            goto out;
        }
        {
            JSValue parent_ctor = interfaces[i].parent < 0 ? JS_GetPropertyStr(ctx, global, "EventTarget")
                                                           : JS_GetPropertyStr(ctx, parent, "constructor");
            int inherited = JS_IsException(parent_ctor) ? -1 : JS_SetPrototype(ctx, ctor, parent_ctor);
            JS_FreeValue(ctx, parent_ctor);
            if (inherited < 0) {
                JS_FreeValue(ctx, ctor);
                goto out;
            }
        }
        if (JS_SetPropertyStr(ctx, global, interfaces[i].name, ctor) < 0)
            goto out;
    }
    if (qjs_init_tokenlist(ctx) < 0 || qjs_dom_install_style(ctx, doc->prototypes) < 0 || qjs_dom_install_properties(ctx, doc->prototypes) < 0 ||
        qjs_dom_install_tree(ctx, doc->prototypes) < 0 || qjs_dom_install_markup(ctx, doc->prototypes) < 0 ||
        qjs_dom_install_collections(ctx, doc->prototypes) < 0 ||
        JS_SetPrototype(ctx, object, doc->prototypes[QJS_HTML_DOCUMENT]) < 0)
        goto out;
    ret = JS_DefinePropertyValueStr(ctx, global, "document", JS_DupValue(ctx, object), JS_PROP_ENUMERABLE);
    if (ret >= 0 && thread != NULL)
        thread->dom_document = doc;
out:
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, object);
    return ret < 0 ? -1 : 0;
}
