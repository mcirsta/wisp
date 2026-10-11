/* Native checkable-control activation must not depend on layout boxes. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <dom/events/events.h>
#include <libdom/bindings/hubbub/parser.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/form_state.h"

static dom_document *document;

static struct html_form_click_result test_click(dom_node *node, html_checkable_changed_cb changed, void *pw)
{
    return html_form_click(node, &(struct html_form_click_context){.changed = changed, .pw = pw});
}

static dom_string *string(const char *value)
{
    dom_string *s;
    ck_assert_int_eq(dom_string_create((const uint8_t *)value, strlen(value), &s), DOM_NO_ERR);
    return s;
}

static void parse(const char *html)
{
    dom_hubbub_parser *parser;
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &document), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)html, strlen(html)), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
}

static dom_node *node(const char *id)
{
    dom_node *n = NULL;
    dom_string *key = string(id);
    ck_assert_int_eq(dom_document_get_element_by_id(document, key, &n), DOM_NO_ERR);
    dom_string_unref(key);
    ck_assert_msg(n != NULL, "Missing fixture element %s", id);
    return n;
}

static void note_change(dom_node *node, void *pw)
{
    (void)node;
    *(bool *)pw = true;
}

static bool activate(const char *id)
{
    dom_node *clicked = node(id);
    bool changed = false;
    test_click(clicked, note_change, &changed);
    dom_node_unref(clicked);
    return changed;
}

static void checked(const char *id, bool expected)
{
    dom_node *n = node(id);
    bool actual;
    ck_assert_int_eq(dom_html_input_element_get_checked((dom_html_input_element *)n, &actual), DOM_NO_ERR);
    ck_assert_msg(actual == expected, "%s: expected checked=%d, got %d", id, expected, actual);
    dom_node_unref(n);
}

static void teardown(void)
{
    dom_node_unref(document);
    document = NULL;
}

START_TEST(test_radio_groups)
{
    parse("<input id=a type=radio name=tabs checked><input id=b type=RaDiO name=tabs style='display:none'>"
          "<input id=c type=radio name=other checked><input id=d type=radio checked><input id=e type=radio>"
          "<input id=f type=radio name='' checked><input id=g type=radio name=''>"
          "<form id=form><input id=h type=radio name=tabs checked><input id=i type=radio name=tabs></form>"
          "<form><input id=j type=radio name=tabs checked></form>"
          "<input id=k type=radio name=tabs form=form><input id=l type=radio name=Tabs checked>");
    ck_assert(activate("b"));
    checked("a", false);
    checked("b", true);
    checked("c", true);
    checked("h", true);
    ck_assert(!activate("b"));
    ck_assert(activate("e"));
    checked("d", true);
    ck_assert(activate("g"));
    checked("f", true);
    ck_assert(activate("i"));
    checked("h", false);
    checked("i", true);
    checked("j", true);
    checked("b", true);
    ck_assert(activate("k"));
    checked("i", false);
    checked("k", true);
    checked("b", true);
    checked("l", true);
}
END_TEST

START_TEST(test_labels)
{
    parse("<input id=a type=checkbox style='display:none'><label id=l for=a><span id=s>Toggle</span></label>"
          "<label id=implicit><input id=b type=checkbox><span id=t>Implicit</span>"
          "<a id=link href='#'>Link</a><button id=button>Button</button></label>"
          "<label id=missing for=absent><input id=c type=checkbox>Missing target</label>"
          "<label id=text><input type=text><input id=d type=checkbox>First labelable control wins</label>");
    ck_assert(activate("s"));
    checked("a", true);
    ck_assert(activate("l"));
    checked("a", false);
    ck_assert(activate("t"));
    checked("b", true);
    ck_assert(!activate("link"));
    ck_assert(!activate("button"));
    checked("b", true);
    ck_assert(!activate("missing"));
    checked("c", false);
    ck_assert(!activate("text"));
    checked("d", false);
    /* Clicking the input inside its own label must toggle only once. */
    ck_assert(activate("b"));
    checked("b", false);
}
END_TEST

START_TEST(test_disabled)
{
    parse("<input id=a type=checkbox disabled><label id=l for=a>Disabled</label>"
          "<fieldset disabled><legend><input id=b type=checkbox></legend><input id=c type=checkbox>"
          "<legend><input id=d type=checkbox></legend></fieldset>"
          "<fieldset disabled><fieldset><legend><input id=e type=checkbox></legend></fieldset></fieldset>");
    ck_assert(!activate("a"));
    ck_assert(!activate("l"));
    checked("a", false);
    ck_assert(activate("b"));
    checked("b", true);
    ck_assert(!activate("c"));
    ck_assert(!activate("d"));
    ck_assert(!activate("e"));
}
END_TEST

