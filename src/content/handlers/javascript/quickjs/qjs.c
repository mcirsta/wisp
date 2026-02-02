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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <wisp/utils/errors.h>
#include <wisp/utils/log.h>

#include "quickjs.h"

#include "content/handlers/javascript/js.h"
#include "content/handlers/javascript/quickjs/console.h"
#include "content/handlers/javascript/quickjs/document.h"
#include "content/handlers/javascript/quickjs/event_target.h"
#include "content/handlers/javascript/quickjs/location.h"
#include "content/handlers/javascript/quickjs/navigator.h"
#include "content/handlers/javascript/quickjs/storage.h"
#include "content/handlers/javascript/quickjs/timers.h"
#include "content/handlers/javascript/quickjs/window.h"
#include "content/handlers/javascript/quickjs/xhr.h"

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


/**
 * Get the window private data from a JS context.
 *
 * This allows other QuickJS binding modules to access the browser_window
 * pointer stored in the jsthread.
 *
 * \param ctx The QuickJS context
 * \return The win_priv pointer (struct browser_window *), or NULL if
 * unavailable
 */
void *qjs_get_window_priv(JSContext *ctx)
{
    struct jsthread *t = JS_GetContextOpaque(ctx);
    if (t == NULL) {
        return NULL;
    }
    return t->win_priv;
}


/* exported interface documented in js.h */
void js_initialise(void)
{
    NSLOG(wisp, INFO, "QuickJS-ng JavaScript engine initialised");
}


/* exported interface documented in js.h */
void js_finalise(void)
{
    NSLOG(wisp, INFO, "QuickJS-ng JavaScript engine finalised");
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

    NSLOG(wisp, DEBUG, "Created QuickJS heap %p", h);

    *heap = h;
    return NSERROR_OK;
}


/* exported interface documented in js.h */
void js_destroyheap(jsheap *heap)
{
    if (heap == NULL) {
        return;
    }

    NSLOG(wisp, DEBUG, "Destroying QuickJS heap %p", heap);

    if (heap->rt != NULL) {
        JS_FreeRuntime(heap->rt);
    }

    free(heap);
}


/* exported interface documented in js.h */
nserror js_newthread(jsheap *heap, void *win_priv, void *doc_priv, jsthread **thread)
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

    /* Initialize Console binding */
    if (qjs_init_console(t->ctx) < 0) {
        NSLOG(wisp, ERROR, "Failed to initialize QuickJS console binding");
    }

    /* Initialize Window binding */
    if (qjs_init_window(t->ctx) < 0) {
        NSLOG(wisp, ERROR, "Failed to initialize QuickJS window binding");
    }

    /* Initialize Timers */
    if (qjs_init_timers(t->ctx) < 0) {
        NSLOG(wisp, ERROR, "Failed to initialize QuickJS timers");
    }

    /* Initialize Navigator */
    if (qjs_init_navigator(t->ctx) < 0) {
        NSLOG(wisp, ERROR, "Failed to initialize QuickJS navigator");
    }

    /* Initialize Location */
    if (qjs_init_location(t->ctx) < 0) {
        NSLOG(wisp, ERROR, "Failed to initialize QuickJS location");
    }

    /* Initialize Document */
    if (qjs_init_document(t->ctx) < 0) {
        NSLOG(wisp, ERROR, "Failed to initialize QuickJS document");
    }

    /* Initialize Storage (localStorage, sessionStorage) */
    if (qjs_init_storage(t->ctx) < 0) {
        NSLOG(wisp, ERROR, "Failed to initialize QuickJS storage");
    }

    /* Initialize EventTarget (addEventListener, etc.) */
    if (qjs_init_event_target(t->ctx) < 0) {
        NSLOG(wisp, ERROR, "Failed to initialize QuickJS event target");
    }

    /* Initialize XMLHttpRequest */
    if (qjs_init_xhr(t->ctx) < 0) {
        NSLOG(wisp, ERROR, "Failed to initialize QuickJS XMLHttpRequest");
    }

    /* Add DOM constructor stubs that third-party JS commonly checks */
    {
        JSValue global_obj = JS_GetGlobalObject(t->ctx);
        JSValue proto;

        /* HTMLElement constructor with prototype */
        JSValue html_element = JS_NewObject(t->ctx);
        proto = JS_NewObject(t->ctx);
        JS_SetPropertyStr(t->ctx, html_element, "prototype", proto);
        JS_SetPropertyStr(t->ctx, global_obj, "HTMLElement", html_element);

        /* Element constructor */
        JSValue element = JS_NewObject(t->ctx);
        proto = JS_NewObject(t->ctx);
        JS_SetPropertyStr(t->ctx, element, "prototype", proto);
        JS_SetPropertyStr(t->ctx, global_obj, "Element", element);

        /* Node constructor */
        JSValue node = JS_NewObject(t->ctx);
        proto = JS_NewObject(t->ctx);
        JS_SetPropertyStr(t->ctx, node, "prototype", proto);
        /* Node type constants */
        JS_SetPropertyStr(t->ctx, node, "ELEMENT_NODE", JS_NewInt32(t->ctx, 1));
        JS_SetPropertyStr(t->ctx, node, "TEXT_NODE", JS_NewInt32(t->ctx, 3));
        JS_SetPropertyStr(t->ctx, node, "DOCUMENT_NODE", JS_NewInt32(t->ctx, 9));
        JS_SetPropertyStr(t->ctx, global_obj, "Node", node);

        /* Text constructor */
        JSValue text = JS_NewObject(t->ctx);
        proto = JS_NewObject(t->ctx);
        JS_SetPropertyStr(t->ctx, text, "prototype", proto);
        JS_SetPropertyStr(t->ctx, global_obj, "Text", text);

        /* DocumentFragment constructor */
        JSValue doc_frag = JS_NewObject(t->ctx);
        proto = JS_NewObject(t->ctx);
        JS_SetPropertyStr(t->ctx, doc_frag, "prototype", proto);
        JS_SetPropertyStr(t->ctx, global_obj, "DocumentFragment", doc_frag);

        /* HTMLDocument constructor */
        JSValue html_doc = JS_NewObject(t->ctx);
        proto = JS_NewObject(t->ctx);
        JS_SetPropertyStr(t->ctx, html_doc, "prototype", proto);
        JS_SetPropertyStr(t->ctx, global_obj, "HTMLDocument", html_doc);

        /* Event constructor */
        JSValue event_ctor = JS_NewObject(t->ctx);
        proto = JS_NewObject(t->ctx);
        JS_SetPropertyStr(t->ctx, event_ctor, "prototype", proto);
        JS_SetPropertyStr(t->ctx, global_obj, "Event", event_ctor);

        /* CustomEvent constructor */
        JSValue custom_event = JS_NewObject(t->ctx);
        proto = JS_NewObject(t->ctx);
        JS_SetPropertyStr(t->ctx, custom_event, "prototype", proto);
        JS_SetPropertyStr(t->ctx, global_obj, "CustomEvent", custom_event);

        JS_FreeValue(t->ctx, global_obj);
        NSLOG(wisp, DEBUG, "DOM constructor stubs initialized");
    }

    NSLOG(wisp, DEBUG, "Created QuickJS thread %p in heap %p", t, heap);

    *thread = t;
    return NSERROR_OK;
}


