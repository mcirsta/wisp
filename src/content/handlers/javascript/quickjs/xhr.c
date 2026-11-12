/* SPDX-License-Identifier: GPL-2.0-or-later
 * XMLHttpRequest: bounded asynchronous same-origin HTTP GET/text support.
 * Uses the native fetch queue and EventTarget bridge; unsupported request modes
 * fail explicitly. See xhr.spec.whatwg.org for conversions and state ordering.
 */
#include "xhr_internal.h"
#include "event.h"
#include "event_target.h"
#include "content/content_protected.h"
#include "content/handlers/html/private.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct qjs_xhr *qjs_xhr_get(JSContext *ctx, JSValueConst object)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    struct qjs_xhr *xhr = thread ? JS_GetOpaque2(ctx, object, thread->heap->xhr_class) : NULL;
    if (xhr == NULL) {
        if (thread == NULL)
            JS_ThrowTypeError(ctx, "XMLHttpRequest receiver required");
        return NULL;
    }
    if (thread->closed || xhr->thread != thread) {
        qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        return NULL;
    }
    return xhr;
}

JSValue qjs_xhr_target(JSContext *ctx, JSValueConst object)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    if (thread == NULL || JS_GetOpaque(object, thread->heap->xhr_class) == NULL)
        return JS_UNDEFINED;
    struct qjs_xhr *xhr = qjs_xhr_get(ctx, object);
    return xhr ? JS_DupValue(ctx, xhr->target) : JS_EXCEPTION;
}

static void xhr_finalizer(JSRuntime *rt, JSValue object)
{
    struct qjs_xhr *xhr = JS_GetOpaque(object, JS_GetClassID(object));
    if (xhr == NULL)
        return;
    /* Active requests have an external root; close/cancel removes callbacks
     * before releasing it. A finalizer never invokes browser work or script. */
    if (xhr->previous) {
        *xhr->previous = xhr->next;
        if (xhr->next)
            xhr->next->previous = xhr->previous;
    }
    JS_FreeValueRT(rt, xhr->target);
    JS_FreeValueRT(rt, xhr->text);
    qjs_xhr_headers_free(&xhr->request_headers);
    qjs_xhr_headers_free(&xhr->response_headers);
    if (xhr->url)
        nsurl_unref(xhr->url);
    if (xhr->origin)
        nsurl_unref(xhr->origin);
    free(xhr->bytes);
    free(xhr->reason);
    js_free_rt(rt, xhr->method);
    js_free_rt(rt, xhr->mime);
    js_free_rt(rt, xhr);
}

static void xhr_mark(JSRuntime *rt, JSValueConst object, JS_MarkFunc *mark)
{
    struct qjs_xhr *xhr = JS_GetOpaque(object, JS_GetClassID(object));
    if (xhr != NULL) {
        JS_MarkValue(rt, xhr->target, mark);
        JS_MarkValue(rt, xhr->text, mark);
    }
}

static JSValue xhr_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv, int id)
{
    (void)argc;
    (void)argv;
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSValue object = qjs_event_new_object(ctx, new_target, id);
    if (JS_IsException(object))
        return object;
    if (thread == NULL || thread->closed) {
        JS_FreeValue(ctx, object);
        return qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
    }
    struct qjs_xhr *xhr = js_mallocz(ctx, sizeof(*xhr));
    if (xhr == NULL) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    xhr->thread = thread;
    xhr->object = object;
    xhr->active = xhr->text = JS_UNDEFINED;
    xhr->target = qjs_event_target_create(ctx);
    JS_SetOpaque(object, xhr);
    if (JS_IsException(xhr->target)) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    xhr->next = thread->requests;
    xhr->previous = &thread->requests;
    if (xhr->next)
        xhr->next->previous = &xhr->next;
    thread->requests = xhr;
    return object;
}