START_TEST(test_live_state_preserves_default)
{
    parse("<input id=a type=checkbox checked>");
    ck_assert(activate("a"));
    checked("a", false);
    dom_node *n = node("a");
    dom_string *key = string("checked");
    bool present;
    ck_assert_int_eq(dom_element_has_attribute(n, key, &present), DOM_NO_ERR);
    ck_assert(present);
    ck_assert(activate("a"));
    checked("a", true);
    dom_string_unref(key);
    dom_node_unref(n);
}
END_TEST

struct change_log {
    dom_node *nodes[8];
    bool checked[8];
    unsigned count;
};

static void record_change(dom_node *node, void *pw)
{
    struct change_log *log = pw;
    ck_assert_msg(log->count < 8, "Unexpected number of checked-state changes");
    log->nodes[log->count] = dom_node_ref(node);
    ck_assert_int_eq(
        dom_html_input_element_get_checked((dom_html_input_element *)node, &log->checked[log->count]), DOM_NO_ERR);
    log->count++;
}

static void expect_change(struct change_log *log, unsigned index, const char *id, bool value)
{
    dom_node *expected = node(id);
    ck_assert_msg(index < log->count && log->nodes[index] == expected && log->checked[index] == value,
        "Notification %u should be %s checked=%d", index, id, value);
    dom_node_unref(expected);
}

static void clear_changes(struct change_log *log)
{
    for (unsigned i = 0; i < log->count; i++)
        dom_node_unref(log->nodes[i]);
    log->count = 0;
}

START_TEST(test_changed_peers)
{
    parse("<form id=f><input id=inside type=radio name=t checked>"
          "<input id=idle type=radio name=t></form>"
          "<div><input id=outside type=radio name=t form=f></div>"
          "<input id=unowned type=radio name=t checked>"
          "<input id=box type=checkbox><input id=disabled type=checkbox disabled>");
    struct change_log log = {0};
    dom_node *outside = node("outside"), *inside = node("inside"), *box = node("box");
    ck_assert(test_click(outside, record_change, &log).accepted);
    ck_assert_int_eq(log.count, 2);
    expect_change(&log, 0, "inside", false);
    expect_change(&log, 1, "outside", true);
    checked("unowned", true);
    dom_node *idle = node("idle");
    dom_string *checked_attribute = string("checked");
    ck_assert_int_eq(dom_element_set_attribute(idle, checked_attribute, checked_attribute), DOM_NO_ERR);
    dom_string_unref(checked_attribute);
    checked("idle", false); /* Group activation established live state, separate from the default. */
    dom_node_unref(idle);
    clear_changes(&log);
    ck_assert(test_click(outside, record_change, &log).accepted);
    ck_assert_int_eq(log.count, 0);
    ck_assert(test_click(inside, record_change, &log).accepted);
    ck_assert_int_eq(log.count, 2);
    expect_change(&log, 0, "outside", false);
    expect_change(&log, 1, "inside", true);
    clear_changes(&log);
    ck_assert(test_click(box, record_change, &log).accepted);
    ck_assert_int_eq(log.count, 1);
    expect_change(&log, 0, "box", true);
    clear_changes(&log);
    dom_node *disabled = node("disabled");
    ck_assert(!test_click(disabled, record_change, &log).accepted);
    ck_assert_int_eq(log.count, 0);
    dom_node_unref(disabled);
    dom_node_unref(box);
    dom_node_unref(inside);
    dom_node_unref(outside);
}
END_TEST

START_TEST(test_multiple_checked_peers)
{
    parse("<div><input id=a type=radio name=t checked></div>"
          "<div><input id=b type=radio name=t checked></div><input id=c type=radio name=t>");
    struct change_log log = {0};
    dom_node *target = node("c");
    ck_assert(test_click(target, record_change, &log).accepted);
    ck_assert_int_eq(log.count, 3);
    expect_change(&log, 0, "a", false);
    expect_change(&log, 1, "b", false);
    expect_change(&log, 2, "c", true);
    clear_changes(&log);
    dom_node_unref(target);
}
END_TEST

