/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#include <stdlib.h>
#include <string.h>
#include <neosurf/utils/log.h>
#include <neosurf/utils/nsurl.h>
#include <neosurf/browser_window.h>
#include "quickjs.h"
#include "location.h"

/* Defined in qjs.c */
extern void *qjs_get_window_priv(JSContext *ctx);

/**
 * Helper to get the current URL from browser window.
 * Returns NULL if unavailable.
 */
static nsurl *get_current_url(JSContext *ctx)
{
	struct browser_window *bw = qjs_get_window_priv(ctx);
	if (bw == NULL) {
		NSLOG(neosurf, DEBUG, "location: no browser window available");
		return NULL;
	}
	return browser_window_access_url(bw);
}

static JSValue js_location_href_getter(JSContext *ctx,
				       JSValueConst this_val,
				       int argc,
				       JSValueConst *argv)
{
	nsurl *url = get_current_url(ctx);
	if (url == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.href getter: returning about:blank (no URL)");
		return JS_NewString(ctx, "about:blank");
	}
	const char *href = nsurl_access(url);
	NSLOG(neosurf, DEBUG, "location.href getter: returning '%s'", href);
	return JS_NewString(ctx, href);
}

static JSValue js_location_protocol_getter(JSContext *ctx,
					   JSValueConst this_val,
					   int argc,
					   JSValueConst *argv)
{
	nsurl *url = get_current_url(ctx);
	if (url == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.protocol getter: returning 'about:' (no URL)");
		return JS_NewString(ctx, "about:");
	}
	lwc_string *scheme = nsurl_get_component(url, NSURL_SCHEME);
	if (scheme == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.protocol getter: no scheme, returning 'about:'");
		return JS_NewString(ctx, "about:");
	}
	/* Protocol includes trailing colon */
	size_t len = lwc_string_length(scheme);
	char *result = malloc(len + 2);
	if (result == NULL) {
		lwc_string_unref(scheme);
		return JS_NewString(ctx, "about:");
	}
	memcpy(result, lwc_string_data(scheme), len);
	result[len] = ':';
	result[len + 1] = '\0';
	lwc_string_unref(scheme);
	NSLOG(neosurf,
	      DEBUG,
	      "location.protocol getter: returning '%s'",
	      result);
	JSValue ret = JS_NewString(ctx, result);
	free(result);
	return ret;
}

static JSValue js_location_host_getter(JSContext *ctx,
				       JSValueConst this_val,
				       int argc,
				       JSValueConst *argv)
{
	nsurl *url = get_current_url(ctx);
	if (url == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.host getter: returning '' (no URL)");
		return JS_NewString(ctx, "");
	}
	lwc_string *host = nsurl_get_component(url, NSURL_HOST);
	lwc_string *port = nsurl_get_component(url, NSURL_PORT);

	if (host == NULL) {
		if (port)
			lwc_string_unref(port);
		NSLOG(neosurf,
		      DEBUG,
		      "location.host getter: no host, returning ''");
		return JS_NewString(ctx, "");
	}

	JSValue result;
	if (port != NULL) {
		/* host:port format */
		size_t host_len = lwc_string_length(host);
		size_t port_len = lwc_string_length(port);
		char *buf = malloc(host_len + 1 + port_len + 1);
		if (buf) {
			memcpy(buf, lwc_string_data(host), host_len);
			buf[host_len] = ':';
			memcpy(buf + host_len + 1,
			       lwc_string_data(port),
			       port_len);
			buf[host_len + 1 + port_len] = '\0';
			NSLOG(neosurf,
			      DEBUG,
			      "location.host getter: returning '%s'",
			      buf);
			result = JS_NewString(ctx, buf);
			free(buf);
		} else {
			result = JS_NewString(ctx, lwc_string_data(host));
		}
		lwc_string_unref(port);
	} else {
		NSLOG(neosurf,
		      DEBUG,
		      "location.host getter: returning '%s'",
		      lwc_string_data(host));
		result = JS_NewString(ctx, lwc_string_data(host));
	}
	lwc_string_unref(host);
	return result;
}

