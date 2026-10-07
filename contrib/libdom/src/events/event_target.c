/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 */

#include <assert.h>
#include <stdlib.h>

#include "events/event.h"
#include "events/event_listener.h"
#include "events/event_target.h"

#include "core/document.h"
#include "core/node.h"
#include "core/string.h"

#include "utils/utils.h"
#include "utils/validate.h"

static void event_target_listener_unref(struct listener_entry *e)
{
    if (--e->refcnt != 0)
        return;
    dom_event_listener_unref(e->listener);
    dom_string_unref(e->type);
    free(e);
}

static void event_target_remove_listener(dom_event_target_internal *eti, struct listener_entry *e)
{
    if (eti->listeners == e) {
        eti->listeners = e->list.next == &e->list ? NULL : (struct listener_entry *)e->list.next;
    }
    list_del(&e->list);
    e->removed = true;
    event_target_listener_unref(e);
}

/* Initialise this EventTarget */
dom_exception _dom_event_target_internal_initialise(dom_event_target_internal *eti)
{
    eti->listeners = NULL;

    return DOM_NO_ERR;
}

/* Finalise this EventTarget */
void _dom_event_target_internal_finalise(dom_event_target_internal *eti)
{
    while (eti->listeners != NULL)
        event_target_remove_listener(eti, eti->listeners);
}

/*-------------------------------------------------------------------------*/
/* The public API */

/**
 * Add an EventListener to the EventTarget
 *
 * \param et        The EventTarget object
 * \param type      The event type which this event listener listens for
 * \param listener  The event listener object
 * \param capture   Whether add this listener in the capturing phase
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_event_target_add_event_listener(
    dom_event_target_internal *eti, dom_string *type, struct dom_event_listener *listener, bool capture)
{
    struct listener_entry *le = NULL;

    if (eti->listeners != NULL) {
        le = eti->listeners;
        do {
            if (dom_string_isequal(le->type, type) && le->listener == listener && le->capture == capture)
                return DOM_NO_ERR;
            le = (struct listener_entry *)le->list.next;
        } while (le != eti->listeners);
    }

    le = malloc(sizeof(struct listener_entry));
    if (le == NULL)
        return DOM_NO_MEM_ERR;

    /* Initialise the listener_entry */
    list_init(&le->list);
    le->type = dom_string_ref(type);
    le->listener = listener;
    dom_event_listener_ref(listener);
    le->capture = capture;
    le->removed = false;
    le->refcnt = 1;

    if (eti->listeners == NULL) {
        eti->listeners = le;
    } else {
        list_append(&eti->listeners->list, &le->list);
    }

    return DOM_NO_ERR;
}

/**
 * Remove an EventListener from the EventTarget
 *
 * (LibDOM extension: If type is NULL, remove all listener registrations
 * regardless of type and capture)
 *
 * \param et        The EventTarget object
 * \param type      The event type this listener is registered for
 * \param listener  The listener object
 * \param capture   Whether the listener is registered at the capturing phase
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_event_target_remove_event_listener(
    dom_event_target_internal *eti, dom_string *type, struct dom_event_listener *listener, bool capture)
{
    if (eti->listeners != NULL) {
        struct listener_entry *le = eti->listeners;
        struct listener_entry *last = (struct listener_entry *)le->list.prev;

        for (;;) {
            struct listener_entry *next = (struct listener_entry *)le->list.next;
            bool at_end = le == last;
            bool matches;
            if (type == NULL) {
                matches = (le->listener == listener);
            } else {
                matches = dom_string_isequal(le->type, type) && (le->listener == listener) && (le->capture == capture);
            }
            if (matches) {
                event_target_remove_listener(eti, le);
                if (type != NULL)
                    break;
            }
            if (at_end)
                break;
            le = next;
        }
    }

    return DOM_NO_ERR;
}

/**
 * Add an EventListener
 *
 * \param et         The EventTarget object
 * \param namespace  The namespace of this listener
 * \param type       The event type which this event listener listens for
 * \param listener   The event listener object
 * \param capture    Whether add this listener in the capturing phase
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 *
 * We don't support this API now, so it always return DOM_NOT_SUPPORTED_ERR.
 */
dom_exception _dom_event_target_add_event_listener_ns(dom_event_target_internal *eti, dom_string *namespace,
    dom_string *type, struct dom_event_listener *listener, bool capture)
{
    UNUSED(eti);
    UNUSED(namespace);
    UNUSED(type);
    UNUSED(listener);
    UNUSED(capture);

    return DOM_NOT_SUPPORTED_ERR;
}