/* These callbacks use the real libdom dispatcher. JavaScript native input
 * delivery remains guarded until the browser interaction lifetime work lands. */
struct click_test {
    dom_node *input, *previous, *third;
    unsigned clicks, inputs, changes;
    bool cancel, checked_during, indeterminate_during, peer_during;
    void (*edit)(struct click_test *);
};

static void click_listener(dom_event *event, void *pw)
{
    struct click_test *test = pw;
    dom_string *type;
    ck_assert_int_eq(dom_event_get_type(event, &type), DOM_NO_ERR);
    bool value;
    ck_assert_int_eq(dom_event_get_bubbles(event, &value), DOM_NO_ERR);
    ck_assert(value);
    ck_assert_int_eq(dom_event_get_is_trusted(event, &value), DOM_NO_ERR);
    ck_assert(value);
    ck_assert_int_eq(dom_event_get_cancelable(event, &value), DOM_NO_ERR);
    ck_assert(value == dom_string_isequal(type, corestring_dom_click));
    if (dom_string_isequal(type, corestring_dom_click)) {
        test->clicks++;
        ck_assert_int_eq(
            dom_html_input_element_get_checked((dom_html_input_element *)test->input, &test->checked_during),
            DOM_NO_ERR);
        ck_assert_int_eq(dom_html_input_element_get_indeterminate(
                             (dom_html_input_element *)test->input, &test->indeterminate_during),
            DOM_NO_ERR);
        if (test->previous != NULL)
            ck_assert_int_eq(
                dom_html_input_element_get_checked((dom_html_input_element *)test->previous, &test->peer_during),
                DOM_NO_ERR);
        if (test->edit != NULL)
            test->edit(test);
        if (test->cancel)
            dom_event_prevent_default(event);
    } else if (dom_string_isequal(type, corestring_dom_input)) {
        ck_assert_uint_eq(test->clicks, 1);
        ck_assert_uint_eq(test->changes, 0);
        test->inputs++;
    } else if (dom_string_isequal(type, corestring_dom_change)) {
        ck_assert_uint_eq(test->inputs, 1);
        test->changes++;
    }
    dom_string_unref(type);
}

static bool click_with_probe(struct click_test *test, struct change_log *changes)
{
    dom_event_listener *listener;
    ck_assert_int_eq(dom_event_listener_create(click_listener, test, &listener), DOM_NO_ERR);
    dom_string *types[] = {corestring_dom_click, corestring_dom_input, corestring_dom_change};
    for (unsigned i = 0; i < 3; i++)
        ck_assert_int_eq(dom_event_target_add_event_listener(test->input, types[i], listener, false), DOM_NO_ERR);
    bool accepted = test_click(test->input, changes != NULL ? record_change : NULL, changes).accepted;
    for (unsigned i = 0; i < 3; i++)
        ck_assert_int_eq(dom_event_target_remove_event_listener(test->input, types[i], listener, false), DOM_NO_ERR);
    dom_event_listener_unref(listener);
    return accepted;
}

START_TEST(test_checkbox_click_transaction)
{
    parse("<input id=a type=checkbox checked><input id=b type=checkbox disabled>");
    dom_node *a = node("a"), *b = node("b");
    ck_assert_int_eq(dom_html_input_element_set_indeterminate((dom_html_input_element *)a, true), DOM_NO_ERR);
    struct change_log log = {0};
    struct click_test test = {.input = a, .cancel = true};
    ck_assert(!click_with_probe(&test, &log));
    ck_assert_uint_eq(test.clicks, 1);
    ck_assert(!test.checked_during && !test.indeterminate_during);
    checked("a", true);
    bool indeterminate = false;
    dom_html_input_element_get_indeterminate((dom_html_input_element *)a, &indeterminate);
    ck_assert(indeterminate);
    ck_assert_uint_eq(test.inputs + test.changes, 0);
    ck_assert_uint_eq(log.count, 2);
    expect_change(&log, 0, "a", false);
    expect_change(&log, 1, "a", true);
    clear_changes(&log);
    test = (struct click_test){.input = a};
    ck_assert(click_with_probe(&test, &log));
    checked("a", false);
    ck_assert_uint_eq(test.inputs, 1);
    ck_assert_uint_eq(test.changes, 1);
    dom_html_input_element_get_indeterminate((dom_html_input_element *)a, &indeterminate);
    ck_assert(!indeterminate);
    clear_changes(&log);
    test = (struct click_test){.input = b};
    ck_assert(!click_with_probe(&test, NULL));
    ck_assert_uint_eq(test.clicks + test.inputs + test.changes, 0);
    dom_node_unref(a);
    dom_node_unref(b);
}
END_TEST

