/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include <stdlib.h>
#include <neosurf/utils/log.h>
#include "quickjs.h"
#include "timers.h"

static JSValue js_setTimeout(JSContext *ctx,
			     JSValueConst this_val,
			     int argc,
			     JSValueConst *argv)
{
	/* Stub implementation: just log and return a dummy ID */
	NSLOG(neosurf, INFO, "setTimeout called");
	if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
		/* In a real implementation we would schedule this.
		   For now, to unblock basic scripts that use 0 delay,
		   we could potentially execute it immediately or just queue it.
		   But usually executing immediately breaks expectations.
		   Refining to just log for now. */
	}
	return JS_NewInt32(ctx, 1);
}

static JSValue js_clearTimeout(JSContext *ctx,
			       JSValueConst this_val,
			       int argc,
			       JSValueConst *argv)
{
	NSLOG(neosurf, INFO, "clearTimeout called");
	return JS_UNDEFINED;
}

static JSValue js_setInterval(JSContext *ctx,
			      JSValueConst this_val,
			      int argc,
			      JSValueConst *argv)
{
	NSLOG(neosurf, INFO, "setInterval called");
	return JS_NewInt32(ctx, 2);
}

static JSValue js_clearInterval(JSContext *ctx,
				JSValueConst this_val,
				int argc,
				JSValueConst *argv)
{
	NSLOG(neosurf, INFO, "clearInterval called");
	return JS_UNDEFINED;
}

int qjs_init_timers(JSContext *ctx)
{
	JSValue global_obj = JS_GetGlobalObject(ctx);

	JS_SetPropertyStr(ctx,
			  global_obj,
			  "setTimeout",
			  JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));
	JS_SetPropertyStr(
		ctx,
		global_obj,
		"clearTimeout",
		JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));
	JS_SetPropertyStr(
		ctx,
		global_obj,
		"setInterval",
		JS_NewCFunction(ctx, js_setInterval, "setInterval", 2));
	JS_SetPropertyStr(
		ctx,
		global_obj,
		"clearInterval",
		JS_NewCFunction(ctx, js_clearInterval, "clearInterval", 1));

	JS_FreeValue(ctx, global_obj);
	return 0;
}
