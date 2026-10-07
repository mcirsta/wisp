/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include "xhr.h"
#include <wisp/utils/log.h>
#include "quickjs.h"
#include <stdlib.h>
#include <string.h>

/* XMLHttpRequest ready states */
#define XHR_UNSENT 0
#define XHR_OPENED 1
#define XHR_HEADERS_RECEIVED 2
#define XHR_LOADING 3
#define XHR_DONE 4

/* Per-instance data for XMLHttpRequest */
typedef struct {
    int ready_state;
    int status;
    char *response_text;
    char *method;
    char *url;
} XHRData;

static JSClassID xhr_class_id;

static void xhr_finalizer(JSRuntime *rt, JSValue val)
{
    XHRData *data = JS_GetOpaque(val, xhr_class_id);
    if (data) {
        if (data->response_text)
            js_free_rt(rt, data->response_text);
        if (data->method)
            js_free_rt(rt, data->method);
        if (data->url)
            js_free_rt(rt, data->url);
        js_free_rt(rt, data);
    }
}

static JSClassDef xhr_class = {
    "XMLHttpRequest",
    .finalizer = xhr_finalizer,
};

static JSValue xhr_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    XHRData *data = JS_GetOpaque2(ctx, this_val, xhr_class_id);
    if (!data)
        return JS_EXCEPTION;

    if (argc >= 2) {
        const char *method = JS_ToCString(ctx, argv[0]);
        if (method == NULL)
            return JS_EXCEPTION;
        const char *url = JS_ToCString(ctx, argv[1]);
        if (url == NULL) {
            JS_FreeCString(ctx, method);
            return JS_EXCEPTION;
        }

        NSLOG(wisp, INFO, "XHR.open('%s', '%s')", method, url);

        /* Convert and allocate before replacing the current request. A
         * throwing conversion or allocation failure leaves it untouched. */
        char *new_method = js_strdup(ctx, method);
        char *new_url = new_method != NULL ? js_strdup(ctx, url) : NULL;
        JS_FreeCString(ctx, method);
        JS_FreeCString(ctx, url);
        if (new_method == NULL || new_url == NULL) {
            js_free(ctx, new_method);
            js_free(ctx, new_url);
            return JS_EXCEPTION;
        }

        if (data->method)
            js_free(ctx, data->method);
        if (data->url)
            js_free(ctx, data->url);

        data->method = new_method;
        data->url = new_url;
        data->ready_state = XHR_OPENED;
    }
    return JS_UNDEFINED;
}

static JSValue xhr_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    XHRData *data = JS_GetOpaque2(ctx, this_val, xhr_class_id);
    if (!data)
        return JS_EXCEPTION;

    NSLOG(wisp, INFO, "XHR.send() called for %s %s", data->method ? data->method : "(null)",
        data->url ? data->url : "(null)");

    /* Simulate immediate completion for stub */
    data->ready_state = XHR_DONE;
    data->status = 200;

    /* Would trigger onreadystatechange here in real implementation */

    return JS_UNDEFINED;
}

static JSValue xhr_set_request_header(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc >= 2) {
        const char *name = JS_ToCString(ctx, argv[0]);
        const char *value = JS_ToCString(ctx, argv[1]);
        NSLOG(wisp, INFO, "XHR.setRequestHeader('%s', '%s')", name, value);
        JS_FreeCString(ctx, name);
        JS_FreeCString(ctx, value);
    }
    return JS_UNDEFINED;
}

static JSValue xhr_get_response_header(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    /* Return null - no headers in stub */
    return JS_NULL;
}

static JSValue xhr_get_all_response_headers(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return JS_NewString(ctx, "");
}

static JSValue xhr_abort(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, INFO, "XHR.abort() called");
    return JS_UNDEFINED;
}

/* Getters */
static JSValue xhr_get_ready_state(JSContext *ctx, JSValueConst this_val)
{
    XHRData *data = JS_GetOpaque2(ctx, this_val, xhr_class_id);
    if (!data)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, data->ready_state);
}

static JSValue xhr_get_status(JSContext *ctx, JSValueConst this_val)
{
    XHRData *data = JS_GetOpaque2(ctx, this_val, xhr_class_id);
    if (!data)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, data->status);
}