static void nested_checkbox_click(struct click_test *test)
{
    if (test->clicks == 1) {
        ck_assert(test_click(test->input, NULL, NULL).accepted == false);
        /* Inner canceled click restores the outer provisional checked state. */
        bool checked;
        dom_html_input_element_get_checked((dom_html_input_element *)test->input, &checked);
        ck_assert(checked);
    }
}

START_TEST(test_nested_checkbox_cancellation)
{
    parse("<input id=a type=checkbox>");
    struct click_test test = {.input = node("a"), .cancel = true, .edit = nested_checkbox_click};
    ck_assert(!click_with_probe(&test, NULL));
    ck_assert_uint_eq(test.clicks, 2);
    ck_assert_uint_eq(test.inputs + test.changes, 0);
    checked("a", false);
    dom_node_unref(test.input);
}
END_TEST

START_TEST(test_radio_click_transaction)
{
    parse("<input id=a type=radio name=t checked disabled><input id=b type=radio name=t>");
    struct click_test test = {.input = node("b"), .previous = node("a"), .cancel = true};
    struct change_log log = {0};
    ck_assert(!click_with_probe(&test, &log));
    ck_assert(test.checked_during && !test.peer_during);
    ck_assert_uint_eq(test.inputs + test.changes, 0);
    checked("a", true);
    checked("b", false);
    ck_assert_uint_eq(log.count, 4);
    expect_change(&log, 0, "a", false);
    expect_change(&log, 1, "b", true);
    expect_change(&log, 2, "b", false);
    expect_change(&log, 3, "a", true);
    clear_changes(&log);
    test.cancel = false;
    test.clicks = 0;
    ck_assert(click_with_probe(&test, NULL));
    ck_assert_uint_eq(test.inputs, 1);
    ck_assert_uint_eq(test.changes, 1);
    checked("a", false);
    checked("b", true);
    test.cancel = true;
    test.clicks = test.inputs = test.changes = 0;
    ck_assert(!click_with_probe(&test, NULL));
    checked("b", true);
    ck_assert_uint_eq(test.inputs + test.changes, 0);
    /* The HTML input activation steps also run for an already checked radio. */
    test.cancel = false;
    test.clicks = 0;
    ck_assert(click_with_probe(&test, NULL));
    checked("b", true);
    ck_assert_uint_eq(test.inputs, 1);
    ck_assert_uint_eq(test.changes, 1);
    dom_node_unref(test.input);
    dom_node_unref(test.previous);
}
END_TEST

static void set_attribute(dom_node *node, const char *key, const char *value)
{
    dom_string *name = string(key), *text = string(value);
    ck_assert_int_eq(dom_element_set_attribute(node, name, text), DOM_NO_ERR);
    dom_string_unref(name);
    dom_string_unref(text);
}

static void rename_peer(struct click_test *test)
{
    set_attribute(test->previous, "name", "other");
}
static void reassign_peer(struct click_test *test)
{
    set_attribute(test->previous, "form", "other");
}
static void retype_peer(struct click_test *test)
{
    set_attribute(test->previous, "type", "checkbox");
}
static void retype_input(struct click_test *test)
{
    set_attribute(test->input, "type", "text");
}
static void disable_input(struct click_test *test)
{
    set_attribute(test->input, "disabled", "");
}
static void rename_input(struct click_test *test)
{
    set_attribute(test->input, "name", "other");
}
static void select_third(struct click_test *test)
{
    ck_assert(test_click(test->third, NULL, NULL).accepted);
}

static void detach(dom_node *node)
{
    dom_node *parent, *removed;
    ck_assert_int_eq(dom_node_get_parent_node(node, &parent), DOM_NO_ERR);
    ck_assert_ptr_nonnull(parent);
    ck_assert_int_eq(dom_node_remove_child(parent, node, &removed), DOM_NO_ERR);
    dom_node_unref(removed);
    dom_node_unref(parent);
}
static void detach_peer(struct click_test *test)
{
    detach(test->previous);
}
static void detach_input(struct click_test *test)
{
    detach(test->input);
}