static JSValue js_location_hostname_getter(JSContext *ctx,
					   JSValueConst this_val,
					   int argc,
					   JSValueConst *argv)
{
	nsurl *url = get_current_url(ctx);
	if (url == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.hostname getter: returning '' (no URL)");
		return JS_NewString(ctx, "");
	}
	lwc_string *host = nsurl_get_component(url, NSURL_HOST);
	if (host == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.hostname getter: no host, returning ''");
		return JS_NewString(ctx, "");
	}
	NSLOG(neosurf,
	      DEBUG,
	      "location.hostname getter: returning '%s'",
	      lwc_string_data(host));
	JSValue result = JS_NewString(ctx, lwc_string_data(host));
	lwc_string_unref(host);
	return result;
}

static JSValue js_location_port_getter(JSContext *ctx,
				       JSValueConst this_val,
				       int argc,
				       JSValueConst *argv)
{
	nsurl *url = get_current_url(ctx);
	if (url == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.port getter: returning '' (no URL)");
		return JS_NewString(ctx, "");
	}
	lwc_string *port = nsurl_get_component(url, NSURL_PORT);
	if (port == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.port getter: no port, returning ''");
		return JS_NewString(ctx, "");
	}
	NSLOG(neosurf,
	      DEBUG,
	      "location.port getter: returning '%s'",
	      lwc_string_data(port));
	JSValue result = JS_NewString(ctx, lwc_string_data(port));
	lwc_string_unref(port);
	return result;
}

static JSValue js_location_pathname_getter(JSContext *ctx,
					   JSValueConst this_val,
					   int argc,
					   JSValueConst *argv)
{
	nsurl *url = get_current_url(ctx);
	if (url == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.pathname getter: returning '/' (no URL)");
		return JS_NewString(ctx, "/");
	}
	lwc_string *path = nsurl_get_component(url, NSURL_PATH);
	if (path == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.pathname getter: no path, returning '/'");
		return JS_NewString(ctx, "/");
	}
	NSLOG(neosurf,
	      DEBUG,
	      "location.pathname getter: returning '%s'",
	      lwc_string_data(path));
	JSValue result = JS_NewString(ctx, lwc_string_data(path));
	lwc_string_unref(path);
	return result;
}

static JSValue js_location_search_getter(JSContext *ctx,
					 JSValueConst this_val,
					 int argc,
					 JSValueConst *argv)
{
	nsurl *url = get_current_url(ctx);
	if (url == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.search getter: returning '' (no URL)");
		return JS_NewString(ctx, "");
	}
	lwc_string *query = nsurl_get_component(url, NSURL_QUERY);
	if (query == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.search getter: no query, returning ''");
		return JS_NewString(ctx, "");
	}
	/* search includes leading ? */
	size_t len = lwc_string_length(query);
	char *result = malloc(len + 2);
	if (result == NULL) {
		lwc_string_unref(query);
		return JS_NewString(ctx, "");
	}
	result[0] = '?';
	memcpy(result + 1, lwc_string_data(query), len);
	result[len + 1] = '\0';
	lwc_string_unref(query);
	NSLOG(neosurf, DEBUG, "location.search getter: returning '%s'", result);
	JSValue ret = JS_NewString(ctx, result);
	free(result);
	return ret;
}

static JSValue js_location_hash_getter(JSContext *ctx,
				       JSValueConst this_val,
				       int argc,
				       JSValueConst *argv)
{
	nsurl *url = get_current_url(ctx);
	if (url == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.hash getter: returning '' (no URL)");
		return JS_NewString(ctx, "");
	}
	lwc_string *fragment = nsurl_get_component(url, NSURL_FRAGMENT);
	if (fragment == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.hash getter: no fragment, returning ''");
		return JS_NewString(ctx, "");
	}
	/* hash includes leading # */
	size_t len = lwc_string_length(fragment);
	char *result = malloc(len + 2);
	if (result == NULL) {
		lwc_string_unref(fragment);
		return JS_NewString(ctx, "");
	}
	result[0] = '#';
	memcpy(result + 1, lwc_string_data(fragment), len);
	result[len + 1] = '\0';
	lwc_string_unref(fragment);
	NSLOG(neosurf, DEBUG, "location.hash getter: returning '%s'", result);
	JSValue ret = JS_NewString(ctx, result);
	free(result);
	return ret;
}

