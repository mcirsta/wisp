/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include <stdlib.h>
#include <neosurf/utils/log.h>
#include "quickjs.h"
#include "document.h"

static JSValue js_document_getElementById(JSContext *ctx,
					  JSValueConst this_val,
					  int argc,
					  JSValueConst *argv)
{
	if (argc > 0) {
		const char *id = JS_ToCString(ctx, argv[0]);
		NSLOG(neosurf,
		      INFO,
		      "document.getElementById called with: %s",
		      id);
		JS_FreeCString(ctx, id);
	}
	return JS_NULL;
}

static JSValue js_document_createElement(JSContext *ctx,
					 JSValueConst this_val,
					 int argc,
					 JSValueConst *argv)
{
	if (argc > 0) {
		const char *tag = JS_ToCString(ctx, argv[0]);
		NSLOG(neosurf,
		      INFO,
		      "document.createElement called with: %s",
		      tag);
		JS_FreeCString(ctx, tag);
	}
	/* Return a bare object for now */
	return JS_NewObject(ctx);
}

static JSValue js_document_write(JSContext *ctx,
				 JSValueConst this_val,
				 int argc,
				 JSValueConst *argv)
{
	if (argc > 0) {
		const char *str = JS_ToCString(ctx, argv[0]);
		NSLOG(neosurf, INFO, "document.write called with: %s", str);
		JS_FreeCString(ctx, str);
	}
	return JS_UNDEFINED;
}

static JSValue js_document_body_getter(JSContext *ctx,
				       JSValueConst this_val,
				       int argc,
				       JSValueConst *argv)
{
	/* Should return the body element. For now, NULL or dummy object?
	   Google JS often checks if body exists. */
	// return JS_NULL;
	/* Let's return a dummy object to allow adding properties like
	 * 'appendChild' to it without crashing */
	return JS_NewObject(ctx);
}

static JSValue js_document_documentElement_getter(JSContext *ctx,
						  JSValueConst this_val,
						  int argc,
						  JSValueConst *argv)
{
	/* Return dummy object for html element */
	return JS_NewObject(ctx);
}

static JSValue js_document_cookie_getter(JSContext *ctx,
					 JSValueConst this_val,
					 int argc,
					 JSValueConst *argv)
{
	return JS_NewString(ctx, "");
}

static JSValue js_document_cookie_setter(JSContext *ctx,
					 JSValueConst this_val,
					 int argc,
					 JSValueConst *argv)
{
	if (argc > 0) {
		const char *cookie = JS_ToCString(ctx, argv[0]);
		NSLOG(neosurf, INFO, "document.cookie set to: %s", cookie);
		JS_FreeCString(ctx, cookie);
	}
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

static void define_getter_setter(JSContext *ctx,
				 JSValue obj,
				 const char *name,
				 JSCFunction *getter,
				 JSCFunction *setter)
{
	JSAtom atom = JS_NewAtom(ctx, name);
	JS_DefinePropertyGetSet(ctx,
				obj,
				atom,
				JS_NewCFunction(ctx, getter, name, 0),
				JS_NewCFunction(ctx, setter, name, 1),
				JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
	JS_FreeAtom(ctx, atom);
}

int qjs_init_document(JSContext *ctx)
{
	JSValue global_obj = JS_GetGlobalObject(ctx);
	JSValue document = JS_NewObject(ctx);

	/* Methods */
	JS_SetPropertyStr(ctx,
			  document,
			  "getElementById",
			  JS_NewCFunction(ctx,
					  js_document_getElementById,
					  "getElementById",
					  1));
	JS_SetPropertyStr(ctx,
			  document,
			  "createElement",
			  JS_NewCFunction(ctx,
					  js_document_createElement,
					  "createElement",
					  1));
	JS_SetPropertyStr(ctx,
			  document,
			  "write",
			  JS_NewCFunction(ctx, js_document_write, "write", 1));

	/* Properties */
	define_getter(ctx, document, "body", js_document_body_getter);
	define_getter(ctx,
		      document,
		      "documentElement",
		      js_document_documentElement_getter);
	define_getter_setter(ctx,
			     document,
			     "cookie",
			     js_document_cookie_getter,
			     js_document_cookie_setter);

	/* Attach document to global object */
	JS_SetPropertyStr(ctx, global_obj, "document", document);

	JS_FreeValue(ctx, global_obj);
	return 0;
}
