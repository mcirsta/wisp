/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include "timers.h"
#include <wisp/utils/log.h>
#include "quickjs.h"
#include <stdlib.h>

static JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    /* Stub implementation: just log and return a dummy ID */
    NSLOG(wisp, INFO, "setTimeout called");
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        /* In a real implementation we would schedule this.
           For now, to unblock basic scripts that use 0 delay,
           we could potentially execute it immediately or just queue it.
           But usually executing immediately breaks expectations.
           Refining to just log for now. */
    }
    return JS_NewInt32(ctx, 1);
}

static JSValue js_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, INFO, "clearTimeout called");
    return JS_UNDEFINED;
}

static JSValue js_setInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, INFO, "setInterval called");
    return JS_NewInt32(ctx, 2);
}

static JSValue js_clearInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    NSLOG(wisp, INFO, "clearInterval called");
    return JS_UNDEFINED;
}

int qjs_init_timers(JSContext *ctx)
{
    static const JSCFunctionListEntry properties[] = {
        JS_CFUNC_DEF2("setTimeout", 2, js_setTimeout, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("clearTimeout", 1, js_clearTimeout, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("setInterval", 2, js_setInterval, JS_PROP_C_W_E),
        JS_CFUNC_DEF2("clearInterval", 1, js_clearInterval, JS_PROP_C_W_E),
    };
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = JS_SetPropertyFunctionList(ctx, global, properties, sizeof(properties) / sizeof(properties[0]));
    JS_FreeValue(ctx, global);
    return ret < 0 ? -1 : 0;
}