static JSValue js_location_origin_getter(JSContext *ctx,
					 JSValueConst this_val,
					 int argc,
					 JSValueConst *argv)
{
	nsurl *url = get_current_url(ctx);
	if (url == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.origin getter: returning 'null' (no URL)");
		return JS_NewString(ctx, "null");
	}
	lwc_string *scheme = nsurl_get_component(url, NSURL_SCHEME);
	lwc_string *host = nsurl_get_component(url, NSURL_HOST);
	lwc_string *port = nsurl_get_component(url, NSURL_PORT);

	if (scheme == NULL || host == NULL) {
		if (scheme)
			lwc_string_unref(scheme);
		if (host)
			lwc_string_unref(host);
		if (port)
			lwc_string_unref(port);
		NSLOG(neosurf,
		      DEBUG,
		      "location.origin getter: missing scheme/host, returning 'null'");
		return JS_NewString(ctx, "null");
	}

	/* Format: scheme://host[:port] */
	size_t scheme_len = lwc_string_length(scheme);
	size_t host_len = lwc_string_length(host);
	size_t port_len = port ? lwc_string_length(port) : 0;
	size_t total = scheme_len + 3 + host_len + (port ? 1 + port_len : 0) +
		       1;

	char *result = malloc(total);
	if (result == NULL) {
		lwc_string_unref(scheme);
		lwc_string_unref(host);
		if (port)
			lwc_string_unref(port);
		return JS_NewString(ctx, "null");
	}

	char *p = result;
	memcpy(p, lwc_string_data(scheme), scheme_len);
	p += scheme_len;
	memcpy(p, "://", 3);
	p += 3;
	memcpy(p, lwc_string_data(host), host_len);
	p += host_len;
	if (port) {
		*p++ = ':';
		memcpy(p, lwc_string_data(port), port_len);
		p += port_len;
	}
	*p = '\0';

	lwc_string_unref(scheme);
	lwc_string_unref(host);
	if (port)
		lwc_string_unref(port);

	NSLOG(neosurf, DEBUG, "location.origin getter: returning '%s'", result);
	JSValue ret = JS_NewString(ctx, result);
	free(result);
	return ret;
}

static JSValue js_location_assign(JSContext *ctx,
				  JSValueConst this_val,
				  int argc,
				  JSValueConst *argv)
{
	struct browser_window *bw;
	const char *url_str;
	nsurl *url;
	nserror err;

	if (argc < 1) {
		NSLOG(neosurf, DEBUG, "location.assign: no argument provided");
		return JS_UNDEFINED;
	}

	url_str = JS_ToCString(ctx, argv[0]);
	if (url_str == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.assign: failed to get URL string");
		return JS_UNDEFINED;
	}

	NSLOG(neosurf, DEBUG, "location.assign called with: '%s'", url_str);

	bw = qjs_get_window_priv(ctx);
	if (bw == NULL) {
		NSLOG(neosurf,
		      WARNING,
		      "location.assign: no browser window available");
		JS_FreeCString(ctx, url_str);
		return JS_UNDEFINED;
	}

	err = nsurl_create(url_str, &url);
	JS_FreeCString(ctx, url_str);

	if (err != NSERROR_OK) {
		NSLOG(neosurf,
		      WARNING,
		      "location.assign: failed to create URL");
		return JS_UNDEFINED;
	}

	/* Navigate to the new URL, adding to history */
	err = browser_window_navigate(bw,
				      url,
				      NULL, /* referrer */
				      BW_NAVIGATE_HISTORY |
					      BW_NAVIGATE_UNVERIFIABLE,
				      NULL, /* post_urlenc */
				      NULL, /* post_multipart */
				      NULL); /* parent */

	nsurl_unref(url);

	if (err != NSERROR_OK) {
		NSLOG(neosurf, WARNING, "location.assign: navigation failed");
	}

	return JS_UNDEFINED;
}