/**
 * Remove an EventListener
 *
 * \param et         The EventTarget object
 * \param namespace  The namespace of this listener
 * \param type       The event type which this event listener listens for
 * \param listener   The event listener object
 * \param capture    Whether add this listener in the capturing phase
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 *
 * We don't support this API now, so it always return DOM_NOT_SUPPORTED_ERR.
 */
dom_exception _dom_event_target_remove_event_listener_ns(dom_event_target_internal *eti, dom_string *namespace,
    dom_string *type, struct dom_event_listener *listener, bool capture)
{
    UNUSED(eti);
    UNUSED(namespace);
    UNUSED(type);
    UNUSED(listener);
    UNUSED(capture);

    return DOM_NOT_SUPPORTED_ERR;
}

/*-------------------------------------------------------------------------*/

/**
 * Invoke one target/phase using a snapshot of its matching registrations.
 * Additions wait for a later invocation; removals also affect this snapshot.
 * See DOM's invoke/inner invoke algorithms.
 */
dom_exception _dom_event_target_dispatch(
    dom_event_target *et, dom_event_target_internal *eti, struct dom_event *evt, dom_event_flow_phase phase)
{
    struct listener_entry *local[16], **snapshot = local;
    size_t count = 0;
    bool capture = phase == DOM_CAPTURING_PHASE;

    assert(phase == DOM_CAPTURING_PHASE || phase == DOM_BUBBLING_PHASE);
    evt->current = et;
    if (eti->listeners == NULL)
        return DOM_NO_ERR;

    struct listener_entry *le = eti->listeners;
    do {
        if (le->capture == capture && dom_string_isequal(le->type, evt->type))
            count++;
        le = (struct listener_entry *)le->list.next;
    } while (le != eti->listeners);

    if (count > sizeof(local) / sizeof(local[0])) {
        if (count > SIZE_MAX / sizeof(*snapshot))
            return DOM_NO_MEM_ERR;
        snapshot = malloc(count * sizeof(*snapshot));
        if (snapshot == NULL)
            return DOM_NO_MEM_ERR;
    }

    count = 0;
    do {
        if (le->capture == capture && dom_string_isequal(le->type, evt->type)) {
            le->refcnt++;
            snapshot[count++] = le;
        }
        le = (struct listener_entry *)le->list.next;
    } while (le != eti->listeners);

    for (size_t i = 0; i < count; i++) {
        le = snapshot[i];
        if (!le->removed && !evt->stop_now) {
            assert(le->listener->handler != NULL);
            le->listener->handler(evt, le->listener->pw);
        }
        event_target_listener_unref(le);
    }
    if (snapshot != local)
        free(snapshot);
    return DOM_NO_ERR;
}

static void event_default_action(dom_document *doc, dom_event *evt, dom_string *type, dom_default_action_phase phase)
{
    if (doc != NULL && doc->dei.actions != NULL) {
        void *pw = doc->dei.actions_ctx;
        dom_default_action_callback cb = doc->dei.actions(type, phase, &pw);
        if (cb != NULL)
            cb(evt, pw);
    }
}

/* Helper to determine if an event is a mutation event */
static bool _event_is_mutation(dom_document *doc, struct dom_event *evt)
{
    return dom_string_isequal(evt->type, doc->_memo_domnodeinserted) ||
        dom_string_isequal(evt->type, doc->_memo_domnoderemoved) ||
        dom_string_isequal(evt->type, doc->_memo_domnodeinsertedintodocument) ||
        dom_string_isequal(evt->type, doc->_memo_domnoderemovedfromdocument) ||
        dom_string_isequal(evt->type, doc->_memo_domattrmodified) ||
        dom_string_isequal(evt->type, doc->_memo_domcharacterdatamodified) ||
        dom_string_isequal(evt->type, doc->_memo_domsubtreemodified);
}