static JSValue xhr_open(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    if (qjs_xhr_get(ctx, object) == NULL)
        return JS_EXCEPTION;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "open requires method and URL");
    size_t method_length;
    char *method = qjs_xhr_bytestring(ctx, argv[0], &method_length);
    if (method == NULL)
        return JS_EXCEPTION;
    const char *url = JS_ToCString(ctx, argv[1]);
    if (url == NULL) {
        js_free(ctx, method);
        return JS_EXCEPTION;
    }
    bool async = argc < 3 || JS_ToBool(ctx, argv[2]);
    /* Convert optional credentials even though authenticated URL construction
     * is not supported yet. Getters may throw or retire this document. */
    const char *credentials[2] = {NULL, NULL};
    JSValue result = JS_EXCEPTION;
    nsurl *resolved = NULL, *without_fragment = NULL;
    for (int i = 0; i < 2 && argc > 3 + i; i++) {
        if (!JS_IsNull(argv[3 + i]) && !JS_IsUndefined(argv[3 + i])) {
            credentials[i] = JS_ToCString(ctx, argv[3 + i]);
            if (credentials[i] == NULL)
                goto out;
        }
    }
    struct qjs_xhr *xhr = qjs_xhr_get(ctx, object);
    if (xhr == NULL)
        goto out;
    if (memchr(method, 0, method_length) || !qjs_xhr_token(method)) {
        result = qjs_dom_exception(ctx, DOM_SYNTAX_ERR);
        goto out;
    }
    if (!strcasecmp(method, "CONNECT") || !strcasecmp(method, "TRACE") || !strcasecmp(method, "TRACK")) {
        result = JS_ThrowDOMException(ctx, "SecurityError", "Forbidden HTTP method");
        goto out;
    }
    const char *known[] = {"DELETE", "GET", "HEAD", "OPTIONS", "POST", "PUT"};
    for (unsigned i = 0; i < sizeof(known) / sizeof(*known); i++)
        if (!strcasecmp(method, known[i]))
            strcpy(method, known[i]);
    html_content *html = xhr->thread->doc_priv;
    nsurl *origin = html && html->base.llcache ? content_get_url(&html->base) : NULL;
    nsurl *base = html && html->base_url ? html->base_url : origin;
    nserror err = base     ? nsurl_join(base, url, &resolved)
        : strchr(url, ':') ? nsurl_create(url, &resolved)
                           : NSERROR_BAD_URL;
    if (err != NSERROR_OK || nsurl_defragment(resolved, &without_fragment) != NSERROR_OK) {
        result = qjs_dom_exception(ctx, err == NSERROR_NOMEM ? DOM_NO_MEM_ERR : DOM_SYNTAX_ERR);
        goto out;
    }
    if (!async || credentials[0] || credentials[1] || nsurl_has_component(resolved, NSURL_CREDENTIALS)) {
        result = qjs_dom_exception(ctx, DOM_NOT_SUPPORTED_ERR);
        goto out;
    }
    qjs_xhr_cancel(xhr);
    qjs_xhr_clear_response(xhr);
    qjs_xhr_headers_free(&xhr->request_headers);
    if (xhr->url)
        nsurl_unref(xhr->url);
    if (xhr->origin)
        nsurl_unref(xhr->origin);
    xhr->url = without_fragment;
    without_fragment = NULL;
    xhr->origin = origin ? nsurl_ref(origin) : NULL;
    js_free(ctx, xhr->method);
    xhr->method = method;
    method = NULL;
    qjs_trace(ctx, "xhr.open", "method=%s url=%s", xhr->method, nsurl_access(xhr->url));
    result = JS_UNDEFINED;
    if (xhr->state != 1) {
        xhr->state = 1;
        if (!qjs_xhr_event(xhr, "readystatechange"))
            result = JS_EXCEPTION;
    }
out:
    if (resolved)
        nsurl_unref(resolved);
    if (without_fragment)
        nsurl_unref(without_fragment);
    JS_FreeCString(ctx, credentials[0]);
    JS_FreeCString(ctx, credentials[1]);
    JS_FreeCString(ctx, url);
    js_free(ctx, method);
    return result;
}

