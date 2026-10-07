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
#include <assert.h>
#include <nsutils/time.h>

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
 * Reference counted: the browser_window holds one reference (the owner),
 * and each jsthread (JSContext) holds one reference. JS_FreeRuntime is
 * only called when the last reference is dropped, ensuring all contexts
 * are freed before the runtime.
 */
struct jsheap {
    JSRuntime *rt;
    int timeout;
    int refcount;
    unsigned execution_depth;
    uint64_t deadline_ms;
    bool interrupted;
};

/**
 * JavaScript thread structure.
 *
 * Maps to QuickJS's JSContext - one per document.
 */
struct jsthread {
    jsheap *heap;
    JSContext *ctx;
    JSValue error_stack_getter;
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
    if (t == NULL || t->closed) {
        return NULL;
    }
    return t->win_priv;
}


/** Global count of live jsheaps, for leak detection at shutdown. */
static int jsheap_live_count = 0;

/* QuickJS calls this periodically during execution. Never pump the event loop
 * here: rendering and document destruction must not reenter through a timeout. */
static int qjs_interrupt(JSRuntime *rt, void *opaque)
{
    jsheap *heap = opaque;
    uint64_t now;

    (void)rt;
    if (heap->execution_depth == 0 || heap->timeout <= 0) {
        return 0;
    }
    if (heap->interrupted || nsu_getmonotonic_ms(&now) != NSUERROR_OK || now >= heap->deadline_ms) {
        heap->interrupted = true;
        return 1;
    }
    return 0;
}

static bool qjs_execution_begin(jsheap *heap)
{
    /* Nested host entries share the outer budget. Future callback/microtask
     * checkpoints must likewise remain inside a single execution scope. */
    if (heap->execution_depth++ == 0) {
        heap->interrupted = false;
        if (heap->timeout > 0) {
            uint64_t now;
            if (nsu_getmonotonic_ms(&now) != NSUERROR_OK) {
                heap->interrupted = true;
            } else {
                heap->deadline_ms = now + (uint64_t)heap->timeout * 1000;
            }
        }
    }
    return !heap->interrupted;
}

/* Inspect only string-valued own data properties of actual Error objects.
 * Coercing arbitrary thrown objects or invoking a stack getter can execute page
 * code, including another infinite loop, while reporting the original failure. */
static void qjs_log_error_property(JSContext *ctx, JSValueConst error, const char *property)
{
    JSPropertyDescriptor desc;
    JSAtom atom = JS_NewAtom(ctx, property);
    if (atom == JS_ATOM_NULL) {
        return;
    }
    if (JS_GetOwnProperty(ctx, &desc, error, atom) > 0) {
        if (JS_IsString(desc.value)) {
            const char *text = JS_ToCString(ctx, desc.value);
            if (text != NULL) {
                NSLOG(wisp, WARNING, "JavaScript %s: %s", property, text);
                JS_FreeCString(ctx, text);
            }
        }
        JS_FreeValue(ctx, desc.value);
        JS_FreeValue(ctx, desc.getter);
        JS_FreeValue(ctx, desc.setter);
    }
    JS_FreeAtom(ctx, atom);
}

/* QuickJS-ng 0.17 stores its backtrace internally and exposes it through this
 * native accessor. Retain the pristine function before any page code runs;
 * calling it directly bypasses user getters, proxies and prototype changes. */
static bool qjs_capture_stack_getter(jsthread *thread)
{
    JSContext *ctx = thread->ctx;
    JSValue error = JS_NewError(ctx);
    if (JS_IsException(error)) {
        return false;
    }
    JSValue proto = JS_GetPrototype(ctx, error);
    JSAtom atom = JS_NewAtom(ctx, "stack");
    JSPropertyDescriptor desc;
    bool ok = false;
    if (!JS_IsException(proto) && atom != JS_ATOM_NULL && JS_GetOwnProperty(ctx, &desc, proto, atom) > 0) {
        if (JS_IsFunction(ctx, desc.getter)) {
            thread->error_stack_getter = JS_DupValue(ctx, desc.getter);
            ok = true;
        }
        JS_FreeValue(ctx, desc.value);
        JS_FreeValue(ctx, desc.getter);
        JS_FreeValue(ctx, desc.setter);
    }
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, error);
    return ok && !JS_HasException(ctx);
}

