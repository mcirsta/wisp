/* Event expectations follow https://dom.spec.whatwg.org/#concept-event-dispatch. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <dom/events/events.h>

static dom_document *doc;
static dom_node *parent, *target;
static dom_string *type;
static char calls[1024];
static size_t ncalls;
static unsigned actions[4];

static dom_string *string(const char *s)
{
    dom_string *result;
    ck_assert_int_eq(dom_string_create((const uint8_t *)s, strlen(s), &result), DOM_NO_ERR);
    return result;
}

static void append(dom_node *p, dom_node *child)
{
    dom_node *result;
    ck_assert_int_eq(dom_node_append_child(p, child, &result), DOM_NO_ERR);
    dom_node_unref(result);
}

static dom_node *element(void)
{
    dom_node *result;
    dom_string *name = string("div");
    ck_assert_int_eq(dom_document_create_element(doc, name, &result), DOM_NO_ERR);
    dom_string_unref(name);
    return result;
}

static void action(dom_event *event, void *pw)
{
    (void)event;
    (*(unsigned *)pw)++;
}

static dom_default_action_callback fetch_action(dom_string *name, dom_default_action_phase phase, void **pw)
{
    if (!dom_string_isequal(name, type))
        return NULL;
    *pw = &actions[phase];
    return action;
}

static void setup(void)
{
    type = string("test");
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, fetch_action, NULL, &doc),
        DOM_NO_ERR);
    parent = element();
    target = element();
    append((dom_node *)doc, parent);
    append(parent, target);
    memset(calls, 0, sizeof(calls));
    memset(actions, 0, sizeof(actions));
    ncalls = 0;
}

static void teardown(void)
{
    dom_node_unref(target);
    dom_node_unref(parent);
    dom_node_unref(doc);
    dom_string_unref(type);
}

static dom_event *event(bool bubbles, bool cancelable)
{
    dom_event *result;
    ck_assert_int_eq(dom_event_create(&result), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_init(result, type, bubbles, cancelable), DOM_NO_ERR);
    return result;
}

static void record(dom_event *event, void *pw)
{
    dom_event_flow_phase phase;
    dom_event_target *current, *origin;
    ck_assert_int_eq(dom_event_get_event_phase(event, &phase), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_get_current_target(event, &current), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_get_target(event, &origin), DOM_NO_ERR);
    ck_assert_ptr_nonnull(current);
    ck_assert_ptr_nonnull(origin);
    if (current == origin)
        ck_assert_int_eq(phase, DOM_AT_TARGET);
    else
        ck_assert(phase == DOM_CAPTURING_PHASE || phase == DOM_BUBBLING_PHASE);
    ck_assert_uint_lt(ncalls + 2, sizeof(calls));
    calls[ncalls++] = *(const char *)pw;
    calls[ncalls++] = '0' + phase;
    dom_node_unref(current);
    dom_node_unref(origin);
}

static dom_event_listener *listen(dom_node *node, handle_event callback, void *pw, bool capture)
{
    dom_event_listener *listener;
    ck_assert_int_eq(dom_event_listener_create(callback, pw, &listener), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(node, type, listener, capture), DOM_NO_ERR);
    return listener;
}

static void record_at(dom_node *node, const char *label, bool capture)
{
    dom_event_listener *listener = listen(node, record, (void *)label, capture);
    dom_event_listener_unref(listener);
}

static bool dispatch(dom_node *node, dom_event *evt)
{
    bool success = false, active = true;
    dom_event_flow_phase phase;
    dom_event_target *current;
    ck_assert_int_eq(dom_event_target_dispatch_event(node, evt, &success), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_in_dispatch(evt, &active), DOM_NO_ERR);
    ck_assert_msg(!active, "dispatch flag must be cleared on return");
    ck_assert_int_eq(dom_event_get_event_phase(evt, &phase), DOM_NO_ERR);
    ck_assert_int_eq(phase, 0);
    ck_assert_int_eq(dom_event_get_current_target(evt, &current), DOM_NO_ERR);
    ck_assert_ptr_null(current);
    return success;
}

START_TEST(test_phase_order)
{
    dom_event *evt = event(_i != 0, true);
    record_at((dom_node *)doc, "D", true);
    record_at(parent, "P", true);
    /* Register target bubbling first: capture must still run first. */
    record_at(target, "b", false);
    record_at(target, "c", true);
    record_at(parent, "p", false);
    record_at((dom_node *)doc, "d", false);
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, _i ? "D1P1c2b2p3d3" : "D1P1c2b2");
    ck_assert_uint_eq(actions[DOM_DEFAULT_ACTION_STARTED], 1);
    ck_assert_uint_eq(actions[DOM_DEFAULT_ACTION_END], 1);
    ck_assert_uint_eq(actions[DOM_DEFAULT_ACTION_FINISHED], 1);
    dom_event_unref(evt);
}
END_TEST