static JSValue xhr_send(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    struct qjs_xhr *xhr = qjs_xhr_get(ctx, object);
    if (xhr == NULL)
        return JS_EXCEPTION;
    /* IDL conversion precedes the request algorithm even though GET discards
     * the body. Native Document/BufferSource members need no string coercion. */
    if (argc && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0])) {
        struct qjs_dom_node *node = JS_GetOpaque(argv[0], xhr->thread->heap->node_class);
        bool native = (node && node->type == DOM_DOCUMENT_NODE) || JS_IsArrayBuffer(argv[0]) ||
            JS_GetTypedArrayType(argv[0]) >= 0 || JS_IsDataView(argv[0]);
        if (!native) {
            dom_string *body = qjs_dom_usv_string(ctx, argv[0]);
            if (body == NULL)
                return JS_EXCEPTION;
            dom_string_unref(body);
        }
        xhr = qjs_xhr_get(ctx, object);
        if (xhr == NULL)
            return JS_EXCEPTION;
    }
    /* Uploads and other HTTP methods are a separate feature. */
    if (xhr->state != 1 || xhr->sending)
        return qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
    if (strcmp(xhr->method, "GET"))
        return qjs_dom_exception(ctx, DOM_NOT_SUPPORTED_ERR);
    qjs_trace(ctx, "xhr.send", "method=%s url=%s", xhr->method, nsurl_access(xhr->url));
    if (!qjs_xhr_begin(xhr))
        return JS_ThrowOutOfMemory(ctx);
    uint64_t generation = xhr->generation;
    if (!qjs_xhr_event(xhr, "loadstart")) {
        if (xhr->thread && xhr->generation == generation)
            qjs_xhr_cancel(xhr);
        return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
}

static JSValue xhr_abort(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    struct qjs_xhr *xhr = qjs_xhr_get(ctx, object);
    if (xhr == NULL)
        return JS_EXCEPTION;
    bool active = xhr->sending;
    qjs_xhr_cancel(xhr);
    uint64_t generation = xhr->generation;
    bool ok = !active || qjs_xhr_fail(xhr, "abort");
    if (xhr->thread && xhr->generation == generation && xhr->state == 4) {
        xhr->state = 0;
        qjs_xhr_clear_response(xhr);
    }
    return ok ? JS_UNDEFINED : JS_EXCEPTION;
}

static JSValue xhr_set_header(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    if (qjs_xhr_get(ctx, object) == NULL)
        return JS_EXCEPTION;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "setRequestHeader requires name and value");
    size_t name_length;
    char *name = qjs_xhr_bytestring(ctx, argv[0], &name_length);
    if (name == NULL)
        return JS_EXCEPTION;
    size_t value_length;
    char *value = qjs_xhr_bytestring(ctx, argv[1], &value_length);
    JSValue result = JS_EXCEPTION;
    if (value == NULL)
        goto out;
    struct qjs_xhr *xhr = qjs_xhr_get(ctx, object);
    if (xhr == NULL)
        goto out;
    if (xhr->state != 1 || xhr->sending) {
        result = qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        goto out;
    }
    if (memchr(name, 0, name_length) || memchr(value, 0, value_length)) {
        result = qjs_dom_exception(ctx, DOM_SYNTAX_ERR);
        goto out;
    }
    char *start = value + strspn(value, " \t\r\n");
    size_t n = strlen(start);
    while (n && strchr(" \t\r\n", start[n - 1]))
        start[--n] = 0;
    if (!qjs_xhr_token(name) || strchr(start, '\r') || strchr(start, '\n')) {
        result = qjs_dom_exception(ctx, DOM_SYNTAX_ERR);
        goto out;
    }
    if (!qjs_xhr_forbidden_header(name, start) && !qjs_xhr_header_add(&xhr->request_headers, name, start))
        result = JS_ThrowOutOfMemory(ctx);
    else
        result = JS_UNDEFINED;
out:
    js_free(ctx, name);
    js_free(ctx, value);
    return result;
}

