/* HTML form-owner reset rules, exercised through public DOM APIs and Hubbub.
 * https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#association-of-controls-and-forms */
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <dom/dom.h>
#include <libdom/bindings/hubbub/parser.h>
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

static dom_document *document;
static dom_node *held[64];
static size_t held_count;
static const char *tags[] = {"input", "button", "select", "textarea", "fieldset", "object"};
enum { CONTROL_TYPES = sizeof(tags) / sizeof(tags[0]), PARSER_TYPES = 4 };

static dom_string *string(const char *text)
{
    dom_string *result;
    ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &result), DOM_NO_ERR);
    return result;
}

static dom_node *hold(dom_node *node)
{
    ck_assert_ptr_nonnull(node);
    ck_assert(held_count < sizeof(held) / sizeof(held[0]));
    held[held_count++] = node;
    return node;
}

static void setup(void)
{
    held_count = 0;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document),
        DOM_NO_ERR);
}

static void teardown(void)
{
    for (size_t i = 0; i < held_count; i++)
        dom_node_unref(held[i]);
    dom_node_unref(document);
}

static dom_node *create(const char *tag)
{
    dom_string *name = string(tag);
    dom_node *node;
    ck_assert_int_eq(dom_document_create_element(document, name, &node), DOM_NO_ERR);
    dom_string_unref(name);
    return hold(node);
}

static void append(dom_node *parent, dom_node *node)
{
    dom_node *result;
    ck_assert_int_eq(dom_node_append_child(parent, node, &result), DOM_NO_ERR);
    dom_node_unref(result);
}

static void remove_node(dom_node *node)
{
    dom_node *parent, *result;
    ck_assert_int_eq(dom_node_get_parent_node(node, &parent), DOM_NO_ERR);
    ck_assert_ptr_nonnull(parent);
    ck_assert_int_eq(dom_node_remove_child(parent, node, &result), DOM_NO_ERR);
    dom_node_unref(result);
    dom_node_unref(parent);
}

static void set(dom_node *node, const char *key, const char *text)
{
    dom_string *name = string(key);
    if (text == NULL) {
        ck_assert_int_eq(dom_element_remove_attribute(node, name), DOM_NO_ERR);
    } else {
        dom_string *value = string(text);
        ck_assert_int_eq(dom_element_set_attribute(node, name, value), DOM_NO_ERR);
        dom_string_unref(value);
    }
    dom_string_unref(name);
}

static dom_node *by_id(const char *text)
{
    dom_node *node;
    dom_string *id = string(text);
    ck_assert_int_eq(dom_document_get_element_by_id(document, id, &node), DOM_NO_ERR);
    dom_string_unref(id);
    return hold(node);
}

static void expect(dom_node *node, dom_node *want, const char *reason)
{
    dom_html_form_element *form = NULL;
    dom_html_element_type tag;
    dom_exception error = DOM_NOT_SUPPORTED_ERR;
    ck_assert_int_eq(dom_html_element_get_tag_type(node, &tag), DOM_NO_ERR);
    switch (tag) {
    case DOM_HTML_ELEMENT_TYPE_INPUT:
        error = dom_html_input_element_get_form((dom_html_input_element *)node, &form);
        break;
    case DOM_HTML_ELEMENT_TYPE_BUTTON:
        error = dom_html_button_element_get_form((dom_html_button_element *)node, &form);
        break;
    case DOM_HTML_ELEMENT_TYPE_SELECT:
        error = dom_html_select_element_get_form((dom_html_select_element *)node, &form);
        break;
    case DOM_HTML_ELEMENT_TYPE_TEXTAREA:
        error = dom_html_text_area_element_get_form((dom_html_text_area_element *)node, &form);
        break;
    case DOM_HTML_ELEMENT_TYPE_FIELDSET:
        error = dom_html_field_set_element_get_form((dom_html_field_set_element *)node, &form);
        break;
    case DOM_HTML_ELEMENT_TYPE_OBJECT:
        error = dom_html_object_element_get_form((dom_html_object_element *)node, &form);
        break;
    default:
        break;
    }
    ck_assert_msg(error == DOM_NO_ERR && (dom_node *)form == want, "%s: tag=%d error=%d expected owner %p, got %p",
        reason, tag, error, (void *)want, (void *)form);
    dom_node_unref(form);
}

