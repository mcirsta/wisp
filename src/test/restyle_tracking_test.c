/* Verify update scopes, input guards and transient style lookup lifetime. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsoption.h>
#include <wisp/utils/nsurl.h>
#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/form_internal.h>
#include "content/urldb.h"
#include "content/handlers/html/restyle.h"
#include "content/handlers/css/internal.h"

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
    ck_assert_int_eq(css_select_ctx_create(&html.select_ctx), CSS_OK);
    html.had_initial_layout = true;
    html.base.status = CONTENT_STATUS_DONE;
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
    html_restyle_reset(&html);
    css_select_ctx_destroy(html.select_ctx);
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
    ck_assert_str_eq(html.restyle_profile.fallback_reason, "no_reusable_styles");
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

START_TEST(test_selector_scope_diagnostic)
{
    css_stylesheet *sheet;
    css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT, .charset = "UTF-8", .url = "file:///scope.css", .resolve = nscss_resolve_url};
    const char *css = ":host { color:red }";
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    css_error error = css_stylesheet_append_data(sheet, (const uint8_t *)css, strlen(css));
    ck_assert_msg(error == CSS_OK || error == CSS_NEEDDATA, "CSS parse error %d", error);
    ck_assert_int_eq(css_stylesheet_data_done(sheet), CSS_OK);
    ck_assert_int_eq(css_select_ctx_append_sheet(html.select_ctx, sheet, CSS_ORIGIN_AUTHOR, NULL), CSS_OK);
    record("b");
    html_restyle_begin(&html);
#ifdef WISP_PROFILE_RESTYLE
    ck_assert_str_eq(html.restyle_profile.fallback_reason, "selector_scope");
#endif
    ck_assert_int_eq(css_select_ctx_remove_sheet(html.select_ctx, sheet), CSS_OK);
    css_stylesheet_destroy(sheet);
}
END_TEST

START_TEST(test_reuse_table_and_input_guards)
{
    /* The style payload ownership is covered by css_style_reuse_test. Here an
     * empty result is enough to test the real DOM lookup and batch lifetime. */
    dom_node *outside = node("outer");
    struct box old_box = {.styles = calloc(1, sizeof(css_select_results))};
    ck_assert_ptr_nonnull(old_box.styles);
    void *previous;
    ck_assert_int_eq(dom_node_set_user_data(outside, corestring_dom___ns_key_box_node_data, &old_box, NULL, &previous),
        DOM_NO_ERR);
    ck_assert_int_eq(html_restyle_capture_inputs(&html), NSERROR_OK);
    html_restyle_end(&html, true);
    record("d"); /* Separate parent, so the outer subtree remains reusable. */
    html_restyle_begin(&html);
    ck_assert(html.restyle.partial);
    ck_assert_uint_eq(html.restyle.count, 1);
    ck_assert_int_eq(html_restyle_invalidate(&html), NSERROR_OK);
    ck_assert_int_eq(html_restyle_capture_inputs(&html), NSERROR_OK);
    ck_assert(!html_restyle_needs_restart(&html));
    css_select_results *clone;
    ck_assert_int_eq(html_restyle_reuse_style(&html, outside, &clone), NSERROR_OK);
    ck_assert_ptr_nonnull(clone);
    ck_assert_ptr_ne(clone, old_box.styles);

    int font_default = nsoption_int(font_default);
    bool author_css = nsoption_bool(author_level_css);
    switch (_i) {
    case 0: html_restyle_note_dom_change(&html); break;
    case 1: html.base.available_width++; break;
    case 2: html.unit_len_ctx.viewport_width++; break;
    case 3: html.media.width++; break;
    case 4: nsoption_set_int(font_default, font_default + 1); break;
    case 5: nsoption_set_bool(author_level_css, !author_css); break;
    case 6: record("b"); break; /* New checked state belongs to the pending batch. */
    case 7: {
        css_select_ctx_destroy(html.select_ctx);
        ck_assert_int_eq(css_select_ctx_create(&html.select_ctx), CSS_OK);
        break;
    }
    case 8: {
        struct nsurl *url;
        ck_assert_int_eq(nsurl_create("https://example.test/visited", &url), NSERROR_OK);
        ck_assert(urldb_add_url(url));
        ck_assert_int_eq(urldb_update_url_visit_data(url), NSERROR_OK);
        nsurl_unref(url);
        break;
    }
    case 9:
        html.restyling = true;
        record("b");
        html_restyle_close(&html);
        ck_assert_uint_eq(html.restyle.pending.count, 0);
        ck_assert_ptr_nonnull(html.restyle.entries);
        ck_assert(html.restyle.partial);
        break;
    }
    ck_assert_msg(html_restyle_needs_restart(&html), "Changed input %d did not invalidate the active reuse", _i);
    html_restyle_use_full(&html, "test_changed_inputs");
    ck_assert_ptr_null(html.restyle.entries);
    ck_assert_uint_eq(html.restyle.active.count, 0);
    css_select_results_destroy(clone);
    css_select_results_destroy(old_box.styles);
    dom_node_set_user_data(outside, corestring_dom___ns_key_box_node_data, NULL, NULL, &previous);
    dom_node_unref(outside);
    nsoption_set_int(font_default, font_default);
    nsoption_set_bool(author_level_css, author_css);
    if (_i == 8)
        urldb_destroy();
}
END_TEST

int main(void)
{
    if (nsoption_init(NULL, NULL, NULL) != NSERROR_OK || corestrings_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("Restyle tracking");
    TCase *tc = tcase_create("Scopes and edit generations");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_coalescing_and_batches);
    tcase_add_test(tc, test_scope_capacity);
    tcase_add_test(tc, test_selector_scope_diagnostic);
    tcase_add_loop_test(tc, test_reuse_table_and_input_guards, 0, 10);
    tcase_add_loop_test(tc, test_edits_before_refresh, 0, 2);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    corestrings_fini();
    nsoption_finalise(NULL, NULL);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
