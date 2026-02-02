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

static JSValue js_storage_length_getter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, 0); /* Empty storage */
}

static JSValue create_storage_object(JSContext *ctx)
{
    JSValue storage = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, storage, "getItem", JS_NewCFunction(ctx, js_storage_getItem, "getItem", 1));
    JS_SetPropertyStr(ctx, storage, "setItem", JS_NewCFunction(ctx, js_storage_setItem, "setItem", 2));
    JS_SetPropertyStr(ctx, storage, "removeItem", JS_NewCFunction(ctx, js_storage_removeItem, "removeItem", 1));
    JS_SetPropertyStr(ctx, storage, "clear", JS_NewCFunction(ctx, js_storage_clear, "clear", 0));
    JS_SetPropertyStr(ctx, storage, "key", JS_NewCFunction(ctx, js_storage_key, "key", 1));

    /* length property as getter */
    JSAtom atom = JS_NewAtom(ctx, "length");
    JS_DefinePropertyGetSet(ctx, storage, atom, JS_NewCFunction(ctx, js_storage_length_getter, "length", 0),
        JS_UNDEFINED, JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom);

    return storage;
}

int qjs_init_storage(JSContext *ctx)
{
    JSValue global_obj = JS_GetGlobalObject(ctx);

    /* Create localStorage */
    JS_SetPropertyStr(ctx, global_obj, "localStorage", create_storage_object(ctx));

    /* Create sessionStorage */
    JS_SetPropertyStr(ctx, global_obj, "sessionStorage", create_storage_object(ctx));

    JS_FreeValue(ctx, global_obj);
    return 0;
}