dom_exception
_dom_dispatch_event_path(dom_event *evt, const struct dom_event_path *path, dom_document *doc, bool *success)
{
    dom_exception ret = DOM_NO_ERR;
    dom_string *type;
    bool mutation;
    dom_event_target *et = path->targets[0];

    assert(et != NULL);
    assert(evt != NULL);

    if (evt->in_dispatch)
        return DOM_DISPATCH_REQUEST_ERR;
    if (!evt->is_initialised || evt->type == NULL)
        return DOM_UNSPECIFIED_EVENT_TYPE_ERR;

    dom_event_ref(evt);
    dom_node_ref(doc);
    type = dom_string_ref(evt->type);
    mutation = doc != NULL && _event_is_mutation(doc, evt);
    evt->in_dispatch = true;
    evt->path = path;
    *success = !evt->prevent_default;

    dom_event_target_ref(et);
    dom_event_target_unref(evt->target);
    evt->target = et;
    if (mutation)
        doc->dispatching_mutation++;
    event_default_action(doc, evt, type, DOM_DEFAULT_ACTION_STARTED);

    for (size_t i = path->count; i != 0 && !evt->stop; i--) {
        dom_event_target *target = path->targets[i - 1];
        evt->phase = i == 1 ? DOM_AT_TARGET : DOM_CAPTURING_PHASE;
        ret = ((dom_event_target_vtable *)target->vtable)->invoke(target, evt, true);
        if (ret != DOM_NO_ERR)
            goto cleanup;
    }
    for (size_t i = 0; i < path->count && !evt->stop; i++) {
        if (i != 0 && !evt->bubble)
            break;
        dom_event_target *target = path->targets[i];
        evt->phase = i == 0 ? DOM_AT_TARGET : DOM_BUBBLING_PHASE;
        ret = ((dom_event_target_vtable *)target->vtable)->invoke(target, evt, false);
        if (ret != DOM_NO_ERR)
            goto cleanup;
    }

cleanup:
    evt->path = NULL;
    evt->phase = DOM_NONE;
    evt->current = NULL;
    evt->in_dispatch = false;
    evt->stop = false;
    evt->stop_now = false;
    evt->passive = false;

    /* Stopping propagation does not cancel default actions. Use the saved
     * type: a native action may initialize or redispatch the event. */
    if (ret == DOM_NO_ERR)
        event_default_action(
            doc, evt, type, evt->prevent_default ? DOM_DEFAULT_ACTION_PREVENTED : DOM_DEFAULT_ACTION_END);
    if (mutation)
        doc->dispatching_mutation--;
    event_default_action(doc, evt, type, DOM_DEFAULT_ACTION_FINISHED);
    *success = !evt->prevent_default;
    dom_string_unref(type);
    dom_event_unref(evt);
    dom_node_unref(doc);
    return ret;
}

struct standalone_target {
    dom_event_target base;
    unsigned refcnt;
    dom_event_target_internal listeners;
};

static void standalone_ref(dom_event_target *target)
{
    ((struct standalone_target *)target)->refcnt++;
}

static void standalone_unref(dom_event_target *target)
{
    struct standalone_target *t = (struct standalone_target *)target;
    if (--t->refcnt != 0)
        return;
    _dom_event_target_internal_finalise(&t->listeners);
    free(t);
}

static dom_exception
standalone_add(dom_event_target *target, dom_string *type, dom_event_listener *listener, bool capture)
{
    return _dom_event_target_add_event_listener(
        &((struct standalone_target *)target)->listeners, type, listener, capture);
}

static dom_exception
standalone_remove(dom_event_target *target, dom_string *type, dom_event_listener *listener, bool capture)
{
    return _dom_event_target_remove_event_listener(
        &((struct standalone_target *)target)->listeners, type, listener, capture);
}

static dom_exception standalone_invoke(dom_event_target *target, dom_event *evt, bool capture)
{
    return _dom_event_target_dispatch(target, &((struct standalone_target *)target)->listeners, evt,
        capture ? DOM_CAPTURING_PHASE : DOM_BUBBLING_PHASE);
}

static dom_exception standalone_dispatch(dom_event_target *target, dom_event *evt, bool *success)
{
    dom_event_target_ref(target);
    struct dom_event_path path = {&target, 1};
    dom_exception ret = _dom_dispatch_event_path(evt, &path, NULL, success);
    dom_event_target_unref(target);
    return ret;
}

static dom_exception standalone_namespace(
    dom_event_target *target, dom_string *ns, dom_string *type, dom_event_listener *listener, bool capture)
{
    return DOM_NOT_SUPPORTED_ERR;
}

static const dom_event_target_vtable standalone_vtable = {standalone_add, standalone_remove, standalone_dispatch,
    standalone_namespace, standalone_namespace, standalone_ref, standalone_unref, standalone_invoke};

dom_exception dom_event_target_create(dom_event_target **result)
{
    *result = NULL;
    struct standalone_target *target = calloc(1, sizeof(*target));
    if (target == NULL)
        return DOM_NO_MEM_ERR;
    target->base.vtable = (void *)&standalone_vtable;
    target->refcnt = 1;
    *result = &target->base;
    return DOM_NO_ERR;
}
