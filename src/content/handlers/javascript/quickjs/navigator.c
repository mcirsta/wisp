/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include "navigator.h"
#include <wisp/utils/log.h>
#include "quickjs.h"
#include <stdlib.h>

static JSValue js_navigator_cookieEnabled_getter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return JS_NewBool(ctx, 1); /* true */
}

static JSValue js_navigator_userAgent_getter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    /* Mimic a modern browser to satisfy Google scripts */
    return JS_NewString(ctx,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36 Wisp/1.0");
}

static JSValue js_navigator_language_getter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return JS_NewString(ctx, "en-US");
}

static void define_getter(JSContext *ctx, JSValue obj, const char *name, JSCFunction *func)
{
    JSAtom atom = JS_NewAtom(ctx, name);
    JS_DefinePropertyGetSet(
        ctx, obj, atom, JS_NewCFunction(ctx, func, name, 0), JS_UNDEFINED, JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom);
}

int qjs_init_navigator(JSContext *ctx)
{
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue navigator = JS_NewObject(ctx);

    /* Define properties with getters */
    define_getter(ctx, navigator, "cookieEnabled", js_navigator_cookieEnabled_getter);
    define_getter(ctx, navigator, "userAgent", js_navigator_userAgent_getter);
    define_getter(ctx, navigator, "language", js_navigator_language_getter);

    /* Attach navigator to global object */
    JS_SetPropertyStr(ctx, global_obj, "navigator", navigator);

    JS_FreeValue(ctx, global_obj);
    return 0;
}