static dom_node *root(void)
{
    dom_node *node = create("div");
    append((dom_node *)document, node);
    return node;
}

START_TEST(test_live_ordinary_ownership)
{
    dom_node *r = root(), *a = create("form"), *b = create("form"), *wrap = create("div");
    dom_node *control = create(tags[_i]);
    set(a, "id", "a");
    set(b, "id", "b");
    append(r, a);
    append(r, b);
    append(a, wrap);
    append(wrap, control);
    expect(control, a, "created control finds ancestor");
    append(b, wrap);
    expect(control, b, "moving ancestor resets owner");
    set(control, "form", "a");
    expect(control, a, "explicit owner overrides ancestor");
    set(a, "id", "renamed");
    expect(control, NULL, "ID change invalidates explicit owner");
    set(control, "form", "missing");
    expect(control, NULL, "unresolved explicit owner does not fall back");
    set(control, "form", "");
    expect(control, NULL, "empty explicit owner does not fall back");
    set(a, "id", "");
    expect(control, NULL, "empty ID attribute does not establish an ID for form=");
    dom_string *empty = string("");
    dom_node *matched = NULL;
    ck_assert_int_eq(dom_document_get_element_by_id(document, empty, &matched), DOM_NO_ERR);
    ck_assert_ptr_null(matched);
    dom_string_unref(empty);
    remove_node(b);
    expect(control, b, "disconnected form= uses nearest ancestor");
    append(r, b);
    expect(control, NULL, "reconnection restores explicit-owner rules");
    set(control, "form", NULL);
    expect(control, b, "removing form= restores ancestor");
    remove_node(control);
    expect(control, NULL, "detached control has no owner");
}
END_TEST

START_TEST(test_first_matching_id)
{
    dom_node *r = root(), *blocker = create("div"), *a = create("form"), *b = create("form");
    dom_node *control = create(tags[_i]);
    append(r, blocker);
    append(r, a);
    append(r, b);
    append(a, control);
    set(blocker, "id", "match");
    set(a, "id", "match");
    set(b, "id", "match");
    set(control, "form", "match");
    expect(control, NULL, "first ID match is not a form");
    remove_node(blocker);
    expect(control, a, "removing earlier ID reveals first form");
    dom_node *result;
    ck_assert_int_eq(dom_node_insert_before(r, b, a, &result), DOM_NO_ERR);
    dom_node_unref(result);
    expect(control, b, "reordering duplicate IDs changes owner");
    set(b, "id", "MATCH");
    expect(control, a, "form IDs are case-sensitive");
}
END_TEST

static void parse(const char *source)
{
    dom_node_unref(document);
    dom_hubbub_parser *parser;
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &document), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)source, strlen(source)), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
}

static const char *control_markup[] = {"<input id=control>", "<button id=control>button</button>",
    "<select id=control><option>one</option></select>", "<textarea id=control>text</textarea>"};

static void parse_control(unsigned type)
{
    char source[1024];
    snprintf(source, sizeof(source),
        "<!doctype html><div id=group><table><form id=parser>"
        "<tr><td>%s</td></tr></table></form></div><form id=other></form>",
        control_markup[type]);
    parse(source);
}

