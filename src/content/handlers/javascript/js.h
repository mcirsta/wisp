/*
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file
 * Interface to javascript engine functions.
 */

#ifndef WISP_JAVASCRIPT_JS_H_
#define WISP_JAVASCRIPT_JS_H_

#include <wisp/utils/errors.h>

struct dom_event;
struct dom_document;
struct dom_node;
struct dom_element;
struct dom_string;

/**
 * JavaScript interpreter heap
 *
 * In order to try and be moderately performant, we create a heap
 * per browser window.  This heap is shared by all browsing contexts
 * we end up creating in that window.
 */
typedef struct jsheap jsheap;

/**
 * JavaScript interpreter thread
 *
 * When we create a browsing context itself (window+content) we have
 * to create a JS thread to attach to the browsing context.
 *
 * Each thread retains its heap until explicitly destroyed. Closing disconnects
 * document access and cancels jobs/timers; destroying releases the owner
 * reference. Active entries/jobs/timer callbacks retain the context until they
 * return. A JavaScript thread is not an operating-system thread.
 */
typedef struct jsthread jsthread;

/**
 * Initialise javascript interpreter
 */
void js_initialise(void);

/**
 * finalise javascript interpreter
 */
void js_finalise(void);

/**
 * Create a new javascript heap.
 *
 * There is usually one heap per browser window.
 *
 * \param timeout Monotonic elapsed-time limit in seconds per outer execution;
 * nonpositive values disable the time limit. Nested entries share the budget.
 * \param heap Updated to the created JS heap
 * \return NSERROR_OK on success, appropriate error otherwise.
 */
nserror js_newheap(int timeout, jsheap **heap);

/**
 * Destroy a previously created heap.
 *
 * \param heap The heap to destroy
 */
void js_destroyheap(jsheap *heap);

/**
 * Create a new javascript thread
 *
 * This is called once for a page with javascript script tags on
 * it. It constructs a fresh global window object and prepares the JS
 * browsing context.  It's important that threads are shut down cleanly
 * when the browsing context is going to be cleaned up.
 *
 * \param heap The heap to create the thread within
 * \param win_priv The value to give to the Window constructor as the window
 * \param doc_priv The value to give to the Document constructor as the document
 * \param thread Updated to the created thread
 * \return NSERROR_OK on success, appropriate error otherwise
 */
nserror js_newthread(jsheap *heap, void *win_priv, void *doc_priv, jsthread **thread);

/**
 * Close a javascript thread
 *
 * This should be called when the HTML content which owns the thread is
 * being closed.  This is a separate process from destroying the thread
 * and merely disconnects any callbacks and thus hopefully stops
 * additional JS things from triggering.  If any code runs and attempts to
 * register callbacks after closedown, they will fail.
 *
 * \param thread The thread to close down
 * \return NSERROR_OK on success, appropriate error otherwise
 */
nserror js_closethread(jsthread *thread);

/**
 * Destroy a javascript thread
 *
 * This should be called when the browsing context is done with the thread.
 *
 * This will be called when the HTML content associated with the browsing
 * context is being destroyed.  The thread should have already been closed
 * down during the HTML content close.
 *
 * \param thread The thread to be destroyed
 */
void js_destroythread(jsthread *thread);

/**
 * Execute JavaScript within the heap's resource limits.
 *
 * Returns true for normal completion (including a false/undefined JS result),
 * false for exceptions, interruption or invalid input/context. The outer entry
 * drains microtasks using the same time budget. Ordinary job exceptions are
 * reported without aborting the checkpoint. Time limits are cooperative QuickJS
 * interrupts, not preemption of arbitrary native binding code.
 */
bool js_exec(jsthread *thread, const uint8_t *txt, size_t txtlen, const char *name);

/* Trusted document lifecycle notifications. The caller updates readiness before
 * notifying, and retains the HTML content across callbacks. Window load uses the
 * DOM legacy target override (Document target, Window currentTarget/path). */
enum js_document_event { JS_DOCUMENT_READYSTATE, JS_DOCUMENT_DOM_CONTENT_LOADED, JS_DOCUMENT_LOAD };
bool js_fire_document_event(jsthread *thread, enum js_document_event event);

bool js_dom_event_add_listener(jsthread *thread, struct dom_document *document, struct dom_node *node,
    struct dom_string *event_type_dom, void *js_funcval);

/* Native click payload in CSS pixels. NULL denotes a non-pointing activation
 * (for example Enter on a form's default button). This API delivers only click
 * and checkable input/change; it does not expose legacy DOM mutation events. */
struct js_mouse_event {
    double client_x, client_y, screen_x, screen_y;
    unsigned buttons, detail;
    bool ctrl, alt, shift, meta;
};
bool js_fire_input_event(
    jsthread *thread, struct dom_string *type, struct dom_node *target, const struct js_mouse_event *mouse);

/* Trusted, bubbling/cancelable SubmitEvent. NULL submitter denotes implicit
 * submission without a button. False suppresses navigation (canceled/failed).
 * Caller owns content/DOM lifetime and guards recursive submission. */
bool js_fire_submit_event(jsthread *thread, struct dom_node *form, struct dom_node *submitter);

/*** New Events ***/

/**
 * Handle a new element being created.
 *
 * This is called once an element is inserted into the DOM document handled
 * by the context provided.  The JS implementation must then scan the element
 * for on* attributes and register appropriate listeners for those handlers.
 */
void js_handle_new_element(jsthread *thread, struct dom_element *node);

/**
 * Handle an event propagation finished callback.
 *
 * This is called once an event finishes propagating, no matter how it
 * finishes.  The intent here is that the JS context can perform any cleanups
 * it may need to perform before the DOM finishes and the event may end up
 * freed.
 */
void js_event_cleanup(jsthread *thread, struct dom_event *evt);

#endif /* NETSURF_JAVASCRIPT_JS_H_ */
