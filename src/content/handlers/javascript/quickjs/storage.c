/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include "storage.h"
#include <wisp/utils/log.h>
#include "quickjs.h"
#include <stdlib.h>

/* In-memory storage for stubs - simple key-value store */
/* In a real implementation, this would be persistent and per-origin */

static JSValue js_storage_getItem(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *key = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, INFO, "storage.getItem called with key: %s", key);
        JS_FreeCString(ctx, key);
    }
    /* Return null for now - item not found */
    return JS_NULL;
}

static JSValue js_storage_setItem(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc >= 2) {
        const char *key = JS_ToCString(ctx, argv[0]);
        const char *value = JS_ToCString(ctx, argv[1]);
        NSLOG(wisp, INFO, "storage.setItem called with key: %s, value: %s", key, value);
        JS_FreeCString(ctx, key);
        JS_FreeCString(ctx, value);
    }
    return JS_UNDEFINED;
}

static JSValue js_storage_removeItem(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc > 0) {
        const char *key = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, INFO, "storage.removeItem called with key: %s", key);
        JS_FreeCString(ctx, key);
    }
    return JS_UNDEFINED;
}

static JSValue js_storage_clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, INFO, "storage.clear called");
    return JS_UNDEFINED;
}

static JSValue js_storage_key(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    /* Return null - no keys at given index */
    return JS_NULL;
}

static JSValue js_storage_length_getter(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewInt32(ctx, 0); /* Empty storage */
}

static JSValue create_storage_object(JSContext *ctx)
{
    static const JSCFunctionListEntry properties[] = {
        JS_CFUNC_DEF2("getItem", 1, js_storage_getItem, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("setItem", 2, js_storage_setItem, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("removeItem", 1, js_storage_removeItem, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("clear", 0, js_storage_clear, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("key", 1, js_storage_key, JS_PROP_C_W_E),
        JS_CGETSET_DEF2("length", js_storage_length_getter, NULL, JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE),
    };
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return object;
    }
    if (JS_SetPropertyFunctionList(ctx, object, properties, sizeof(properties) / sizeof(properties[0])) < 0) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

int qjs_init_storage(JSContext *ctx)
{
    const char *names[] = {"localStorage", "sessionStorage"};
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = 0;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        JSValue object = create_storage_object(ctx);
        if (JS_IsException(object) || JS_SetPropertyStr(ctx, global, names[i], object) < 0) {
            ret = -1;
            break;
        }
    }
    JS_FreeValue(ctx, global);
    return ret;
}