static JSValue xhr_get_header(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    if (qjs_xhr_get(ctx, object) == NULL)
        return JS_EXCEPTION;
    if (!argc)
        return JS_ThrowTypeError(ctx, "getResponseHeader requires a name");
    size_t name_length;
    char *name = qjs_xhr_bytestring(ctx, argv[0], &name_length);
    if (name == NULL)
        return JS_EXCEPTION;
    struct qjs_xhr *xhr = qjs_xhr_get(ctx, object);
    const char *value = xhr && xhr->state >= 2 && !memchr(name, 0, name_length)
        ? qjs_xhr_header(xhr->response_headers, name)
        : NULL;
    JSValue result = !xhr ? JS_EXCEPTION : value ? qjs_xhr_from_bytes(ctx, value) : JS_NULL;
    js_free(ctx, name);
    return result;
}

static JSValue xhr_all_headers(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    struct qjs_xhr *xhr = qjs_xhr_get(ctx, object);
    if (xhr == NULL)
        return JS_EXCEPTION;
    size_t length = 0;
    struct xhr_header *headers = xhr->state >= 2 ? xhr->response_headers : NULL;
    for (struct xhr_header *h = headers; h; h = h->next)
        length += strlen(h->name) + strlen(h->value) + 4;
    char *bytes = js_malloc(ctx, length + 1), *p = bytes;
    if (bytes == NULL)
        return JS_EXCEPTION;
    for (struct xhr_header *h = headers; h; h = h->next)
        p += sprintf(p, "%s: %s\r\n", h->name, h->value);
    *p = 0;
    JSValue result = qjs_xhr_from_bytes(ctx, bytes);
    js_free(ctx, bytes);
    return result;
}

enum xhr_property { STATE, STATUS, STATUS_TEXT, RESPONSE_URL, TEXT, RESPONSE_TYPE, TIMEOUT, CREDENTIALS };
static JSValue xhr_get(JSContext *ctx, JSValueConst object, int property)
{
    struct qjs_xhr *xhr = qjs_xhr_get(ctx, object);
    if (xhr == NULL)
        return JS_EXCEPTION;
    switch (property) {
    case STATE:
        return JS_NewInt32(ctx, xhr->state);
    case STATUS:
        return JS_NewInt32(ctx, xhr->state >= 2 ? xhr->status : 0);
    case STATUS_TEXT:
        return qjs_xhr_from_bytes(ctx, xhr->state >= 2 && xhr->reason ? xhr->reason : "");
    case RESPONSE_URL:
        return JS_NewString(ctx, xhr->state >= 2 && xhr->status ? nsurl_access(xhr->url) : "");
    case TEXT:
        return qjs_xhr_text(xhr);
    case RESPONSE_TYPE:
        return JS_NewString(ctx, xhr->text_type ? "text" : "");
    case TIMEOUT:
        return JS_NewUint32(ctx, xhr->timeout);
    case CREDENTIALS:
        return JS_FALSE;
    }
    return JS_UNDEFINED;
}

static JSValue xhr_set(JSContext *ctx, JSValueConst object, JSValueConst value, int property)
{
    if (qjs_xhr_get(ctx, object) == NULL)
        return JS_EXCEPTION;
    uint32_t timeout = 0;
    const char *text = NULL;
    if (property == TIMEOUT && JS_ToUint32(ctx, &timeout, value) < 0)
        return JS_EXCEPTION;
    if (property == RESPONSE_TYPE && (text = JS_ToCString(ctx, value)) == NULL)
        return JS_EXCEPTION;
    struct qjs_xhr *xhr = qjs_xhr_get(ctx, object);
    JSValue result = JS_UNDEFINED;
    if (xhr == NULL) {
        result = JS_EXCEPTION;
    } else if (property == TIMEOUT) {
        xhr->timeout = timeout;
        if (!qjs_xhr_timeout_arm(xhr)) {
            qjs_xhr_cancel(xhr);
            result = JS_ThrowOutOfMemory(ctx);
        }
    } else if (property == CREDENTIALS) {
        if (xhr->state > 1 || xhr->sending)
            result = qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        else if (JS_ToBool(ctx, value))
            result = qjs_dom_exception(ctx, DOM_NOT_SUPPORTED_ERR);
    } else if (property == RESPONSE_TYPE) {
        bool supported = !strcmp(text, "") || !strcmp(text, "text");
        bool known = supported || !strcmp(text, "arraybuffer") || !strcmp(text, "blob") || !strcmp(text, "document") ||
            !strcmp(text, "json");
        /* Web IDL enum attribute setters ignore unknown strings before the
         * interface's state checks, including after completion. */
        if (!known) {
            JS_FreeCString(ctx, text);
            return JS_UNDEFINED;
        }
        if (xhr->state >= 3)
            result = qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        else if (supported)
            xhr->text_type = *text != 0;
        else
            result = qjs_dom_exception(ctx, DOM_NOT_SUPPORTED_ERR);
    }
    JS_FreeCString(ctx, text);
    return result;
}

