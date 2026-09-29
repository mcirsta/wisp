/* Verify update batches and live form edits before selective CSS is enabled. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <wisp/utils/corestrings.h>
#include <wisp/content/handlers/html/form_internal.h>
#include "content/handlers/html/restyle.h"

static html_content html;

static dom_node *node(const char *id)
{
    dom_string *key;
    dom_node *result = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)id, strlen(id), &key), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_get_element_by_id(html.document, key, &result), DOM_NO_ERR);
    dom_string_unref(key);
    ck_assert_msg(result != NULL, "Missing test node %s", id);
    return result;
}

static void setup(void)
{
    memset(&html, 0, sizeof(html));
    html.had_initial_layout = true;
    const char *source = "<div id=outer><input id=a><div id=inner><input id=b><input id=c></div></div>"
                         "<section><input id=d></section><textarea id=notes>note</textarea>";
    dom_hubbub_parser *parser;
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &html.document), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)source, strlen(source)), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
}

static void teardown(void)
{
    html_restyle_discard_pending(&html);
    dom_node_unref(html.document);
}

static void record(const char *id)
{
    dom_node *changed = node(id);
    html_restyle_record_checked(&html, changed);
    dom_node_unref(changed);
}

START_TEST(test_coalescing_and_batches)
{
    record("b");
    record("c");
    ck_assert_int_eq(html.restyle.pending.count, 1);
    record("a"); /* Replace the nested root with its containing root. */
    record("b"); /* Already covered by the outer root. */
    record("d");
    ck_assert_int_eq(html.restyle.pending.count, 2);
    ck_assert_int_eq(html.restyle.pending.changed_controls, 5);
    dom_node *outer = node("outer");
    ck_assert_ptr_eq(html.restyle.pending.roots[0], outer);
    dom_node_unref(outer);
    html_restyle_begin(&html);
    ck_assert_int_eq(html.restyle.pending.count, 0);
    record("d"); /* A request arriving during conversion belongs to the next batch. */
    html_restyle_capture_inputs(&html);
    html_restyle_end(&html, true);
    ck_assert_int_eq(html.restyle.pending.count, 1);
    html_restyle_record_full(&html, "full_request");
    record("a");
    ck_assert_int_eq(html.restyle.pending.count, 0);
    ck_assert_str_eq(html.restyle.pending.full_reason, "full_request");
}
END_TEST

START_TEST(test_scope_capacity)
{
    for (unsigned i = 0; i <= HTML_RESTYLE_MAX_ROOTS; i++) {
        dom_element *parent, *input;
        dom_node *inserted;
        dom_string *tag;
        ck_assert_int_eq(dom_string_create((const uint8_t *)"div", 3, &tag), DOM_NO_ERR);
        ck_assert_int_eq(dom_document_create_element(html.document, tag, &parent), DOM_NO_ERR);
        dom_string_unref(tag);
        ck_assert_int_eq(dom_string_create((const uint8_t *)"input", 5, &tag), DOM_NO_ERR);
        ck_assert_int_eq(dom_document_create_element(html.document, tag, &input), DOM_NO_ERR);
        dom_string_unref(tag);
        ck_assert_int_eq(dom_node_append_child(parent, input, &inserted), DOM_NO_ERR);
        dom_node_unref(inserted);
        html_restyle_record_checked(&html, (dom_node *)input);
        dom_node_unref(input);
        dom_node_unref(parent);
    }
    ck_assert_int_eq(html.restyle.pending.count, 0);
    ck_assert_str_eq(html.restyle.pending.full_reason, "scope_capacity");
}
END_TEST

START_TEST(test_edits_before_refresh)
{
    dom_node *input = node(_i == 0 ? "a" : "notes");
    struct form_control *control = form_new_control(input, _i == 0 ? GADGET_TEXTBOX : GADGET_TEXTAREA);
    ck_assert_ptr_nonnull(control);
    control->html = &html;
    form_gadget_update_value(control, strdup("edited"));
    ck_assert_msg(html.restyle.dom_generation > html.restyle.styled_generation,
        "A synchronized %s edit must invalidate the previous presentation", _i == 0 ? "input" : "textarea");
    uint64_t generation = html.restyle.dom_generation;
    form_gadget_sync_with_dom(control);
    ck_assert_uint_eq(html.restyle.dom_generation, generation);
    form_gadget_update_value(control, strdup("edited"));
    ck_assert_uint_eq(html.restyle.dom_generation, generation);
    record("b");
    html_restyle_begin(&html);
#ifdef WISP_PROFILE_RESTYLE
    ck_assert_str_eq(html.restyle_profile.fallback_reason, "dom_changed");
#endif
    html_restyle_capture_inputs(&html);
    html_restyle_end(&html, false);
    ck_assert_uint_eq(html.restyle.styled_generation, 0);
    html_restyle_end(&html, true);
    ck_assert_uint_eq(html.restyle.styled_generation, generation);
    record("b");
    html_restyle_begin(&html);
#ifdef WISP_PROFILE_RESTYLE
    ck_assert_str_eq(html.restyle_profile.fallback_reason, "partial_not_implemented");
#endif
    html_restyle_capture_inputs(&html);
    form_gadget_update_value(control, strdup("newer"));
    html_restyle_end(&html, true);
    ck_assert_msg(html.restyle.dom_generation > html.restyle.styled_generation,
        "Completion must not discard an edit arriving during construction");
    html_restyle_discard_pending(&html);
    ck_assert_msg(html.restyle.dom_generation > html.restyle.styled_generation,
        "Cancelling pending work must not make stale styles reusable");
    form_gadget_update_value(control, strdup(""));
    generation = html.restyle.dom_generation;
    form_gadget_update_value(control, strdup(""));
    ck_assert_msg(html.restyle.dom_generation == generation,
        "Synchronizing unchanged empty text must not permanently disable style reuse");
    form_free_control(control);
    dom_node_unref(input);
}
END_TEST

int main(void)
{
    if (corestrings_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("Restyle tracking");
    TCase *tc = tcase_create("Scopes and edit generations");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_coalescing_and_batches);
    tcase_add_test(tc, test_scope_capacity);
    tcase_add_loop_test(tc, test_edits_before_refresh, 0, 2);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    corestrings_fini();
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