START_TEST(test_parser_association_and_reset)
{
    parse_control(_i);
    dom_node *control = by_id("control"), *form = by_id("parser"), *other = by_id("other"), *group = by_id("group");
    expect(control, form, "preserve non-ancestor parser association");
    set(control, "class", "unrelated");
    expect(control, form, "unrelated edits preserve parser association");
    remove_node(group);
    expect(control, form, "removing common subtree preserves parser association");
    append(other, group);
    expect(control, form, "reinserting common subtree preserves parser association");
    append(other, control);
    expect(control, other, "moving control separately resets parser association");
    dom_node *replacement = create(tags[_i]), *old;
    ck_assert_int_eq(dom_node_replace_child(other, replacement, control, &old), DOM_NO_ERR);
    dom_node_unref(old);
    expect(control, NULL, "replaced control is detached");
    expect(replacement, other, "replacement acquires new owner");
}
END_TEST

START_TEST(test_parser_clone)
{
    parse_control(_i);
    dom_node *control = by_id("control"), *form = by_id("parser"), *other = by_id("other"), *clone;
    expect(control, form, "original parser association");
    ck_assert_int_eq(dom_node_clone_node(control, true, &clone), DOM_NO_ERR);
    hold(clone);
    expect(clone, NULL, "clone does not inherit parser association");
    append(other, clone);
    expect(clone, other, "inserted clone acquires its own owner");
    remove_node(form);
    expect(control, form, "removing non-ancestor owner alone does not reset control");
}
END_TEST

START_TEST(test_parser_owner_becomes_ancestor)
{
    parse_control(_i);
    dom_node *control = by_id("control"), *form = by_id("parser");
    expect(control, form, "initial non-ancestor owner");
    append(form, control);
    expect(control, form, "moving into parser owner creates ordinary ancestor association");
    remove_node(form);
    expect(control, form, "detached ancestor remains owner without a reference cycle");
    /* Fixture teardown and LeakSanitizer also check that the former retained
     * parser reference cannot keep this ancestor and its children alive. */
}
END_TEST

START_TEST(test_fragment_reparenting)
{
    parse_control(_i % PARSER_TYPES);
    dom_node *control = by_id("control"), *form = by_id("parser"), *other = by_id("other"), *group = by_id("group");
    dom_node *fragment;
    ck_assert_int_eq(dom_document_create_document_fragment(document, &fragment), DOM_NO_ERR);
    hold(fragment);
    append(fragment, group);
    append(fragment, create("span"));
    expect(control, form, "moving common subtree to fragment preserves parser owner");
    if (_i < PARSER_TYPES) {
        append(other, fragment);
    } else {
        dom_node *placeholder = create("div"), *old;
        append(other, placeholder);
        ck_assert_int_eq(dom_node_replace_child(other, fragment, placeholder, &old), DOM_NO_ERR);
        dom_node_unref(old);
    }
    expect(control, form, "fragment insertion preserves association inside common subtree");
    append(fragment, control);
    expect(control, NULL, "moving control alone into fragment resets parser owner");
    append(other, fragment);
    expect(control, other, "fragment insertion establishes new ancestor owner");
}
END_TEST

START_TEST(test_form_attribute_discards_parser_identity)
{
    parse_control(_i % PARSER_TYPES);
    dom_node *control = by_id("control"), *form = by_id("parser"), *other = by_id("other");
    expect(control, form, "initial parser association");
    dom_string *name = string("form"), *value = string("other");
    dom_attr *attr = NULL, *old = NULL;
    if (_i < PARSER_TYPES) {
        ck_assert_int_eq(dom_element_set_attribute_ns(control, NULL, name, value), DOM_NO_ERR);
    } else {
        ck_assert_int_eq(dom_document_create_attribute(document, name, &attr), DOM_NO_ERR);
        ck_assert_int_eq(dom_attr_set_value(attr, value), DOM_NO_ERR);
        ck_assert_int_eq(dom_element_set_attribute_node(control, attr, &old), DOM_NO_ERR);
        dom_node_unref(old);
    }
    expect(control, other, "explicit owner overrides parser association");
    if (attr != NULL) {
        ck_assert_int_eq(dom_element_remove_attribute_node(control, attr, &old), DOM_NO_ERR);
        dom_node_unref(old);
        dom_node_unref(attr);
    } else {
        ck_assert_int_eq(dom_element_remove_attribute_ns(control, NULL, name), DOM_NO_ERR);
    }
    expect(control, NULL, "removing explicit owner must not revive parser identity");
    dom_string_unref(value);
    dom_string_unref(name);
}
END_TEST