static JSValue xhr_override_mime(JSContext *ctx, JSValueConst object, int argc, JSValueConst *argv)
{
    if (qjs_xhr_get(ctx, object) == NULL)
        return JS_EXCEPTION;
    if (!argc)
        return JS_ThrowTypeError(ctx, "overrideMimeType requires a MIME type");
    const char *value = JS_ToCString(ctx, argv[0]);
    if (value == NULL)
        return JS_EXCEPTION;
    struct qjs_xhr *xhr = qjs_xhr_get(ctx, object);
    JSValue result = JS_EXCEPTION;
    if (!xhr)
        goto out;
    if (xhr->state >= 3) {
        result = qjs_dom_exception(ctx, DOM_INVALID_STATE_ERR);
        goto out;
    }
    char *mime = js_strdup(ctx, value);
    if (mime != NULL) {
        js_free(ctx, xhr->mime);
        xhr->mime = mime;
        result = JS_UNDEFINED;
    }
out:
    JS_FreeCString(ctx, value);
    return result;
}

static JSValue xhr_handler_get(JSContext *ctx, JSValueConst object, int kind)
{
    return qjs_xhr_get(ctx, object) ? qjs_event_handler_get(ctx, object, kind) : JS_EXCEPTION;
}
static JSValue xhr_handler_set(JSContext *ctx, JSValueConst object, JSValueConst value, int kind)
{
    return qjs_xhr_get(ctx, object) ? qjs_event_handler_set(ctx, object, value, kind) : JS_EXCEPTION;
}
static JSValue xhr_target_constructor(JSContext *ctx, JSValueConst target, int argc, JSValueConst *argv)
{
    (void)target;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "Illegal constructor");
}