static const struct {
    const char *name;
    void (*edit)(struct click_test *);
    bool restores_peer;
} group_edits[] = {{"rename peer", rename_peer, false}, {"reassign peer", reassign_peer, false},
    {"retype peer", retype_peer, false}, {"detach peer", detach_peer, false},
    {"detach input", detach_input, false}, {"rename input", rename_input, false},
    {"select third radio", select_third, true}};

START_TEST(test_radio_canceled_group_edits)
{
    parse("<form id=f><input id=a type=radio name=t checked><input id=b type=radio name=t>"
          "<input id=c type=radio name=t></form><form id=other></form>");
    struct click_test test = {
        .input = node("b"), .previous = node("a"), .third = node("c"), .cancel = true, .edit = group_edits[_i].edit};
    ck_assert(!click_with_probe(&test, NULL));
    bool value;
    dom_html_input_element_get_checked((dom_html_input_element *)test.input, &value);
    ck_assert_msg(!value, "%s: canceled input remained checked", group_edits[_i].name);
    dom_html_input_element_get_checked((dom_html_input_element *)test.previous, &value);
    ck_assert_msg(value == group_edits[_i].restores_peer, "%s: unexpected restored peer state", group_edits[_i].name);
    checked("c", false);
    ck_assert_uint_eq(test.inputs + test.changes, 0);
    dom_node_unref(test.input);
    dom_node_unref(test.previous);
    dom_node_unref(test.third);
}
END_TEST

static void release_peer(struct click_test *test)
{
    detach(test->previous);
    dom_node_unref(test->previous);
    test->previous = NULL;
}

START_TEST(test_radio_saved_peer_lifetime)
{
    parse("<input id=a type=radio name=t checked><input id=b type=radio name=t>");
    struct click_test test = {.input = node("b"), .previous = node("a"), .cancel = true, .edit = release_peer};
    ck_assert(!click_with_probe(&test, NULL));
    ck_assert_ptr_null(test.previous);
    checked("b", false);
    ck_assert_uint_eq(test.inputs + test.changes, 0);
    dom_node_unref(test.input);
}
END_TEST

START_TEST(test_click_detached_or_changed_type)
{
    parse("<input id=a type=checkbox>");
    struct click_test test = {.input = node("a"), .edit = _i == 0 ? detach_input : retype_input};
    ck_assert(click_with_probe(&test, NULL));
    ck_assert(test.checked_during);
    ck_assert_uint_eq(test.inputs + test.changes, 0);
    dom_node_unref(test.input);
}
END_TEST

START_TEST(test_detached_radio_group)
{
    parse("<div id=root><input id=a type=radio name=t checked><input id=b type=radio name=t></div>"
          "<input id=c type=radio name=t checked>");
    dom_node *root = node("root"), *a = node("a"), *b = node("b");
    detach(root);
    struct click_test test = {.input = b, .previous = a, .cancel = true};
    ck_assert(!click_with_probe(&test, NULL));
    ck_assert(test.checked_during && !test.peer_during);
    bool value;
    dom_html_input_element_get_checked((dom_html_input_element *)a, &value);
    ck_assert(value);
    dom_html_input_element_get_checked((dom_html_input_element *)b, &value);
    ck_assert(!value);
    checked("c", true);
    dom_node_unref(root);
    dom_node_unref(a);
    dom_node_unref(b);
}
END_TEST

START_TEST(test_disable_during_click)
{
    parse("<input id=a type=checkbox><input id=b type=radio name=t>");
    struct click_test test = {.input = node(_i == 0 ? "a" : "b"), .edit = disable_input};
    ck_assert(click_with_probe(&test, NULL));
    ck_assert(test.checked_during);
    ck_assert_uint_eq(test.inputs, 1);
    ck_assert_uint_eq(test.changes, 1);
    dom_node_unref(test.input);
}
END_TEST

START_TEST(test_indeterminate_state)
{
    parse("<input id=a type=checkbox checked>");
    dom_node *a = node("a"), *clone = NULL;
    bool value = true;
    dom_html_input_element_get_indeterminate((dom_html_input_element *)a, &value);
    ck_assert(!value);
    dom_html_input_element_set_indeterminate((dom_html_input_element *)a, true);
    checked("a", true);
    ck_assert_int_eq(dom_node_clone_node(a, false, &clone), DOM_NO_ERR);
    dom_html_input_element_get_checked((dom_html_input_element *)clone, &value);
    ck_assert(value);
    dom_html_input_element_get_indeterminate((dom_html_input_element *)clone, &value);
    ck_assert(value);
    dom_html_input_element_set_indeterminate((dom_html_input_element *)clone, false);
    dom_html_input_element_get_indeterminate((dom_html_input_element *)a, &value);
    ck_assert(value);
    dom_node_unref(a);
    dom_node_unref(clone);
}
END_TEST