START_TEST(test_attribute_value_and_namespace)
{
    dom_node *r = root(), *a = create("form"), *b = create("form"), *control = create(tags[_i]);
    append(r, a);
    append(r, b);
    append(a, control);
    set(a, "id", "a");
    set(b, "id", "b");
    dom_string *ns = string("urn:test"), *name = string("form"), *value = string("b");
    ck_assert_int_eq(dom_element_set_attribute_ns(control, ns, name, value), DOM_NO_ERR);
    expect(control, a, "namespaced form attribute is unrelated");
    set(control, "form", "a");
    dom_attr *attr;
    ck_assert_int_eq(dom_element_get_attribute_node(control, name, &attr), DOM_NO_ERR);
    ck_assert_int_eq(dom_attr_set_value(attr, value), DOM_NO_ERR);
    expect(control, b, "Attr.value updates ownership");
    dom_node_unref(attr);
    dom_string_unref(ns);
    dom_string_unref(name);
    dom_string_unref(value);
}
END_TEST

static void expect_collection(dom_html_collection *col, dom_node *first, dom_node *second, const char *why)
{
    uint32_t length;
    ck_assert_int_eq(dom_html_collection_get_length(col, &length), DOM_NO_ERR);
    ck_assert_msg(length == (first == NULL ? 0 : second == NULL ? 1 : 2), "%s: length=%u", why, length);
    dom_node *node;
    ck_assert_int_eq(dom_html_collection_item(col, 0, &node), DOM_NO_ERR);
    ck_assert_msg(node == first, "%s: first element", why);
    dom_node_unref(node);
    ck_assert_int_eq(dom_html_collection_item(col, 1, &node), DOM_NO_ERR);
    ck_assert_msg(node == second, "%s: second element", why);
    dom_node_unref(node);
}

START_TEST(test_live_form_collection)
{
    dom_node *r = root(), *a = create("form"), *b = create("form"), *inside = create(tags[_i]),
             *outside = create(tags[_i]);
    set(a, "id", "a");
    append(r, a);
    append(r, b);
    append(a, inside);
    append(r, outside);
    set(outside, "form", "a");
    set(outside, "name", "outside");
    dom_html_collection *col;
    ck_assert_int_eq(dom_html_form_element_get_elements((dom_html_form_element *)a, &col), DOM_NO_ERR);
    expect_collection(col, inside, outside, "connected controls including external form=");
    dom_string *name = string("outside");
    dom_node *named;
    ck_assert_int_eq(dom_html_collection_named_item(col, name, &named), DOM_NO_ERR);
    ck_assert_ptr_eq(named, outside);
    dom_node_unref(named);
    dom_string_unref(name);
    remove_node(a);
    expect_collection(col, inside, NULL, "detached form collection follows its tree");
    append(r, a);
    expect_collection(col, outside, inside, "reinserted collection follows tree order");
    append(b, inside);
    expect_collection(col, outside, NULL, "moved control leaves old form collection");
    set(a, "id", "renamed");
    expect_collection(col, NULL, NULL, "ID edits update live collection");
    append(a, inside);
    remove_node(a);
    /* The collection must itself retain its form/predicate context. Drop all
     * caller-held nodes and the document before querying the detached tree. */
    for (size_t i = 0; i < held_count; i++)
        dom_node_unref(held[i]);
    held_count = 0;
    dom_node_unref(document);
    document = NULL;
    expect_collection(col, inside, NULL, "collection alone keeps detached form and controls alive");
    dom_html_collection_unref(col);
}
END_TEST