START_TEST(test_duplicates_and_removal_order)
{
    dom_event *evt = event(true, true);
    dom_event_listener *first = listen(target, record, "a", false);
    dom_event_listener *middle = listen(target, record, "b", false);
    record_at(target, "c", false);
    ck_assert_int_eq(dom_event_target_add_event_listener(target, type, first, false), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(target, type, first, true), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_remove_event_listener(target, type, middle, false), DOM_NO_ERR);
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, "a2a2c2");
    dom_event_listener_unref(first);
    dom_event_listener_unref(middle);
    dom_event_unref(evt);
}
END_TEST

struct editing {
    dom_event_listener *self, *next;
};

static void edit_listeners(dom_event *evt, void *pw)
{
    struct editing *edit = pw;
    record(evt, "e");
    ck_assert_int_eq(dom_event_target_remove_event_listener(target, type, edit->self, false), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_remove_event_listener(target, type, edit->next, false), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(target, type, edit->next, false), DOM_NO_ERR);
    record_at(target, "n", false);
    /* This ancestor had no listener when the path was built. */
    record_at(parent, "p", false);
}

START_TEST(test_listener_snapshot)
{
    dom_event *evt = event(true, true);
    struct editing edit;
    edit.self = listen(target, edit_listeners, &edit, false);
    edit.next = listen(target, record, "r", false);
    record_at(target, "s", false);
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, "e2s2p3");
    ncalls = 0;
    memset(calls, 0, sizeof(calls));
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, "s2r2n2p3");
    dom_event_listener_unref(edit.self);
    dom_event_listener_unref(edit.next);
    dom_event_unref(evt);
}
END_TEST

static void remove_all(dom_event *evt, void *pw)
{
    dom_event_listener **listener = pw;
    record(evt, "a");
    ck_assert_int_eq(dom_event_target_remove_event_listener(target, NULL, *listener, false), DOM_NO_ERR);
}

START_TEST(test_remove_all_registrations)
{
    dom_event *evt = event(true, true);
    dom_event_listener *listener = listen(target, remove_all, &listener, true);
    dom_string *other = string("other");
    ck_assert_int_eq(dom_event_target_add_event_listener(target, type, listener, false), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(target, other, listener, false), DOM_NO_ERR);
    record_at(target, "b", false);
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, "a2b2");
    ck_assert_int_eq(dom_event_init(evt, other, true, true), DOM_NO_ERR);
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, "a2b2");
    dom_string_unref(other);
    dom_event_listener_unref(listener);
    dom_event_unref(evt);
}
END_TEST

static void add_target_bubble_listener(dom_event *evt, void *pw)
{
    (void)pw;
    record(evt, "c");
    record_at(target, "b", false);
}

START_TEST(test_snapshot_is_per_invocation)
{
    dom_event *evt = event(false, false);
    dom_event_listener *listener = listen(target, add_target_bubble_listener, NULL, true);
    ck_assert(dispatch(target, evt));
    /* Added during capture, visible in the subsequent target bubble invocation. */
    ck_assert_str_eq(calls, "c2b2");
    dom_event_listener_unref(listener);
    dom_event_unref(evt);
}
END_TEST