/* exported interface documented in js.h */
nserror js_closethread(jsthread *thread)
{
    if (thread == NULL) {
        return NSERROR_BAD_PARAMETER;
    }

    NSLOG(wisp, DEBUG, "Closing QuickJS thread %p", thread);

    thread->closed = true;

    return NSERROR_OK;
}


/* exported interface documented in js.h */
void js_destroythread(jsthread *thread)
{
    if (thread == NULL) {
        return;
    }

    NSLOG(wisp, DEBUG, "Destroying QuickJS thread %p", thread);

    if (thread->ctx != NULL) {
        /* Execute any pending jobs before freeing context.
         * This is required by QuickJS to properly clean up Promise
         * callbacks and other async operations that hold references
         * to function objects.
         */
        JSRuntime *rt = JS_GetRuntime(thread->ctx);
        JSContext *ctx1;
        while (JS_ExecutePendingJob(rt, &ctx1) > 0) {
            /* Drain the job queue */
        }

        JS_FreeContext(thread->ctx);
    }

    free(thread);
}


/* exported interface documented in js.h */
bool js_exec(jsthread *thread, const uint8_t *txt, size_t txtlen, const char *name)
{
    JSValue result;
    bool success = true;
    char stack_buf[1024];
    char *term_txt = NULL;

    if (thread == NULL || thread->ctx == NULL || thread->closed) {
        NSLOG(wisp, WARNING, "Attempted to execute JS on invalid/closed thread");
        return false;
    }

    if (txt == NULL || txtlen == 0) {
        return true; /* Nothing to execute */
    }

    NSLOG(wisp, INFO, "Executing JS: %s (length %zu)", name ? name : "<anonymous>", txtlen);

    /* QuickJS-ng requires the input to be null-terminated at txt[txtlen] */
    if (txtlen < sizeof(stack_buf)) {
        memcpy(stack_buf, txt, txtlen);
        stack_buf[txtlen] = '\0';
        term_txt = stack_buf;
    } else {
        term_txt = malloc(txtlen + 1);
        if (term_txt == NULL) {
            NSLOG(wisp, ERROR, "Failed to allocate memory for JS execution");
            return false;
        }
        memcpy(term_txt, txt, txtlen);
        term_txt[txtlen] = '\0';
    }

    result = JS_Eval(thread->ctx, term_txt, txtlen, name ? name : "<script>", JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(thread->ctx);
        const char *exc_str = JS_ToCString(thread->ctx, exc);

        NSLOG(wisp, WARNING, "JavaScript error: %s", exc_str ? exc_str : "<unknown error>");

        if (exc_str) {
            JS_FreeCString(thread->ctx, exc_str);
        }
        JS_FreeValue(thread->ctx, exc);
        success = false;
    }

    JS_FreeValue(thread->ctx, result);

    if (term_txt != stack_buf) {
        free(term_txt);
    }

    return success;
}


/* exported interface documented in js.h */
bool js_fire_event(jsthread *thread, const char *type, struct dom_document *doc, struct dom_node *target)
{
    /* TODO: Implement event firing */
    NSLOG(wisp, DEBUG, "js_fire_event called (not yet implemented)");
    return true;
}


bool js_dom_event_add_listener(jsthread *thread, struct dom_document *document, struct dom_node *node,
    struct dom_string *event_type_dom, void *js_funcval)
{
    /* TODO: Implement event listener registration */
    NSLOG(wisp, DEBUG, "js_dom_event_add_listener called (not yet implemented)");
    return true;
}


/* exported interface documented in js.h */
void js_handle_new_element(jsthread *thread, struct dom_element *node)
{
    /* TODO: Implement new element handling */
    NSLOG(wisp, DEBUG, "js_handle_new_element called (not yet implemented)");
}


/* exported interface documented in js.h */
void js_event_cleanup(jsthread *thread, struct dom_event *evt)
{
    /* TODO: Implement event cleanup */
    NSLOG(wisp, DEBUG, "js_event_cleanup called (not yet implemented)");
}