START_TEST(test_surviving_control_and_form_lifetimes)
{
    parse_control(_i);
    dom_node *control = by_id("control"), *form = by_id("parser");
    remove_node(form);
    /* Release every external form reference. The non-ancestor parser identity
     * must keep the detached form alive until ownership is reset. */
    for (size_t i = 0; i < held_count; i++) {
        if (held[i] == form) {
            dom_node_unref(held[i]);
            held[i] = NULL;
        }
    }
    expect(control, form, "detached parser owner remains alive");
    set(control, "form", "missing");
    expect(control, NULL, "reset releases detached parser owner");
    /* No use of form beyond this point: clearing ownership may destroy it. */
}
END_TEST

static void expect_snapshot(dom_html_form_control_list *controls, dom_node **expected, size_t count, const char *why)
{
    ck_assert_msg(controls->length == count, "%s: expected %zu controls, got %zu", why, count, controls->length);
    for (size_t i = 0; i < count; i++)
        ck_assert_msg(controls->items[i] == expected[i], "%s: wrong node at index %zu", why, i);
}

START_TEST(test_submission_snapshot_inventory)
{
    dom_node *r = root(), *form = create("form"), *outside = create("input");
    set(form, "id", "form");
    set(outside, "form", "form");
    append(r, outside);
    append(r, form);
    dom_node *expected[6] = {outside};
    for (unsigned i = 0; i < PARSER_TYPES; i++) {
        expected[i + 1] = create(tags[i]);
        append(form, expected[i + 1]);
    }
    expected[5] = create("input");
    set(expected[5], "type", "ImAgE");
    append(form, expected[5]);
    const char *excluded[] = {"fieldset", "object", "output", "img", "div"};
    for (size_t i = 0; i < sizeof(excluded) / sizeof(*excluded); i++)
        append(form, create(excluded[i]));
    dom_string *ns = string("urn:foreign"), *name = string("input");
    dom_node *foreign;
    ck_assert_int_eq(dom_document_create_element_ns(document, ns, name, &foreign), DOM_NO_ERR);
    append(form, hold(foreign));
    dom_string_unref(ns);
    dom_string_unref(name);
    dom_html_form_control_list controls;
    ck_assert_int_eq(dom_html_form_element_get_submittable_elements((dom_html_form_element *)form, &controls), DOM_NO_ERR);
    expect_snapshot(&controls, expected, 6, "built-in submittables including external and image inputs");
    dom_html_collection *public;
    ck_assert_int_eq(dom_html_form_element_get_elements((dom_html_form_element *)form, &public), DOM_NO_ERR);
    uint32_t count, before;
    ck_assert_int_eq(dom_html_collection_get_length(public, &before), DOM_NO_ERR);
    for (uint32_t i = 0; i < before; i++) {
        dom_node *item = NULL;
        ck_assert_int_eq(dom_html_collection_item(public, i, &item), DOM_NO_ERR);
        ck_assert_ptr_ne(item, expected[5]);
        dom_node_unref(item);
    }
    set(expected[5], "type", "text");
    ck_assert_int_eq(dom_html_collection_get_length(public, &count), DOM_NO_ERR);
    ck_assert_uint_eq(count, before + 1);
    set(expected[5], "type", "IMAGE");
    ck_assert_int_eq(dom_html_collection_get_length(public, &count), DOM_NO_ERR);
    ck_assert_uint_eq(count, before);
    expect_snapshot(&controls, expected, 6, "type edits do not alter a captured list");
    dom_html_collection_unref(public);
    dom_html_form_control_list_fini(&controls);
    ck_assert_ptr_null(controls.items);
    ck_assert_uint_eq(controls.length, 0);
    dom_html_form_control_list_fini(&controls); /* Reset empty state is reusable. */
}
END_TEST