static void stop(dom_event *evt, void *pw)
{
    record(evt, "s");
    if (*(int *)pw)
        ck_assert_int_eq(dom_event_stop_immediate_propagation(evt), DOM_NO_ERR);
    else
        ck_assert_int_eq(dom_event_stop_propagation(evt), DOM_NO_ERR);
}

START_TEST(test_stop_does_not_cancel)
{
    int immediate = _i & 1;
    bool capture = (_i & 2) != 0;
    dom_event *evt = event(true, true);
    dom_event_listener *listener = listen(target, stop, &immediate, capture);
    record_at(target, "a", capture);
    record_at(target, "b", !capture);
    record_at(parent, "p", false);
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, capture ? (immediate ? "s2" : "s2a2") : (immediate ? "b2s2" : "b2s2a2"));
    ck_assert_uint_eq(actions[DOM_DEFAULT_ACTION_END], 1);
    ck_assert_uint_eq(actions[DOM_DEFAULT_ACTION_PREVENTED], 0);
    ck_assert_uint_eq(actions[DOM_DEFAULT_ACTION_FINISHED], 1);
    ck_assert_int_eq(dom_event_target_remove_event_listener(target, type, listener, capture), DOM_NO_ERR);
    /* Redispatch clears propagation flags, but retains the cancellation flag. */
    ncalls = 0;
    memset(calls, 0, sizeof(calls));
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, capture ? "a2b2p3" : "b2a2p3");
    dom_event_listener_unref(listener);
    dom_event_unref(evt);
}
END_TEST

START_TEST(test_stopped_before_target)
{
    int immediate = 0;
    dom_event *evt = event(true, true);
    dom_event_listener *listener = listen(parent, stop, &immediate, true);
    record_at(parent, "a", true);
    record_at(target, "t", false);
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, "s1a1");
    ck_assert_uint_eq(actions[DOM_DEFAULT_ACTION_END], 1);
    ck_assert_int_eq(dom_event_target_remove_event_listener(parent, type, listener, true), DOM_NO_ERR);
    dom_event_stop_propagation(evt);
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, "s1a1");
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, "s1a1a1t2");
    dom_event_listener_unref(listener);
    dom_event_unref(evt);
}
END_TEST

static void cancel(dom_event *evt, void *pw)
{
    (void)pw;
    ck_assert_int_eq(dom_event_prevent_default(evt), DOM_NO_ERR);
}

START_TEST(test_cancelable_and_reinitialization)
{
    bool cancelable = _i != 0, prevented;
    dom_event *evt = event(true, cancelable);
    dom_event_listener *listener = listen(target, cancel, NULL, false);
    ck_assert_int_eq(dispatch(target, evt), !cancelable);
    ck_assert_uint_eq(actions[cancelable ? DOM_DEFAULT_ACTION_PREVENTED : DOM_DEFAULT_ACTION_END], 1);
    ck_assert_int_eq(dom_event_target_remove_event_listener(target, type, listener, false), DOM_NO_ERR);
    ck_assert_int_eq(dispatch(target, evt), !cancelable);
    ck_assert_int_eq(dom_event_init(evt, type, true, true), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_is_default_prevented(evt, &prevented), DOM_NO_ERR);
    ck_assert(!prevented);
    dom_event_target *origin;
    ck_assert_int_eq(dom_event_get_target(evt, &origin), DOM_NO_ERR);
    ck_assert_ptr_null(origin);
    ck_assert(dispatch(target, evt));
    dom_event_listener_unref(listener);
    dom_event_unref(evt);
}
END_TEST

START_TEST(test_namespace_reinitialization)
{
    dom_event *evt = event(true, true);
    dom_string *ns = string("urn:test"), *saved;
    ck_assert_int_eq(dom_event_init_ns(evt, ns, type, true, true), DOM_NO_ERR);
    /* Passing the same strings again must not leak or release them too soon. */
    ck_assert_int_eq(dom_event_init_ns(evt, ns, type, false, false), DOM_NO_ERR);
    dom_string_unref(ns);
    ck_assert_int_eq(dom_event_get_namespace(evt, &saved), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(saved), "urn:test");
    dom_string_unref(saved);
    ck_assert_int_eq(dom_event_init(evt, type, false, false), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_get_namespace(evt, &saved), DOM_NO_ERR);
    ck_assert_ptr_null(saved);
    dom_event_unref(evt);
}
END_TEST

