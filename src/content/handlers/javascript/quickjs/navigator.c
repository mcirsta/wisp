/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include "navigator.h"
#include <wisp/utils/log.h>
#include "quickjs.h"
#include <stdlib.h>

static JSValue js_navigator_cookieEnabled_getter(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewBool(ctx, 1); /* true */
}

static JSValue js_navigator_userAgent_getter(JSContext *ctx, JSValueConst this_val)
{
    /* Mimic a modern browser to satisfy Google scripts */
    return JS_NewString(ctx,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36 Wisp/1.0");
}

static JSValue js_navigator_language_getter(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewString(ctx, "en-US");
}

int qjs_init_navigator(JSContext *ctx)
{
    static const JSCFunctionListEntry properties[] = {
        JS_CGETSET_DEF2(
            "cookieEnabled", js_navigator_cookieEnabled_getter, NULL, JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE),
        JS_CGETSET_DEF2("userAgent", js_navigator_userAgent_getter, NULL, JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE),
        JS_CGETSET_DEF2("language", js_navigator_language_getter, NULL, JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE),
    };
    JSValue object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        return -1;
    }
    if (JS_SetPropertyFunctionList(ctx, object, properties, sizeof(properties) / sizeof(properties[0])) < 0) {
        JS_FreeValue(ctx, object);
        return -1;
    }
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = JS_SetPropertyStr(ctx, global, "navigator", object);
    JS_FreeValue(ctx, global);
    return ret < 0 ? -1 : 0;
}