/* Label forwarding follows HTML's label association/activation rules. The DOM
 * dispatch activation element is frozen before listeners change the tree. */
enum label_edit {
    LABEL_UNCHANGED, LABEL_RETARGET, LABEL_REMOVE_CONTROL, LABEL_RETYPE_CONTROL,
    LABEL_DISABLE_CONTROL, LABEL_REMOVE_FOR, LABEL_MOVE_TARGET, LABEL_DETACH,
    LABEL_DETACH_TREE, LABEL_CREATE_ASSOCIATION, LABEL_REMOVE_AND_RELEASE, LABEL_ADD_LINK, LABEL_MOVE_INTO_BUTTON
};

struct label_probe {
    dom_node *label, *hit, *other, *root, *a;
    enum label_edit edit;
    bool cancel, stop;
    unsigned calls;
};

static void label_click_listener(dom_event *event, void *pw)
{
    struct label_probe *probe = pw;
    probe->calls++;
    switch (probe->edit) {
    case LABEL_UNCHANGED:
        break;
    case LABEL_RETARGET:
        set_attribute(probe->label, "for", "b");
        break;
    case LABEL_REMOVE_CONTROL:
        detach(probe->a);
        break;
    case LABEL_RETYPE_CONTROL:
        set_attribute(probe->a, "type", "text");
        break;
    case LABEL_DISABLE_CONTROL:
        set_attribute(probe->a, "disabled", "");
        break;
    case LABEL_REMOVE_FOR: {
        dom_string *name = string("for");
        ck_assert_int_eq(dom_element_remove_attribute(probe->label, name), DOM_NO_ERR);
        dom_string_unref(name);
        break;
    }
    case LABEL_MOVE_TARGET:
    case LABEL_MOVE_INTO_BUTTON: {
        dom_node *result;
        dom_node *parent = probe->edit == LABEL_MOVE_TARGET ? dom_node_ref(probe->other) : node("other_button");
        ck_assert_int_eq(dom_node_append_child(parent, probe->hit, &result), DOM_NO_ERR);
        dom_node_unref(result);
        dom_node_unref(parent);
        break;
    }
    case LABEL_ADD_LINK: {
        dom_node *parent;
        ck_assert_int_eq(dom_node_get_parent_node(probe->hit, &parent), DOM_NO_ERR);
        set_attribute(parent, "href", "#destination");
        dom_node_unref(parent);
        break;
    }
    case LABEL_DETACH:
        detach(probe->label);
        break;
    case LABEL_DETACH_TREE:
        detach(probe->root);
        break;
    case LABEL_CREATE_ASSOCIATION:
        set_attribute(probe->label, "for", "a");
        break;
    case LABEL_REMOVE_AND_RELEASE:
        detach(probe->label);
        dom_node_unref(probe->label);
        probe->label = NULL;
        break;
    }
    if (probe->stop)
        ck_assert_int_eq(dom_event_stop_propagation(event), DOM_NO_ERR);
    if (probe->cancel)
        ck_assert_int_eq(dom_event_prevent_default(event), DOM_NO_ERR);
}

START_TEST(test_plain_click_cancellation)
{
    parse("<a id=hit href='#destination'>Link</a>");
    struct label_probe probe = {.hit = node("hit"), .cancel = _i == 0, .stop = _i != 0};
    dom_event_listener *listener;
    ck_assert_int_eq(dom_event_listener_create(label_click_listener, &probe, &listener), DOM_NO_ERR);
    dom_event_target_add_event_listener(probe.hit, corestring_dom_click, listener, false);
    struct html_form_click_result result = test_click(probe.hit, NULL, NULL);
    ck_assert(!result.handled);
    ck_assert(result.accepted == !probe.cancel);
    ck_assert_uint_eq(probe.calls, 1);
    dom_event_target_remove_event_listener(probe.hit, corestring_dom_click, listener, false);
    dom_event_listener_unref(listener);
    dom_node_unref(probe.hit);
}
END_TEST