START_TEST(test_initialization_and_document_target)
{
    dom_event *evt;
    bool success;
    dom_string *empty = string("");
    ck_assert_int_eq(dom_event_create(&evt), DOM_NO_ERR);
    ck_assert_int_ne(dom_event_target_dispatch_event(target, evt, &success), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_init(evt, empty, false, false), DOM_NO_ERR);
    ck_assert(dispatch(target, evt));
    ck_assert_int_eq(dom_event_init(evt, type, false, false), DOM_NO_ERR);
    record_at((dom_node *)doc, "b", false);
    record_at((dom_node *)doc, "c", true);
    ck_assert(dispatch((dom_node *)doc, evt));
    ck_assert_str_eq(calls, "c2b2");
    dom_string_unref(empty);
    dom_event_unref(evt);
}
END_TEST

static void recursive(dom_event *evt, void *pw)
{
    bool success;
    dom_string *other = pw, *actual;
    ck_assert_int_eq(dom_event_target_dispatch_event(target, evt, &success), DOM_DISPATCH_REQUEST_ERR);
    ck_assert_int_eq(dom_event_init(evt, other, false, false), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_get_type(evt, &actual), DOM_NO_ERR);
    ck_assert(dom_string_isequal(actual, type));
    dom_string_unref(actual);
    dom_event *nested = event(false, false);
    ck_assert_int_eq(dom_event_init(nested, other, false, false), DOM_NO_ERR);
    ck_assert(dispatch(parent, nested));
    dom_event_unref(nested);
    record(evt, "r");
}

START_TEST(test_reentrant_dispatch)
{
    dom_string *other = string("nested");
    dom_event *evt = event(true, true);
    dom_event_listener *listener = listen(target, recursive, other, false);
    record_at(parent, "p", false);
    ck_assert(dispatch(target, evt));
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, "r2p3r2p3");
    dom_event_listener_unref(listener);
    dom_string_unref(other);
    dom_event_unref(evt);
}
END_TEST

static void move_target(dom_event *evt, void *pw)
{
    record(evt, "m");
    append(pw, target);
}

START_TEST(test_path_survives_tree_changes)
{
    dom_node *new_parent = element();
    dom_event *evt = event(true, true);
    dom_event_listener *listener = listen(target, move_target, new_parent, false);
    record_at(parent, "p", false);
    record_at((dom_node *)doc, "d", false);
    record_at(new_parent, "n", false);
    ck_assert(dispatch(target, evt));
    ck_assert_str_eq(calls, "m2p3d3");
    dom_event_listener_unref(listener);
    dom_event_unref(evt);
    dom_node_unref(new_parent);
}
END_TEST

START_TEST(test_saved_event_owns_target)
{
    dom_node *detached = element();
    dom_event *evt = event(false, false);
    ck_assert(dispatch(detached, evt));
    dom_node_unref(detached);
    dom_event_target *saved;
    dom_string *name;
    ck_assert_int_eq(dom_event_get_target(evt, &saved), DOM_NO_ERR);
    ck_assert_ptr_eq(saved, detached);
    ck_assert_int_eq(dom_node_get_node_name(saved, &name), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(name), "DIV");
    dom_string_unref(name);
    dom_node_unref(saved);
    dom_event_unref(evt);
}
END_TEST

static void release_callers(dom_event *evt, void *pw)
{
    record(evt, "r");
    dom_node_unref(target);
    dom_node_unref(parent);
    dom_node_unref(doc);
    target = parent = NULL;
    doc = NULL;
    if (pw != NULL)
        dom_event_unref(evt);
}