#define XHR_GET(name, property) JS_CGETSET_MAGIC_DEF2(name, xhr_get, NULL, property, JS_PROP_C_W_E)
#define XHR_SET(name, property) JS_CGETSET_MAGIC_DEF2(name, xhr_get, xhr_set, property, JS_PROP_C_W_E)
#define XHR_HANDLER(name, kind) JS_CGETSET_MAGIC_DEF2(name, xhr_handler_get, xhr_handler_set, kind, JS_PROP_C_W_E)
int qjs_init_xhr(JSContext *ctx)
{
    jsthread *thread = JS_GetContextOpaque(ctx);
    JSClassID local = 0, *id = thread ? &thread->heap->xhr_class : &local;
    if (!*id) {
        JS_NewClassID(JS_GetRuntime(ctx), id);
        JSClassDef def = {.class_name = "XMLHttpRequest", .finalizer = xhr_finalizer, .gc_mark = xhr_mark};
        if (JS_NewClass(JS_GetRuntime(ctx), *id, &def) < 0) {
            *id = 0;
            return -1;
        }
    }
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue event_proto = thread ? JS_GetClassProto(ctx, thread->heap->event_target_class) : JS_NULL;
    JSValue target_proto = JS_NewObjectProto(ctx, event_proto);
    JSValue proto = JS_NewObjectProto(ctx, target_proto);
    JSValue event_ctor = thread ? JS_GetPropertyStr(ctx, global, "EventTarget") : JS_UNDEFINED;
    JSValue target_ctor = JS_NewCFunction2(
        ctx, xhr_target_constructor, "XMLHttpRequestEventTarget", 0, JS_CFUNC_constructor, 0);
    JSValue ctor = JS_NewCFunctionMagic(ctx, xhr_constructor, "XMLHttpRequest", 0, JS_CFUNC_constructor_magic, *id);
    JS_FreeValue(ctx, event_proto);
    int ret = -1;
    if (JS_IsException(global) || JS_IsException(target_proto) || JS_IsException(proto) || JS_IsException(event_ctor) ||
        JS_IsException(target_ctor) || JS_IsException(ctor))
        goto out;
    static const JSCFunctionListEntry handlers[] = {
        XHR_HANDLER("onloadstart", QJS_HANDLER_LOADSTART),
        XHR_HANDLER("onprogress", QJS_HANDLER_PROGRESS),
        XHR_HANDLER("onabort", QJS_HANDLER_ABORT),
        XHR_HANDLER("onerror", QJS_HANDLER_ERROR),
        XHR_HANDLER("onload", QJS_HANDLER_LOAD),
        XHR_HANDLER("ontimeout", QJS_HANDLER_TIMEOUT),
        XHR_HANDLER("onloadend", QJS_HANDLER_LOADEND),
    };
    static const JSCFunctionListEntry properties[] = {
        JS_CFUNC_DEF2("open", 2, xhr_open, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("send", 0, xhr_send, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("abort", 0, xhr_abort, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("setRequestHeader", 2, xhr_set_header, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("getResponseHeader", 1, xhr_get_header, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("getAllResponseHeaders", 0, xhr_all_headers, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("overrideMimeType", 1, xhr_override_mime, JS_PROP_C_W_E),
        XHR_GET("readyState", STATE),
        XHR_GET("status", STATUS),
        XHR_GET("statusText", STATUS_TEXT),
        XHR_GET("responseURL", RESPONSE_URL),
        XHR_GET("responseText", TEXT),
        XHR_GET("response", TEXT),
        XHR_SET("responseType", RESPONSE_TYPE),
        XHR_SET("timeout", TIMEOUT),
        XHR_SET("withCredentials", CREDENTIALS),
        XHR_HANDLER("onreadystatechange", QJS_HANDLER_READYSTATE),
    };
    if (JS_SetPropertyFunctionList(ctx, target_proto, handlers, sizeof(handlers) / sizeof(*handlers)) < 0 ||
        JS_SetPropertyFunctionList(ctx, proto, properties, sizeof(properties) / sizeof(*properties)) < 0 ||
        (thread && JS_SetPrototype(ctx, target_ctor, event_ctor) < 0) ||
        qjs_event_define_interface(ctx, global, "XMLHttpRequestEventTarget", target_ctor, target_proto) < 0 ||
        JS_SetPrototype(ctx, ctor, target_ctor) < 0 ||
        qjs_event_define_interface(ctx, global, "XMLHttpRequest", ctor, proto) < 0)
        goto out;
    const char *constants[] = {"UNSENT", "OPENED", "HEADERS_RECEIVED", "LOADING", "DONE"};
    for (unsigned i = 0; i < 5; i++) {
        if (JS_DefinePropertyValueStr(ctx, ctor, constants[i], JS_NewInt32(ctx, i), JS_PROP_ENUMERABLE) < 0 ||
            JS_DefinePropertyValueStr(ctx, proto, constants[i], JS_NewInt32(ctx, i), JS_PROP_ENUMERABLE) < 0)
            goto out;
    }
    JS_SetClassProto(ctx, *id, JS_DupValue(ctx, proto));
    ret = 0;
out:
    JS_FreeValue(ctx, event_ctor);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, target_ctor);
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, target_proto);
    JS_FreeValue(ctx, global);
    return ret;
}
