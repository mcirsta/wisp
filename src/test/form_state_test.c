/* Native checkable-control activation must not depend on layout boxes. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <libdom/bindings/hubbub/parser.h>
#include <wisp/utils/corestrings.h>
#include "content/handlers/html/form_state.h"

static dom_document *document;

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

static bool activate(const char *id)
{
    dom_node *clicked = node(id), *target = html_checkable_target(clicked);
    bool changed = target != NULL && html_checkable_activate(target);
    dom_node_unref(target);
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
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    corestrings_fini();
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