START_TEST(test_submission_snapshot_mutation)
{
    dom_node *r = root(), *form = create("form"), *other = create("form");
    dom_node *first = create(tags[_i]), *second = create(tags[_i]);
    append(r, form);
    append(r, other);
    append(form, first);
    append(form, second);
    set(form, "id", "form");
    dom_html_form_control_list before, after;
    ck_assert_int_eq(dom_html_form_element_get_submittable_elements((dom_html_form_element *)form, &before), DOM_NO_ERR);
    dom_node *expected[] = {first, second};
    expect_snapshot(&before, expected, 2, "initial candidates");
    append(other, first);
    remove_node(second);
    dom_node *replacement = create(tags[_i]);
    set(replacement, "form", "form");
    append(r, replacement);
    expect_snapshot(&before, expected, 2, "moving/removing controls preserves snapshot identity and order");
    ck_assert_int_eq(dom_html_form_element_get_submittable_elements((dom_html_form_element *)form, &after), DOM_NO_ERR);
    expect_snapshot(&after, &replacement, 1, "next snapshot observes live owners");
    dom_html_form_control_list_fini(&before);
    dom_html_form_control_list_fini(&after);
    remove_node(form);
    append(form, first);
    ck_assert_int_eq(dom_html_form_element_get_submittable_elements((dom_html_form_element *)form, &after), DOM_NO_ERR);
    expect_snapshot(&after, &first, 1, "detached form snapshot stays in its current tree");
    dom_html_form_control_list_fini(&after);
}
END_TEST

START_TEST(test_submission_snapshot_parser_owner)
{
    parse_control(_i);
    dom_node *control = by_id("control"), *form = by_id("parser");
    dom_html_form_control_list controls;
    ck_assert_int_eq(dom_html_form_element_get_submittable_elements((dom_html_form_element *)form, &controls), DOM_NO_ERR);
    expect_snapshot(&controls, &control, 1, "parser's non-ancestor owner participates in submission");
    dom_html_form_control_list_fini(&controls);
}
END_TEST

START_TEST(test_submission_snapshot_lifetime)
{
    dom_node *r = root(), *form = create("form"), *control = create(tags[_i]);
    append(r, form);
    append(form, control);
    set(control, "name", "retained");
    dom_html_form_control_list controls;
    ck_assert_int_eq(dom_html_form_element_get_submittable_elements((dom_html_form_element *)form, &controls), DOM_NO_ERR);
    for (size_t i = 0; i < held_count; i++)
        dom_node_unref(held[i]);
    held_count = 0;
    dom_node_unref(document);
    document = NULL;
    expect_snapshot(&controls, &control, 1, "only snapshot retains control after document teardown");
    dom_string *name = string("name"), *value = NULL;
    ck_assert_int_eq(dom_element_get_attribute(control, name, &value), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(value), "retained");
    dom_string_unref(value);
    dom_string_unref(name);
    dom_html_form_control_list_fini(&controls);
}
END_TEST

static dom_node *large_form(void)
{
    dom_node *r = root(), *form = create("form");
    append(r, form);
    dom_string *input = string("input");
    for (unsigned i = 0; i < 130; i++) {
        dom_node *node;
        ck_assert_int_eq(dom_document_create_element(document, input, &node), DOM_NO_ERR);
        char id[20];
        snprintf(id, sizeof(id), "%u", i);
        set(node, "id", id);
        append(form, node);
        dom_node_unref(node);
    }
    dom_string_unref(input);
    return form;
}