static JSValue js_location_replace(JSContext *ctx,
				   JSValueConst this_val,
				   int argc,
				   JSValueConst *argv)
{
	struct browser_window *bw;
	const char *url_str;
	nsurl *url;
	nserror err;

	if (argc < 1) {
		NSLOG(neosurf, DEBUG, "location.replace: no argument provided");
		return JS_UNDEFINED;
	}

	url_str = JS_ToCString(ctx, argv[0]);
	if (url_str == NULL) {
		NSLOG(neosurf,
		      DEBUG,
		      "location.replace: failed to get URL string");
		return JS_UNDEFINED;
	}

	NSLOG(neosurf, DEBUG, "location.replace called with: '%s'", url_str);

	bw = qjs_get_window_priv(ctx);
	if (bw == NULL) {
		NSLOG(neosurf,
		      WARNING,
		      "location.replace: no browser window available");
		JS_FreeCString(ctx, url_str);
		return JS_UNDEFINED;
	}

	err = nsurl_create(url_str, &url);
	JS_FreeCString(ctx, url_str);

	if (err != NSERROR_OK) {
		NSLOG(neosurf,
		      WARNING,
		      "location.replace: failed to create URL");
		return JS_UNDEFINED;
	}

	/* Navigate to the new URL, replacing history */
	err = browser_window_navigate(bw,
				      url,
				      NULL, /* referrer */
				      BW_NAVIGATE_HISTORY |
					      BW_NAVIGATE_UNVERIFIABLE,
				      NULL, /* post_urlenc */
				      NULL, /* post_multipart */
				      NULL); /* parent */

	nsurl_unref(url);

	if (err != NSERROR_OK) {
		NSLOG(neosurf, WARNING, "location.replace: navigation failed");
	}

	return JS_UNDEFINED;
}

static JSValue js_location_reload(JSContext *ctx,
				  JSValueConst this_val,
				  int argc,
				  JSValueConst *argv)
{
	NSLOG(neosurf, DEBUG, "location.reload called");
	/* TODO: Implement actual reload */
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
	NSLOG(neosurf, DEBUG, "Initializing location binding");

	JSValue global_obj = JS_GetGlobalObject(ctx);
	JSValue location = JS_NewObject(ctx);

	/* URL properties (implements URLUtils) */
	define_getter(ctx, location, "href", js_location_href_getter);
	define_getter(ctx, location, "protocol", js_location_protocol_getter);
	define_getter(ctx, location, "host", js_location_host_getter);
	define_getter(ctx, location, "hostname", js_location_hostname_getter);
	define_getter(ctx, location, "port", js_location_port_getter);
	define_getter(ctx, location, "pathname", js_location_pathname_getter);
	define_getter(ctx, location, "search", js_location_search_getter);
	define_getter(ctx, location, "hash", js_location_hash_getter);
	define_getter(ctx, location, "origin", js_location_origin_getter);

	/* Methods */
	JS_SetPropertyStr(
		ctx,
		location,
		"assign",
		JS_NewCFunction(ctx, js_location_assign, "assign", 1));
	JS_SetPropertyStr(
		ctx,
		location,
		"replace",
		JS_NewCFunction(ctx, js_location_replace, "replace", 1));
	JS_SetPropertyStr(
		ctx,
		location,
		"reload",
		JS_NewCFunction(ctx, js_location_reload, "reload", 0));

	/* Attach location to global object */
	JS_SetPropertyStr(ctx, global_obj, "location", location);

	JS_FreeValue(ctx, global_obj);

	NSLOG(neosurf,
	      DEBUG,
	      "Location binding initialized with all URL properties");
	return 0;
}
