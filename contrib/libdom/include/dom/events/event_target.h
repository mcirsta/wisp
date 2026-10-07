/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 */

#ifndef dom_events_event_target_h_
#define dom_events_event_target_h_

#include <dom/core/exceptions.h>
#include <dom/core/string.h>
#include <stdbool.h>

struct dom_event_listener;
struct dom_event;

/* Nodes and standalone targets provide the same event and ownership interface. */
typedef struct dom_event_target {
    void *vtable;
} dom_event_target;

typedef struct dom_event_target_vtable {
    dom_exception (*add_event_listener)(
        dom_event_target *et, dom_string *type, struct dom_event_listener *listener, bool capture);
    dom_exception (*remove_event_listener)(
        dom_event_target *et, dom_string *type, struct dom_event_listener *listener, bool capture);
    dom_exception (*dispatch_event)(dom_event_target *et, struct dom_event *evt, bool *success);
    dom_exception (*add_event_listener_ns)(dom_event_target *et, dom_string *namespace, dom_string *type,
        struct dom_event_listener *listener, bool capture);
    dom_exception (*remove_event_listener_ns)(dom_event_target *et, dom_string *namespace, dom_string *type,
        struct dom_event_listener *listener, bool capture);
    void (*ref)(dom_event_target *et);
    void (*unref)(dom_event_target *et);
    dom_exception (*invoke)(dom_event_target *et, struct dom_event *evt, bool capture);
} dom_event_target_vtable;

/* Create a target with no parent (DOM EventTarget constructor semantics). */
dom_exception dom_event_target_create(dom_event_target **target);
static inline dom_event_target *dom_event_target_ref(dom_event_target *target)
{
    if (target != NULL)
        ((dom_event_target_vtable *)target->vtable)->ref(target);
    return target;
}
static inline void dom_event_target_unref(dom_event_target *target)
{
    if (target != NULL)
        ((dom_event_target_vtable *)target->vtable)->unref(target);
}

static inline dom_exception dom_event_target_add_event_listener(
    dom_event_target *et, dom_string *type, struct dom_event_listener *listener, bool capture)
{
    return ((dom_event_target_vtable *)et->vtable)->add_event_listener(et, type, listener, capture);
}
#define dom_event_target_add_event_listener(et, t, l, c)                                                               \
    dom_event_target_add_event_listener(                                                                               \
        (dom_event_target *)(et), (dom_string *)(t), (struct dom_event_listener *)(l), (bool)(c))

static inline dom_exception dom_event_target_remove_event_listener(
    dom_event_target *et, dom_string *type, struct dom_event_listener *listener, bool capture)
{
    return ((dom_event_target_vtable *)et->vtable)->remove_event_listener(et, type, listener, capture);
}
#define dom_event_target_remove_event_listener(et, t, l, c)                                                            \
    dom_event_target_remove_event_listener(                                                                            \
        (dom_event_target *)(et), (dom_string *)(t), (struct dom_event_listener *)(l), (bool)(c))

static inline dom_exception dom_event_target_dispatch_event(dom_event_target *et, struct dom_event *evt, bool *success)
{
    return ((dom_event_target_vtable *)et->vtable)->dispatch_event(et, evt, success);
}
#define dom_event_target_dispatch_event(et, e, s)                                                                      \
    dom_event_target_dispatch_event((dom_event_target *)(et), (struct dom_event *)(e), (bool *)(s))

static inline dom_exception dom_event_target_add_event_listener_ns(
    dom_event_target *et, dom_string *namespace, dom_string *type, struct dom_event_listener *listener, bool capture)
{
    return ((dom_event_target_vtable *)et->vtable)->add_event_listener_ns(et, namespace, type, listener, capture);
}
#define dom_event_target_add_event_listener_ns(et, n, t, l, c)                                                         \
    dom_event_target_add_event_listener_ns(                                                                            \
        (dom_event_target *)(et), (dom_string *)(n), (dom_string *)(t), (struct dom_event_listener *)(l), (bool)(c))

static inline dom_exception dom_event_target_remove_event_listener_ns(
    dom_event_target *et, dom_string *namespace, dom_string *type, struct dom_event_listener *listener, bool capture)
{
    return ((dom_event_target_vtable *)et->vtable)->remove_event_listener_ns(et, namespace, type, listener, capture);
}
#define dom_event_target_remove_event_listener_ns(et, n, t, l, c)                                                      \
    dom_event_target_remove_event_listener_ns(                                                                         \
        (dom_event_target *)(et), (dom_string *)(n), (dom_string *)(t), (struct dom_event_listener *)(l), (bool)(c))

#endif