static const struct {
    const char *name;
    enum label_edit edit;
    int forwarded; /* a=0, b=1, c=2; -1 means no control receives a click. */
    bool cancel_label, cancel_control, stop;
} label_cases[] = {
    {"ordinary label", LABEL_UNCHANGED, 0},
    {"changed for", LABEL_RETARGET, 1},
    {"descendant becomes interactive", LABEL_ADD_LINK, -1},
    {"new interactive ancestor outside original label", LABEL_MOVE_INTO_BUTTON, 0},
    {"removed control", LABEL_REMOVE_CONTROL, -1},
    {"changed control type", LABEL_RETYPE_CONTROL, -1},
    {"disabled control", LABEL_DISABLE_CONTROL, -1},
    {"removed for uses first descendant", LABEL_REMOVE_FOR, 2},
    {"original label survives target reparenting", LABEL_MOVE_TARGET, 0},
    {"detached label cannot address document", LABEL_DETACH, -1},
    {"detached shared tree retains association", LABEL_DETACH_TREE, 0},
    {"association created during click", LABEL_CREATE_ASSOCIATION, 0},
    {"released label remains safe", LABEL_REMOVE_AND_RELEASE, -1},
    {"canceled original click", LABEL_UNCHANGED, -1, true},
    {"canceled forwarded click", LABEL_UNCHANGED, 0, false, true},
    {"stopPropagation still activates", LABEL_UNCHANGED, 0, false, false, true},
};

START_TEST(test_label_callback_activation)
{
    parse("<div id=root><input id=a type=checkbox><input id=b type=checkbox>"
          "<label id=l for=a><a><span id=hit>Label</span></a><input id=c type=checkbox></label>"
          "<label id=other for=b><button id=other_button>Other</button></label></div>");
    struct label_probe probe = {.label = node("l"), .hit = node("hit"), .other = node("other"),
        .root = node("root"), .a = node("a"), .edit = label_cases[_i].edit,
        .cancel = label_cases[_i].cancel_label, .stop = label_cases[_i].stop};
    if (probe.edit == LABEL_CREATE_ASSOCIATION)
        set_attribute(probe.label, "for", "missing");
    dom_event_listener *original, *listeners[3];
    struct click_test controls[] = {{.input = dom_node_ref(probe.a), .cancel = label_cases[_i].cancel_control},
        {.input = node("b")}, {.input = node("c")}};
    dom_string *types[] = {corestring_dom_click, corestring_dom_input, corestring_dom_change};
    ck_assert_int_eq(dom_event_listener_create(label_click_listener, &probe, &original), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(probe.hit, corestring_dom_click, original, false), DOM_NO_ERR);
    for (unsigned i = 0; i < 3; i++) {
        ck_assert_int_eq(dom_event_listener_create(click_listener, &controls[i], &listeners[i]), DOM_NO_ERR);
        for (unsigned j = 0; j < 3; j++)
            ck_assert_int_eq(dom_event_target_add_event_listener(controls[i].input, types[j], listeners[i], false),
                DOM_NO_ERR);
    }
    struct html_form_click_result result = test_click(probe.hit, NULL, NULL);
    ck_assert_msg(result.handled && result.accepted == !probe.cancel,
        "%s: wrong original click result", label_cases[_i].name);
    ck_assert_uint_eq(probe.calls, 1);
    for (unsigned i = 0; i < 3; i++) {
        bool forwarded = label_cases[_i].forwarded == (int)i;
        bool changed = forwarded && !controls[i].cancel;
        unsigned followup = changed && probe.edit != LABEL_DETACH_TREE;
        ck_assert_msg(controls[i].clicks == (unsigned)forwarded && controls[i].inputs == followup &&
                controls[i].changes == followup, "%s: wrong control %u event counts", label_cases[_i].name, i);
        bool value;
        ck_assert_int_eq(dom_html_input_element_get_checked((dom_html_input_element *)controls[i].input, &value),
            DOM_NO_ERR);
        ck_assert_msg(value == changed, "%s: wrong control %u checkedness", label_cases[_i].name, i);
        for (unsigned j = 0; j < 3; j++)
            dom_event_target_remove_event_listener(controls[i].input, types[j], listeners[i], false);
        dom_event_listener_unref(listeners[i]);
        dom_node_unref(controls[i].input);
    }
    dom_event_target_remove_event_listener(probe.hit, corestring_dom_click, original, false);
    dom_event_listener_unref(original);
    dom_node_unref(probe.label);
    dom_node_unref(probe.hit);
    dom_node_unref(probe.other);
    dom_node_unref(probe.root);
    dom_node_unref(probe.a);
}
END_TEST