START_TEST(test_submission_snapshot_growth)
{
    dom_node *form = large_form();
    dom_html_form_control_list controls;
    ck_assert_int_eq(dom_html_form_element_get_submittable_elements((dom_html_form_element *)form, &controls), DOM_NO_ERR);
    ck_assert_uint_eq(controls.length, 130);
    for (size_t i = 0; i < controls.length; i++) {
        dom_string *id = NULL;
        ck_assert_int_eq(dom_html_element_get_id(controls.items[i], &id), DOM_NO_ERR);
        char expected[20];
        snprintf(expected, sizeof(expected), "%zu", i);
        ck_assert_str_eq(dom_string_data(id), expected);
        dom_string_unref(id);
    }
    dom_html_form_control_list_fini(&controls);
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_submission_snapshot_allocation_failure)
{
    dom_node *form = large_form();
    bool success = false;
    for (unsigned fail = 0; fail < 8; fail++) {
        dom_html_form_control_list controls;
        realloc_limit(fail);
        dom_exception err = dom_html_form_element_get_submittable_elements((dom_html_form_element *)form, &controls);
        realloc_limit(UINT_MAX);
        if (err == DOM_NO_ERR) {
            ck_assert_uint_eq(controls.length, 130);
            dom_html_form_control_list_fini(&controls);
            ck_assert_uint_ge(fail, 4); /* Failure after retaining earlier blocks is exercised. */
            success = true;
            break;
        }
        ck_assert_int_eq(err, DOM_NO_MEM_ERR);
        ck_assert_ptr_null(controls.items);
        ck_assert_uint_eq(controls.length, 0);
    }
    ck_assert(success);
}
END_TEST
#endif

START_TEST(test_option_effective_disabledness)
{
    static const char *barriers[] = {"div", "select", "hr", "datalist", "option", "optgroup"};
    dom_node *group = create("optgroup"), *barrier = create(barriers[_i]), *option = create("option");
    set(group, "disabled", "");
    append(group, barrier);
    append(barrier, option);
    bool disabled = true;
    ck_assert_int_eq(dom_html_option_element_get_disabled((dom_html_option_element *)option, &disabled), DOM_NO_ERR);
    ck_assert(!disabled); /* IDL reflection must not start reporting inherited state. */
    ck_assert_int_eq(dom_html_option_element_is_disabled((dom_html_option_element *)option, &disabled), DOM_NO_ERR);
    ck_assert_msg(disabled == (_i == 0), "Option boundary %s: disabled=%d", barriers[_i], disabled);
    set(option, "disabled", "");
    ck_assert_int_eq(dom_html_option_element_is_disabled((dom_html_option_element *)option, &disabled), DOM_NO_ERR);
    ck_assert(disabled);
    set(option, "disabled", NULL);
    append(group, option);
    ck_assert_int_eq(dom_html_option_element_is_disabled((dom_html_option_element *)option, &disabled), DOM_NO_ERR);
    ck_assert(disabled);
    set(group, "disabled", NULL);
    ck_assert_int_eq(dom_html_option_element_is_disabled((dom_html_option_element *)option, &disabled), DOM_NO_ERR);
    ck_assert(!disabled);
}
END_TEST

START_TEST(test_datalist_and_foreign_ancestors)
{
    dom_node *form = create("form"), *datalist = create("datalist"), *wrap = create("div"), *input = create("input");
    append(form, datalist);
    append(datalist, wrap);
    append(wrap, input);
    ck_assert(dom_html_form_control_has_datalist_ancestor(input));
    dom_html_form_control_list controls;
    ck_assert_int_eq(dom_html_form_element_get_submittable_elements((dom_html_form_element *)form, &controls), DOM_NO_ERR);
    expect_snapshot(&controls, &input, 1, "datalist exclusion happens after candidate collection");
    dom_html_form_control_list_fini(&controls);
    dom_string *ns = string("urn:foreign"), *name = string("datalist");
    dom_node *foreign;
    ck_assert_int_eq(dom_document_create_element_ns(document, ns, name, &foreign), DOM_NO_ERR);
    hold(foreign);
    append(form, foreign);
    append(foreign, wrap);
    ck_assert(!dom_html_form_control_has_datalist_ancestor(input));
    dom_string_unref(name);
    name = string("optgroup");
    dom_node *group;
    ck_assert_int_eq(dom_document_create_element_ns(document, ns, name, &group), DOM_NO_ERR);
    hold(group);
    set(group, "disabled", "");
    dom_node *option = create("option");
    append(group, option);
    bool disabled = true;
    ck_assert_int_eq(dom_html_option_element_is_disabled((dom_html_option_element *)option, &disabled), DOM_NO_ERR);
    ck_assert(!disabled);
    dom_string_unref(name);
    dom_string_unref(ns);
}
END_TEST