static JSValue xhr_get_response_text(JSContext *ctx, JSValueConst this_val)
{
    XHRData *data = JS_GetOpaque2(ctx, this_val, xhr_class_id);
    if (!data)
        return JS_EXCEPTION;
    return JS_NewString(ctx, data->response_text ? data->response_text : "");
}

static JSValue xhr_get_status_text(JSContext *ctx, JSValueConst this_val)
{
    XHRData *data = JS_GetOpaque2(ctx, this_val, xhr_class_id);
    if (!data)
        return JS_EXCEPTION;
    if (data->status == 200)
        return JS_NewString(ctx, "OK");
    return JS_NewString(ctx, "");
}

/* Constructor */
static JSValue xhr_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    JSValue obj;
    XHRData *data;

    (void)new_target; /* unused */
    (void)argc;
    (void)argv;

    data = js_mallocz(ctx, sizeof(*data));
    if (!data)
        return JS_EXCEPTION;

    data->ready_state = XHR_UNSENT;
    data->status = 0;
    data->response_text = NULL;
    data->method = NULL;
    data->url = NULL;

    /* Create object using the class proto */
    obj = JS_NewObjectClass(ctx, xhr_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, data);
        return obj;
    }

    JS_SetOpaque(obj, data);
    return obj;
}

static const JSCFunctionListEntry xhr_proto_funcs[] = {
    JS_CFUNC_DEF("open", 2, xhr_open),
    JS_CFUNC_DEF("send", 1, xhr_send),
    JS_CFUNC_DEF("setRequestHeader", 2, xhr_set_request_header),
    JS_CFUNC_DEF("getResponseHeader", 1, xhr_get_response_header),
    JS_CFUNC_DEF("getAllResponseHeaders", 0, xhr_get_all_response_headers),
    JS_CFUNC_DEF("abort", 0, xhr_abort),
    JS_CGETSET_DEF("readyState", xhr_get_ready_state, NULL),
    JS_CGETSET_DEF("status", xhr_get_status, NULL),
    JS_CGETSET_DEF("statusText", xhr_get_status_text, NULL),
    JS_CGETSET_DEF("responseText", xhr_get_response_text, NULL),
};

/* Ready state constants */
static const JSCFunctionListEntry xhr_class_props[] = {
    JS_PROP_INT32_DEF("UNSENT", XHR_UNSENT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("OPENED", XHR_OPENED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("HEADERS_RECEIVED", XHR_HEADERS_RECEIVED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LOADING", XHR_LOADING, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("DONE", XHR_DONE, JS_PROP_ENUMERABLE),
};

int qjs_init_xhr(JSContext *ctx)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JS_NewClassID(rt, &xhr_class_id);
    if (!JS_IsRegisteredClass(rt, xhr_class_id) && JS_NewClass(rt, xhr_class_id, &xhr_class) < 0) {
        return -1;
    }

    JSValue proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return -1;
    }
    if (JS_SetPropertyFunctionList(ctx, proto, xhr_proto_funcs, sizeof(xhr_proto_funcs) / sizeof(xhr_proto_funcs[0])) <
            0 ||
        JS_SetPropertyFunctionList(ctx, proto, xhr_class_props, sizeof(xhr_class_props) / sizeof(xhr_class_props[0])) <
            0) {
        JS_FreeValue(ctx, proto);
        return -1;
    }
    JS_SetClassProto(ctx, xhr_class_id, proto);

    JSValue ctor = JS_NewCFunction2(ctx, xhr_constructor, "XMLHttpRequest", 0, JS_CFUNC_constructor, 0);
    if (JS_IsException(ctor)) {
        return -1;
    }
    if (JS_SetPropertyFunctionList(ctx, ctor, xhr_class_props, sizeof(xhr_class_props) / sizeof(xhr_class_props[0])) <
        0) {
        JS_FreeValue(ctx, ctor);
        return -1;
    }
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = JS_SetPropertyStr(ctx, global, "XMLHttpRequest", ctor);
    JS_FreeValue(ctx, global);
    return ret < 0 ? -1 : 0;
}