static void qjs_report_exception(jsthread *thread, const char *name, bool interrupted)
{
    JSContext *ctx = thread->ctx;
    JSValue exc = JS_GetException(ctx);

    NSLOG(wisp, WARNING, "JavaScript %s in %s", interrupted ? "execution limit reached" : "exception", name);
    if (JS_IsError(exc)) {
        qjs_log_error_property(ctx, exc, "message");
        if (!JS_IsUndefined(thread->error_stack_getter)) {
            JSValue stack = JS_Call(ctx, thread->error_stack_getter, exc, 0, NULL);
            if (JS_IsString(stack)) {
                const char *text = JS_ToCString(ctx, stack);
                if (text != NULL) {
                    NSLOG(wisp, WARNING, "JavaScript stack: %s", text);
                    JS_FreeCString(ctx, text);
                }
            }
            JS_FreeValue(ctx, stack);
        }
    } else if (JS_IsString(exc)) {
        const char *text = JS_ToCString(ctx, exc);
        if (text != NULL) {
            NSLOG(wisp, WARNING, "JavaScript thrown string: %s", text);
            JS_FreeCString(ctx, text);
        }
    }
    JS_FreeValue(ctx, exc);
    /* Diagnostic allocation failure must not leave an exception for the next
     * script. Arbitrary thrown objects are deliberately left unformatted. */
    if (JS_HasException(ctx)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
    }
}

/**
 * Drop one reference to a jsheap. When the refcount reaches zero,
 * the QuickJS runtime is freed and the heap struct is deallocated.
 */
static void jsheap_unref(jsheap *heap)
{
    if (heap == NULL) {
        return;
    }

    heap->refcount--;
    NSLOG(wisp, DEBUG, "jsheap %p unref -> refcount=%d", heap, heap->refcount);

    if (heap->refcount < 0) {
        NSLOG(wisp, ERROR, "jsheap %p refcount went negative (%d) - this is a bug!", heap, heap->refcount);
    }

    if (heap->refcount <= 0) {
        NSLOG(wisp, DEBUG, "jsheap %p refcount reached zero, freeing JSRuntime", heap);
        if (heap->rt != NULL) {
            JS_FreeRuntime(heap->rt);
        }
        jsheap_live_count--;
        free(heap);
    }
}


/* exported interface documented in js.h */
void js_initialise(void)
{
    NSLOG(wisp, INFO, "QuickJS-ng JavaScript engine initialised");
}


/* exported interface documented in js.h */
void js_finalise(void)
{
    if (jsheap_live_count != 0) {
        NSLOG(wisp, ERROR, "JS ENGINE LEAK: %d jsheap(s) still alive at shutdown!", jsheap_live_count);
    }
    assert(jsheap_live_count == 0);
    NSLOG(wisp, INFO, "QuickJS-ng JavaScript engine finalised");
}


/* exported interface documented in js.h */
nserror js_newheap(int timeout, jsheap **heap)
{
    jsheap *h;

    if (heap == NULL) {
        return NSERROR_BAD_PARAMETER;
    }
    *heap = NULL;
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
    h->refcount = 1; /* Owner (browser_window) holds the initial reference */

    /* Set a reasonable memory limit (64MB default) */
    JS_SetMemoryLimit(h->rt, 64 * 1024 * 1024);

    /* Set max stack size (1MB) */
    JS_SetMaxStackSize(h->rt, 1024 * 1024);
    JS_SetInterruptHandler(h->rt, qjs_interrupt, h);

    jsheap_live_count++;
    NSLOG(wisp, DEBUG, "Created QuickJS heap %p (refcount=%d, live_heaps=%d)", h, h->refcount, jsheap_live_count);

    *heap = h;
    return NSERROR_OK;
}


/* exported interface documented in js.h */
void js_destroyheap(jsheap *heap)
{
    if (heap == NULL) {
        return;
    }

    if (heap->refcount > 1) {
        NSLOG(wisp, WARNING,
            "js_destroyheap: heap %p still has %d JS context(s) alive. "
            "Runtime destruction deferred until all contexts are freed.",
            heap, heap->refcount - 1);
    }

    NSLOG(wisp, DEBUG, "js_destroyheap: dropping owner reference for heap %p", heap);
    jsheap_unref(heap);
}


/* These are still API-shape stubs, not libdom constructors. */
static int qjs_init_dom_stubs(JSContext *ctx)
{
    static const char *names[] = {
        "HTMLElement", "Element", "Node", "Text", "DocumentFragment", "HTMLDocument", "Event", "CustomEvent"};
    static const JSCFunctionListEntry node_constants[] = {
        JS_PROP_INT32_DEF("ELEMENT_NODE", 1, JS_PROP_C_W_E),
        JS_PROP_INT32_DEF("TEXT_NODE", 3, JS_PROP_C_W_E),
        JS_PROP_INT32_DEF("DOCUMENT_NODE", 9, JS_PROP_C_W_E),
    };
    JSValue global = JS_GetGlobalObject(ctx);
    int ret = -1;

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        JSValue object = JS_NewObject(ctx);
        if (JS_IsException(object)) {
            goto out;
        }
        JSValue proto = JS_NewObject(ctx);
        if (JS_IsException(proto) || JS_SetPropertyStr(ctx, object, "prototype", proto) < 0 ||
            (strcmp(names[i], "Node") == 0 &&
                JS_SetPropertyFunctionList(
                    ctx, object, node_constants, sizeof(node_constants) / sizeof(node_constants[0])) < 0)) {
            JS_FreeValue(ctx, object);
            goto out;
        }
        if (JS_SetPropertyStr(ctx, global, names[i], object) < 0) {
            goto out;
        }
    }
    ret = 0;
