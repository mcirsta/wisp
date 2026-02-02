/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include "event_target.h"
#include <wisp/utils/log.h>
#include "quickjs.h"
#include <stdlib.h>

static JSValue js_addEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc >= 2) {
        const char *type = JS_ToCString(ctx, argv[0]);
        bool is_func = JS_IsFunction(ctx, argv[1]);
        NSLOG(wisp, INFO, "addEventListener('%s', %s)", type, is_func ? "function" : "non-function");
        JS_FreeCString(ctx, type);
        /* TODO: Store listener for later dispatch */
    }
    return JS_UNDEFINED;
}

static JSValue js_removeEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (argc >= 2) {
        const char *type = JS_ToCString(ctx, argv[0]);
        NSLOG(wisp, INFO, "removeEventListener('%s')", type);
        JS_FreeCString(ctx, type);
    }
    return JS_UNDEFINED;
}

static JSValue js_dispatchEvent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, INFO, "dispatchEvent called");
    /* Always return true - event was handled */
    return JS_NewBool(ctx, 1);
}

int qjs_init_event_target(JSContext *ctx)
{
    JSValue global_obj = JS_GetGlobalObject(ctx);

    /* Add EventTarget methods to global (window) object */
    JS_SetPropertyStr(
        ctx, global_obj, "addEventListener", JS_NewCFunction(ctx, js_addEventListener, "addEventListener", 2));
    JS_SetPropertyStr(
        ctx, global_obj, "removeEventListener", JS_NewCFunction(ctx, js_removeEventListener, "removeEventListener", 2));
    JS_SetPropertyStr(ctx, global_obj, "dispatchEvent", JS_NewCFunction(ctx, js_dispatchEvent, "dispatchEvent", 1));

    JS_FreeValue(ctx, global_obj);
    return 0;
}
