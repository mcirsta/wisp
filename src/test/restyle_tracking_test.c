/* Verify update scopes, input guards and transient style lookup lifetime. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsoption.h>
#include <wisp/utils/nsurl.h>
#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/form_internal.h>
#include "content/urldb.h"
#include "content/handlers/html/restyle.h"
#include "content/handlers/html/font_face.h"
#include "content/handlers/css/internal.h"
#ifdef WISP_TEST_ALLOC_FAILURE
#include "test/malloc_fig.h"
#endif

static html_content html;
static const enum nsoption_e font_options[] = {
    NSOPTION_font_sans, NSOPTION_font_serif, NSOPTION_font_mono, NSOPTION_font_cursive, NSOPTION_font_fantasy};

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
    char *old_family = NULL;
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
    case 10: case 11: case 12: case 13: case 14: {
        enum nsoption_e option = font_options[_i - 10];
        const char *name = nsoptions[option].value.s;
        old_family = name != NULL ? strdup(name) : NULL;
        ck_assert_int_eq(nsoption_set_tbl_charp(nsoptions, option, strdup("changed font family")), NSERROR_OK);
        break;
    }
    case 15:
        html_font_face_notify_changed();
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
    if (_i >= 10 && _i < 15)
        nsoption_set_tbl_charp(nsoptions, font_options[_i - 10], old_family);
    if (_i == 8)
        urldb_destroy();
}
END_TEST

START_TEST(test_oversized_font_names_disable_reuse)
{
    const char *original = nsoption_charp(font_sans);
    char *saved = original != NULL ? strdup(original) : NULL;
    char *long_name = malloc(sizeof(html.restyle.active_inputs.font_families) + 1);
    ck_assert_ptr_nonnull(long_name);
    memset(long_name, 'a', sizeof(html.restyle.active_inputs.font_families));
    long_name[sizeof(html.restyle.active_inputs.font_families)] = '\0';
    nsoption_set_charp(font_sans, long_name);
    ck_assert_int_eq(html_restyle_capture_inputs(&html), NSERROR_OK);
    ck_assert_msg(!html.restyle.active_inputs.valid, "Oversized font names must disable reuse, not truncate");
    nsoption_set_charp(font_sans, saved);
    ck_assert_int_eq(html_restyle_capture_inputs(&html), NSERROR_OK);
    ck_assert_msg(html.restyle.active_inputs.valid, "Normal font names must restore reuse eligibility");
}
END_TEST

#ifdef WISP_TEST_ALLOC_FAILURE
START_TEST(test_allocation_fallbacks)
{
    dom_node *outside = node("outer");
    css_select_results *original = calloc(1, sizeof(*original));
    ck_assert_ptr_nonnull(original);
    struct box old_box = {.styles = original};
    dom_string *tag;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"div", 3, &tag), DOM_NO_ERR);
    /* More than one vector allocation, so failure can also occur after the
     * first allocation succeeded and must be released. */
    for (unsigned i = 0; i < 150; i++) {
        dom_element *child;
        dom_node *inserted;
        void *previous;
        ck_assert_int_eq(dom_document_create_element(html.document, tag, &child), DOM_NO_ERR);
        ck_assert_int_eq(dom_node_append_child(outside, child, &inserted), DOM_NO_ERR);
        dom_node_unref(inserted);
        ck_assert_int_eq(
            dom_node_set_user_data(child, corestring_dom___ns_key_box_node_data, &old_box, NULL, &previous),
            DOM_NO_ERR);
        dom_node_unref(child);
    }
    dom_string_unref(tag);
    ck_assert_int_eq(html_restyle_capture_inputs(&html), NSERROR_OK);
    html_restyle_end(&html, true);
    record("d");
    if (_i < 2)
        realloc_limit(_i);
    html_restyle_begin(&html);
    realloc_limit(UINT_MAX);
    if (_i < 2) {
        ck_assert_msg(!html.restyle.partial, "Table allocation failure %d must choose full selection", _i);
        ck_assert_ptr_null(html.restyle.entries);
#ifdef WISP_PROFILE_RESTYLE
        ck_assert_str_eq(html.restyle_profile.fallback_reason, "reuse_table_failed");
#endif
    } else {
        ck_assert(html.restyle.partial);
        dom_node *child;
        ck_assert_int_eq(dom_node_get_last_child(outside, &child), DOM_NO_ERR);
        css_select_results *clone = original;
        malloc_limit(0);
        css_error error = css_select_results_clone(original, &clone);
        malloc_limit(UINT_MAX);
        ck_assert_int_eq(error, CSS_NOMEM);
        ck_assert_ptr_eq(clone, original); /* Failure must not publish a container. */
        malloc_limit(0);
        nserror result = html_restyle_reuse_style(&html, child, &clone);
        malloc_limit(UINT_MAX);
        ck_assert_int_eq(result, NSERROR_OK);
        ck_assert_ptr_null(clone); /* Construction must select this element afresh. */
        ck_assert(html.restyle.partial);
        ck_assert_int_eq(html_restyle_reuse_style(&html, child, &clone), NSERROR_OK);
        ck_assert_ptr_nonnull(clone);
        css_select_results_destroy(clone);
        dom_node_unref(child);
    }
    html_restyle_end(&html, true);
    ck_assert_ptr_null(html.restyle.entries);
    ck_assert_int_eq(html.restyle.active.count, 0);
    css_select_results_destroy(original);
    dom_node_unref(outside);
}
END_TEST
#endif

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
    tcase_add_loop_test(tc, test_reuse_table_and_input_guards, 0, 16);
    tcase_add_test(tc, test_oversized_font_names_disable_reuse);
#ifdef WISP_TEST_ALLOC_FAILURE
    tcase_add_loop_test(tc, test_allocation_fallbacks, 0, 3);
#endif
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