out:
    JS_FreeValue(ctx, global);
    return ret;
}


/* exported interface documented in js.h */
nserror js_newthread(jsheap *heap, void *win_priv, void *doc_priv, jsthread **thread)
{
    jsthread *t;

    if (thread == NULL) {
        return NSERROR_BAD_PARAMETER;
    }
    *thread = NULL;
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
    t->error_stack_getter = JS_UNDEFINED;
    t->win_priv = win_priv;
    t->doc_priv = doc_priv;
    t->closed = false;

    /* Take a reference on the heap so it outlives this context */
    heap->refcount++;
    NSLOG(wisp, DEBUG, "jsheap %p ref (new thread) -> refcount=%d", heap, heap->refcount);

    /* Store thread pointer in context for later retrieval */
    JS_SetContextOpaque(t->ctx, t);

    if (!qjs_capture_stack_getter(t)) {
        qjs_report_exception(t, "error stack initialization", false);
        js_destroythread(t);
        return NSERROR_INIT_FAILED;
    }

    static const struct {
        const char *name;
        int (*init)(JSContext *ctx);
    } bindings[] = {
        {"console", qjs_init_console},
        {"window", qjs_init_window},
        {"timers", qjs_init_timers},
        {"navigator", qjs_init_navigator},
        {"location", qjs_init_location},
        {"document", qjs_init_document},
        {"storage", qjs_init_storage},
        {"event target", qjs_init_event_target},
        {"XMLHttpRequest", qjs_init_xhr},
        {"DOM constructor stubs", qjs_init_dom_stubs},
    };
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
        if (bindings[i].init(t->ctx) < 0 || JS_HasException(t->ctx)) {
            NSLOG(wisp, ERROR, "Failed to initialize QuickJS %s", bindings[i].name);
            if (JS_HasException(t->ctx)) {
                qjs_report_exception(t, bindings[i].name, false);
            }
            js_destroythread(t);
            return NSERROR_INIT_FAILED;
        }
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
    thread->win_priv = NULL;
    thread->doc_priv = NULL;
    if (thread->ctx != NULL) {
        /* Closing a document must not execute Promise callbacks: they can
         * navigate while the browser is still replacing its content. Old
         * and new documents share a runtime, so close only our own queue. */
        JS_CloseContextJobs(thread->ctx);
    }

    return NSERROR_OK;
}


/* exported interface documented in js.h */
void js_destroythread(jsthread *thread)
{
    jsheap *heap;

    if (thread == NULL) {
        return;
    }

    heap = thread->heap;
    NSLOG(wisp, DEBUG, "Destroying QuickJS thread %p (heap %p)", thread, heap);

    if (thread->ctx != NULL) {
        js_closethread(thread);
        JS_SetContextOpaque(thread->ctx, NULL);
        JS_FreeValue(thread->ctx, thread->error_stack_getter);
        JS_FreeContext(thread->ctx);
        thread->ctx = NULL;
    }

    free(thread);

    /* Drop our reference on the heap. If the owner (browser_window)
     * already released its reference, this will free the runtime. */
    jsheap_unref(heap);
}


/* exported interface documented in js.h */
bool js_exec(jsthread *thread, const uint8_t *txt, size_t txtlen, const char *name)
{
    JSValue result;
    bool success = true;
    char stack_buf[1024];
    char *term_txt = NULL;
    const char *source = name ? name : "<script>";

    if (thread == NULL || thread->ctx == NULL || thread->closed) {
        NSLOG(wisp, WARNING, "Attempted to execute JS on invalid/closed thread");
        return false;
    }

    if (txtlen == 0) {
        return true; /* Nothing to execute */
    }
    if (txt == NULL || txtlen == SIZE_MAX) {
        return false;
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

    if (qjs_execution_begin(thread->heap)) {
        result = JS_Eval(thread->ctx, term_txt, txtlen, source, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result) || JS_HasException(thread->ctx) || thread->heap->interrupted) {
            qjs_report_exception(thread, source, thread->heap->interrupted);
            success = false;
        }
        JS_FreeValue(thread->ctx, result);
    } else {
        NSLOG(wisp, WARNING, "Unable to begin bounded JavaScript execution in %s", source);
        success = false;
    }
    thread->heap->execution_depth--;

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
