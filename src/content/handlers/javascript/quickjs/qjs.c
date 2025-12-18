/*
 * Copyright 2024 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 *
 * NeoSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NeoSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * QuickJS-ng implementation of JavaScript engine functions.
 *
 * This implements the js.h interface using the QuickJS-ng engine.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <neosurf/utils/errors.h>
#include <neosurf/utils/log.h>

#include "quickjs.h"

#include "content/handlers/javascript/js.h"

/**
 * JavaScript heap structure.
 *
 * Maps to QuickJS's JSRuntime - one per browser window.
 */
struct jsheap {
	JSRuntime *rt;
	int timeout;
};

/**
 * JavaScript thread structure.
 *
 * Maps to QuickJS's JSContext - one per browsing context.
 */
struct jsthread {
	jsheap *heap;
	JSContext *ctx;
	bool closed;
	void *win_priv;
	void *doc_priv;
};


/* exported interface documented in js.h */
void js_initialise(void)
{
	NSLOG(neosurf, INFO, "QuickJS-ng JavaScript engine initialised");
}


/* exported interface documented in js.h */
void js_finalise(void)
{
	NSLOG(neosurf, INFO, "QuickJS-ng JavaScript engine finalised");
}


/* exported interface documented in js.h */
nserror js_newheap(int timeout, jsheap **heap)
{
	jsheap *h;

	h = calloc(1, sizeof(*h));
	if (h == NULL) {
		return NSERROR_NOMEM;
	}

	h->rt = JS_NewRuntime();
	if (h->rt == NULL) {
		free(h);
		return NSERROR_NOMEM;
	}

	h->timeout = timeout;

	/* Set a reasonable memory limit (64MB default) */
	JS_SetMemoryLimit(h->rt, 64 * 1024 * 1024);

	/* Set max stack size (1MB) */
	JS_SetMaxStackSize(h->rt, 1024 * 1024);

	NSLOG(neosurf, DEBUG, "Created QuickJS heap %p", h);

	*heap = h;
	return NSERROR_OK;
}


/* exported interface documented in js.h */
void js_destroyheap(jsheap *heap)
{
	if (heap == NULL) {
		return;
	}

	NSLOG(neosurf, DEBUG, "Destroying QuickJS heap %p", heap);

	if (heap->rt != NULL) {
		JS_FreeRuntime(heap->rt);
	}

	free(heap);
}


/* exported interface documented in js.h */
nserror
js_newthread(jsheap *heap, void *win_priv, void *doc_priv, jsthread **thread)
{
	jsthread *t;

	if (heap == NULL) {
		return NSERROR_BAD_PARAMETER;
	}

	t = calloc(1, sizeof(*t));
	if (t == NULL) {
		return NSERROR_NOMEM;
	}

	/* JS_NewContext creates a context with all standard intrinsics */
	t->ctx = JS_NewContext(heap->rt);
	if (t->ctx == NULL) {
		free(t);
		return NSERROR_NOMEM;
	}

	t->heap = heap;
	t->win_priv = win_priv;
	t->doc_priv = doc_priv;
	t->closed = false;

	/* Store thread pointer in context for later retrieval */
	JS_SetContextOpaque(t->ctx, t);

	NSLOG(neosurf, DEBUG, "Created QuickJS thread %p in heap %p", t, heap);

	*thread = t;
	return NSERROR_OK;
}


/* exported interface documented in js.h */
nserror js_closethread(jsthread *thread)
{
	if (thread == NULL) {
		return NSERROR_BAD_PARAMETER;
	}

	NSLOG(neosurf, DEBUG, "Closing QuickJS thread %p", thread);

	thread->closed = true;

	return NSERROR_OK;
}


/* exported interface documented in js.h */
void js_destroythread(jsthread *thread)
{
	if (thread == NULL) {
		return;
	}

	NSLOG(neosurf, DEBUG, "Destroying QuickJS thread %p", thread);

	if (thread->ctx != NULL) {
		JS_FreeContext(thread->ctx);
	}

	free(thread);
}


/* exported interface documented in js.h */
bool js_exec(jsthread *thread,
	     const uint8_t *txt,
	     size_t txtlen,
	     const char *name)
{
	JSValue result;
	bool success = true;

	if (thread == NULL || thread->ctx == NULL || thread->closed) {
		NSLOG(neosurf,
		      WARNING,
		      "Attempted to execute JS on invalid/closed thread");
		return false;
	}

	if (txt == NULL || txtlen == 0) {
		return true; /* Nothing to execute */
	}

	NSLOG(neosurf,
	      DEBUG,
	      "Executing JS: %s (length %zu)",
	      name ? name : "<anonymous>",
	      txtlen);

	result = JS_Eval(thread->ctx,
			 (const char *)txt,
			 txtlen,
			 name ? name : "<script>",
			 JS_EVAL_TYPE_GLOBAL);

	if (JS_IsException(result)) {
		JSValue exc = JS_GetException(thread->ctx);
		const char *exc_str = JS_ToCString(thread->ctx, exc);

		NSLOG(neosurf,
		      WARNING,
		      "JavaScript error: %s",
		      exc_str ? exc_str : "<unknown error>");

		if (exc_str) {
			JS_FreeCString(thread->ctx, exc_str);
		}
		JS_FreeValue(thread->ctx, exc);
		success = false;
	}

	JS_FreeValue(thread->ctx, result);

	return success;
}


/* exported interface documented in js.h */
bool js_fire_event(jsthread *thread,
		   const char *type,
		   struct dom_document *doc,
		   struct dom_node *target)
{
	/* TODO: Implement event firing */
	NSLOG(neosurf, DEBUG, "js_fire_event called (not yet implemented)");
	return true;
}


bool js_dom_event_add_listener(jsthread *thread,
			       struct dom_document *document,
			       struct dom_node *node,
			       struct dom_string *event_type_dom,
			       void *js_funcval)
{
	/* TODO: Implement event listener registration */
	NSLOG(neosurf,
	      DEBUG,
	      "js_dom_event_add_listener called (not yet implemented)");
	return true;
}


/* exported interface documented in js.h */
void js_handle_new_element(jsthread *thread, struct dom_element *node)
{
	/* TODO: Implement new element handling */
	NSLOG(neosurf,
	      DEBUG,
	      "js_handle_new_element called (not yet implemented)");
}


/* exported interface documented in js.h */
void js_event_cleanup(jsthread *thread, struct dom_event *evt)
{
	/* TODO: Implement event cleanup */
	NSLOG(neosurf, DEBUG, "js_event_cleanup called (not yet implemented)");
}
