/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include <stdlib.h>
#include <neosurf/utils/log.h>
#include "quickjs.h"
#include "location.h"

static JSValue js_location_href_getter(JSContext *ctx,
				       JSValueConst this_val,
				       int argc,
				       JSValueConst *argv)
{
	/* TODO: Retrieve actual URL from thread/window private data */
	return JS_NewString(ctx, "about:blank");
}

static JSValue js_location_protocol_getter(JSContext *ctx,
					   JSValueConst this_val,
					   int argc,
					   JSValueConst *argv)
{
	return JS_NewString(ctx, "https:");
}

static JSValue js_location_replace(JSContext *ctx,
				   JSValueConst this_val,
				   int argc,
				   JSValueConst *argv)
{
	if (argc > 0) {
		const char *url = JS_ToCString(ctx, argv[0]);
		NSLOG(neosurf, INFO, "location.replace called with: %s", url);
		JS_FreeCString(ctx, url);
	}
	return JS_UNDEFINED;
}

static JSValue js_location_reload(JSContext *ctx,
				  JSValueConst this_val,
				  int argc,
				  JSValueConst *argv)
{
	NSLOG(neosurf, INFO, "location.reload called");
	return JS_UNDEFINED;
}

static void
define_getter(JSContext *ctx, JSValue obj, const char *name, JSCFunction *func)
{
	JSAtom atom = JS_NewAtom(ctx, name);
	JS_DefinePropertyGetSet(ctx,
				obj,
				atom,
				JS_NewCFunction(ctx, func, name, 0),
				JS_UNDEFINED,
				JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
	JS_FreeAtom(ctx, atom);
}

int qjs_init_location(JSContext *ctx)
{
	JSValue global_obj = JS_GetGlobalObject(ctx);
	JSValue location = JS_NewObject(ctx);

	/* href */
	define_getter(ctx, location, "href", js_location_href_getter);

	/* protocol */
	define_getter(ctx, location, "protocol", js_location_protocol_getter);

	/* replace */
	JS_SetPropertyStr(
		ctx,
		location,
		"replace",
		JS_NewCFunction(ctx, js_location_replace, "replace", 1));

	/* reload */
	JS_SetPropertyStr(
		ctx,
		location,
		"reload",
		JS_NewCFunction(ctx, js_location_reload, "reload", 0));

	/* Attach location to global object */
	JS_SetPropertyStr(ctx, global_obj, "location", location);

	/* Also attach to window.location */
	/* Note: In a real browser, window.location and document.location are
	   complex info objects. We just create a new reference here for now. */
	// JS_SetPropertyStr(ctx, global_obj, "location", JS_DupValue(ctx,
	// location));
	/* The above line overwrites the previous one. We want window.location
	   to be available. The previous line attached 'location' to global,
	   which IS window. So window.location is already set. */

	JS_FreeValue(ctx, global_obj);
	return 0;
}