static void release_document_except(dom_node *control)
{
    dom_node_ref(control);
    for (size_t i = 0; i < held_count; i++)
        dom_node_unref(held[i]);
    held_count = 0;
    dom_node_unref(document);
    document = NULL;
}

START_TEST(test_ancestor_teardown)
{
    dom_node *r = root(), *form = create("form"), *control = create(tags[_i]);
    append(r, form);
    append(form, control);
    expect(control, form, "initial owner");
    /* Keep only the control's external reference across document teardown. */
    release_document_except(control);
    expect(control, NULL, "surviving detached control has no freed ancestor owner");
    dom_node_unref(control);
}
END_TEST

START_TEST(test_parser_ancestor_teardown)
{
    char source[1024];
    snprintf(source, sizeof(source), "<!doctype html><form id=parser>%s</form>", control_markup[_i]);
    parse(source);
    dom_node *control = by_id("control"), *form = by_id("parser");
    expect(control, form, "initial parser ancestor owner");
    release_document_except(control);
    expect(control, NULL, "parser ancestor reference cannot outlive destroyed ancestor");
    set(control, "form", "missing");
    expect(control, NULL, "surviving HTML node keeps usable document metadata");
    dom_node_unref(control);
}
END_TEST

int main(void)
{
    Suite *suite = suite_create("HTML form ownership");
    TCase *tc = tcase_create("Live owners and parser reset");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_live_ordinary_ownership, 0, CONTROL_TYPES);
    tcase_add_loop_test(tc, test_first_matching_id, 0, CONTROL_TYPES);
    tcase_add_loop_test(tc, test_parser_association_and_reset, 0, PARSER_TYPES);
    tcase_add_loop_test(tc, test_parser_clone, 0, PARSER_TYPES);
    tcase_add_loop_test(tc, test_parser_owner_becomes_ancestor, 0, PARSER_TYPES);
    tcase_add_loop_test(tc, test_fragment_reparenting, 0, 2 * PARSER_TYPES);
    tcase_add_loop_test(tc, test_form_attribute_discards_parser_identity, 0, 2 * PARSER_TYPES);
    tcase_add_loop_test(tc, test_attribute_value_and_namespace, 0, CONTROL_TYPES);
    tcase_add_loop_test(tc, test_live_form_collection, 0, PARSER_TYPES);
    tcase_add_loop_test(tc, test_surviving_control_and_form_lifetimes, 0, PARSER_TYPES);
    tcase_add_loop_test(tc, test_ancestor_teardown, 0, CONTROL_TYPES);
    tcase_add_loop_test(tc, test_parser_ancestor_teardown, 0, PARSER_TYPES);
    tcase_add_test(tc, test_submission_snapshot_inventory);
    tcase_add_loop_test(tc, test_submission_snapshot_mutation, 0, PARSER_TYPES);
    tcase_add_loop_test(tc, test_submission_snapshot_parser_owner, 0, PARSER_TYPES);
    tcase_add_loop_test(tc, test_submission_snapshot_lifetime, 0, PARSER_TYPES);
    tcase_add_test(tc, test_submission_snapshot_growth);
    tcase_add_loop_test(tc, test_option_effective_disabledness, 0, 6);
    tcase_add_test(tc, test_datalist_and_foreign_ancestors);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_test(tc, test_submission_snapshot_allocation_failure);
#endif
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
