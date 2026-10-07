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
    static const JSCFunctionListEntry properties[] = {
        JS_CFUNC_DEF2("addEventListener", 2, js_addEventListener, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("removeEventListener", 2, js_removeEventListener, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("dispatchEvent", 1, js_dispatchEvent, JS_PROP_C_W_E),
    };
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = JS_SetPropertyFunctionList(ctx, global, properties, sizeof(properties) / sizeof(properties[0]));
    JS_FreeValue(ctx, global);
    return ret < 0 ? -1 : 0;
}