START_TEST(test_dispatch_owns_objects)
{
    dom_event *evt = event(true, true);
    dom_event_listener *listener = listen(target, release_callers, _i ? &doc : NULL, false);
    record_at(parent, "p", false);
    record_at((dom_node *)doc, "d", false);
    bool success;
    ck_assert_int_eq(dom_event_target_dispatch_event(target, evt, &success), DOM_NO_ERR);
    ck_assert(success);
    ck_assert_str_eq(calls, "r2p3d3");
    if (!_i) {
        dom_event_target *saved;
        ck_assert_int_eq(dom_event_get_target(evt, &saved), DOM_NO_ERR);
        ck_assert_ptr_nonnull(saved);
        dom_node_unref(saved);
        dom_event_unref(evt);
    }
    dom_event_listener_unref(listener);
}
END_TEST

START_TEST(test_large_path_and_listener_snapshot)
{
    dom_node *leaf = dom_node_ref(target);
    for (unsigned i = 0; i < 80; i++) {
        dom_node *child = element();
        append(leaf, child);
        dom_node_unref(leaf);
        leaf = child;
        record_at(leaf, "p", false);
    }
    for (unsigned i = 0; i < 40; i++)
        record_at(leaf, "t", false);
    dom_event *evt = event(true, true);
    ck_assert(dispatch(leaf, evt));
    ck_assert_uint_eq(ncalls, 240);
    ck_assert_int_eq(calls[1], '2');
    ck_assert_int_eq(calls[83], '3');
    dom_event_unref(evt);
    dom_node_unref(leaf);
}
END_TEST

static unsigned owned_listener_freed;
static void owned_cleanup(void *pw)
{
    owned_listener_freed++;
}

static void standalone_listener(dom_event *evt, void *pw)
{
    dom_event_target *current;
    dom_event_get_current_target(evt, &current);
    ck_assert_ptr_eq(current, pw);
    dom_event_target_unref(current);
    dom_event_set_passive_listener(evt, true);
    dom_event_prevent_default(evt);
    bool canceled;
    dom_event_is_default_prevented(evt, &canceled);
    ck_assert(!canceled);
    dom_event_set_passive_listener(evt, false);
    dom_event_prevent_default(evt);
}

START_TEST(test_standalone_target_ownership)
{
    dom_event_target *standalone, *saved;
    dom_event_listener *listener;
    dom_event *evt = event(false, true);
    owned_listener_freed = 0;
    ck_assert_int_eq(dom_event_target_create(&standalone), DOM_NO_ERR);
    ck_assert_int_eq(
        dom_event_listener_create_owned(standalone_listener, standalone, owned_cleanup, &listener), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(standalone, type, listener, false), DOM_NO_ERR);
    dom_event_listener_unref(listener);
    bool success;
    ck_assert_int_eq(dom_event_target_dispatch_event(standalone, evt, &success), DOM_NO_ERR);
    ck_assert(!success);
    dom_event_target_unref(standalone);
    ck_assert_uint_eq(owned_listener_freed, 0);
    ck_assert_int_eq(dom_event_get_target(evt, &saved), DOM_NO_ERR);
    ck_assert_ptr_eq(saved, standalone);
    dom_event_target_unref(saved);
    dom_event_unref(evt);
    ck_assert_uint_eq(owned_listener_freed, 1);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("DOM event dispatch");
    TCase *tc = tcase_create("Propagation and callback ownership");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_phase_order, 0, 2);
    tcase_add_test(tc, test_duplicates_and_removal_order);
    tcase_add_test(tc, test_listener_snapshot);
    tcase_add_test(tc, test_remove_all_registrations);
    tcase_add_test(tc, test_snapshot_is_per_invocation);
    tcase_add_loop_test(tc, test_stop_does_not_cancel, 0, 4);
    tcase_add_test(tc, test_stopped_before_target);
    tcase_add_loop_test(tc, test_cancelable_and_reinitialization, 0, 2);
    tcase_add_test(tc, test_namespace_reinitialization);
    tcase_add_test(tc, test_initialization_and_document_target);
    tcase_add_test(tc, test_reentrant_dispatch);
    tcase_add_test(tc, test_path_survives_tree_changes);
    tcase_add_test(tc, test_saved_event_owns_target);
    tcase_add_loop_test(tc, test_dispatch_owns_objects, 0, 2);
    tcase_add_test(tc, test_large_path_and_listener_snapshot);
    tcase_add_test(tc, test_standalone_target_ownership);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
