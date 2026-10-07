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