static const struct {
    const char *html;
    bool activates;
} label_content_cases[] = {
    {"<label for=a><audio controls><span id=hit>Text</span></audio></label>", false},
    {"<label for=a><video controls><span id=hit>Text</span></video></label>", false},
    {"<label for=a><audio><span id=hit>Text</span></audio></label>", true},
    {"<label for=a><video><span id=hit>Text</span></video></label>", true},
    {"<label for=a><details><summary><span id=hit>Text</span></summary></details></label>", false},
    {"<label for=a><embed id=hit></label>", false},
    {"<label for=a><iframe id=hit></iframe></label>", false},
    {"<label for=a><img id=hit usemap='#map'></label>", false},
    {"<label for=a><img id=hit></label>", true},
    {"<label for=a><output><span id=hit>Text</span></output></label>", true},
    {"<label for=a><a href=''><span id=hit>Text</span></a></label>", false},
    {"<label for=a><a><span id=hit>Text</span></a></label>", true},
    {"<label id=hit><meter></meter><input id=a type=checkbox></label>", false},
    {"<label id=hit><progress></progress><input id=a type=checkbox></label>", false},
    {"<label id=hit><output></output><input id=a type=checkbox></label>", false},
    {"<label id=hit for=a></label><div id=a></div>", false},
    {"<label id=hit for=''></label><input id='' type=checkbox>", false},
};

START_TEST(test_label_content_rules)
{
    char html[512];
    snprintf(html, sizeof(html), "%s<input id=a type=checkbox>", label_content_cases[_i].html);
    parse(html);
    ck_assert_msg(activate("hit") == label_content_cases[_i].activates,
        "Unexpected label activation: %s", label_content_cases[_i].html);
}
END_TEST

START_TEST(test_detached_label_duplicate_ids)
{
    parse("<input id=a type=checkbox><div id=root><label id=hit for=a>Label</label>"
          "<div id=first></div><input id=inside type=checkbox></div>");
    dom_node *root = node("root"), *hit = node("hit"), *inside = node("inside"), *first = node("first");
    detach(root);
    set_attribute(inside, "id", "a");
    if (_i != 0)
        set_attribute(first, "id", "a");
    bool changed = false;
    struct html_form_click_result result = test_click(hit, note_change, &changed);
    ck_assert(result.accepted && result.handled);
    ck_assert_msg(changed == (_i == 0), "Only the first matching ID in the label's tree may be used");
    checked("a", false);
    bool value;
    dom_html_input_element_get_checked((dom_html_input_element *)inside, &value);
    ck_assert(value == (_i == 0));
    dom_node_unref(root);
    dom_node_unref(hit);
    dom_node_unref(inside);
    dom_node_unref(first);
}
END_TEST

int main(void)
{
    if (corestrings_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("Native checked-state activation");
    TCase *tc = tcase_create("DOM controls and labels");
    tcase_add_checked_fixture(tc, NULL, teardown);
    tcase_add_test(tc, test_radio_groups);
    tcase_add_test(tc, test_labels);
    tcase_add_test(tc, test_disabled);
    tcase_add_test(tc, test_live_state_preserves_default);
    tcase_add_test(tc, test_changed_peers);
    tcase_add_test(tc, test_multiple_checked_peers);
    tcase_add_test(tc, test_checkbox_click_transaction);
    tcase_add_test(tc, test_nested_checkbox_cancellation);
    tcase_add_test(tc, test_radio_click_transaction);
    tcase_add_loop_test(tc, test_radio_canceled_group_edits, 0, sizeof(group_edits) / sizeof(group_edits[0]));
    tcase_add_test(tc, test_radio_saved_peer_lifetime);
    tcase_add_loop_test(tc, test_click_detached_or_changed_type, 0, 2);
    tcase_add_test(tc, test_detached_radio_group);
    tcase_add_loop_test(tc, test_disable_during_click, 0, 2);
    tcase_add_test(tc, test_indeterminate_state);
    tcase_add_loop_test(tc, test_plain_click_cancellation, 0, 2);
    tcase_add_loop_test(tc, test_label_callback_activation, 0, sizeof(label_cases) / sizeof(label_cases[0]));
    tcase_add_loop_test(tc, test_label_content_rules, 0, sizeof(label_content_cases) / sizeof(label_content_cases[0]));
    tcase_add_loop_test(tc, test_detached_label_duplicate_ids, 0, 2);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    corestrings_fini();
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
