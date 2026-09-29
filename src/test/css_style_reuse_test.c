/* Style ownership and selective invalidation through Wisp's real CSS adapter. */
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcss/gradient.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsoption.h>
#include "content/handlers/css/dump.h"
#include "content/handlers/css/hints.h"
#include "content/handlers/css/internal.h"
#include "content/handlers/css/select.h"

enum { ROOT, OUTSIDE, TABS, INPUT, LABEL, PANEL, CHILD, NODE_COUNT };
static const char *ids[] = {"root", "outside", "tabs", "input", "label", "panel", "child"};
static const int parents[] = {-1, ROOT, ROOT, TABS, TABS, TABS, PANEL};
static dom_document *document;
static dom_node *nodes[NODE_COUNT];
static css_stylesheet *sheet;
static nscss_select_ctx context;
static const css_media media = {.type = CSS_MEDIA_SCREEN};
static const css_unit_ctx units = {.font_size_default = 16 << CSS_RADIX_POINT, .device_dpi = 96 << CSS_RADIX_POINT,
    .viewport_width = 800 << CSS_RADIX_POINT, .viewport_height = 600 << CSS_RADIX_POINT};

static void setup(void)
{
    /* Construct directly to keep the intended parent/sibling relationships. */
    const char *tags[] = {"html", "div", "section", "input", "label", "div", "span"};
    memset(&context, 0, sizeof(context));
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document),
        DOM_NO_ERR);
    for (int i = 0; i < NODE_COUNT; i++) {
        dom_string *name, *id, *key;
        dom_node *inserted;
        ck_assert_int_eq(dom_string_create((const uint8_t *)tags[i], strlen(tags[i]), &name), DOM_NO_ERR);
        ck_assert_int_eq(dom_document_create_element(document, name, (dom_element **)&nodes[i]), DOM_NO_ERR);
        dom_string_unref(name);
        ck_assert_int_eq(dom_node_append_child(i == ROOT ? (dom_node *)document : nodes[parents[i]], nodes[i],
                             &inserted), DOM_NO_ERR);
        dom_node_unref(inserted);
        ck_assert_int_eq(dom_string_create((const uint8_t *)ids[i], strlen(ids[i]), &id), DOM_NO_ERR);
        ck_assert_int_eq(dom_string_create((const uint8_t *)"id", 2, &key), DOM_NO_ERR);
        ck_assert_int_eq(dom_element_set_attribute(nodes[i], key, id), DOM_NO_ERR);
        dom_string_unref(id);
        dom_string_unref(key);
    }
    dom_string *type;
    ck_assert_int_eq(dom_string_create((const uint8_t *)"checkbox", 8, &type), DOM_NO_ERR);
    ck_assert_int_eq(dom_element_set_attribute(nodes[INPUT], corestring_dom_type, type), DOM_NO_ERR);
    dom_string_unref(type);

    const char *css = ":root { --shared:#123456; color:var(--shared); font:20px serif }"
        "#outside { width:calc(12.5em + 7px); font-family:'Example',serif;"
        "background-image:linear-gradient(to right,red 1em,blue),url(tile.png),radial-gradient(circle,green,blue);"
        "grid-template-columns:11px 2fr; grid-template-rows:13px 1fr; transform:translate(3px,4px) rotate(15deg) }"
        "#outside::before { content:'prefix' attr(id); background-image:url(pseudo.png) }"
        "#outside::after { content:'tail' } #outside::marker { color:#abcdef }"
        "#tabs { --tone:var(--shared) }"
        "input:checked + label { --label-color:#bc1234 } input:not(:checked) + label { --label-color:#1234bc }"
        "label::before { content:'label'; color:var(--label-color) }"
        "input:checked ~ #panel { --tone:#765432; display:block }"
        "input:not(:checked) ~ #panel { display:none }"
        "#panel span { color:var(--absent,var(--tone)); width:calc(1em + 7px) }"
        "#panel span::before { content:'state'; color:var(--tone) }";
    css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT, .charset = "UTF-8", .url = "file:///reuse.html", .resolve = nscss_resolve_url};
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    css_error error = css_stylesheet_append_data(sheet, (const uint8_t *)css, strlen(css));
    ck_assert_msg(error == CSS_OK || error == CSS_NEEDDATA, "CSS parse error %d", error);
    ck_assert_int_eq(css_stylesheet_data_done(sheet), CSS_OK);
    ck_assert_int_eq(css_select_ctx_create(&context.ctx), CSS_OK);
    ck_assert_int_eq(css_select_ctx_append_sheet(context.ctx, sheet, CSS_ORIGIN_AUTHOR, NULL), CSS_OK);
    ck_assert_int_eq(lwc_intern_string("*", 1, &context.universal), lwc_error_ok);
}

static void teardown(void)
{
    for (int i = 0; i < NODE_COUNT; i++)
        dom_node_unref(nodes[i]);
    dom_node_unref(document);
    css_select_ctx_destroy(context.ctx);
    css_stylesheet_destroy(sheet);
    lwc_string_unref(context.universal);
}

static void release(css_select_results **styles)
{
    for (int i = 0; i < NODE_COUNT; i++) {
        if (styles[i] != NULL)
            css_select_results_destroy(styles[i]);
    }
}

static void select_tree(css_select_results **styles, css_select_results **old)
{
    for (int i = 0; i < NODE_COUNT; i++) {
        styles[i] = NULL;
        /* Match construction: descendants of display:none have no saved style. */
        if (i == CHILD && css_computed_display(styles[PANEL]->styles[0], false) == CSS_DISPLAY_NONE)
            continue;
        if (old != NULL && i < TABS) {
            ck_assert_int_eq(css_select_results_clone(old[i], &styles[i]), CSS_OK);
        } else {
            context.parent_style = i == ROOT ? NULL : styles[parents[i]]->styles[0];
            context.root_style = i == ROOT ? NULL : styles[ROOT]->styles[0];
            styles[i] = nscss_get_style(&context, nodes[i], &media, &units, NULL);
        }
        ck_assert_msg(styles[i] != NULL, "No selected style for %s", ids[i]);
    }
}

static char *dump(const css_computed_style *style)
{
    char *text = NULL;
    size_t size;
    FILE *stream = open_memstream(&text, &size);
    ck_assert_ptr_nonnull(stream);
    nscss_dump_computed_style(stream, style);
    ck_assert_int_eq(fclose(stream), 0);
    return text;
}

static void check_dynamic(const css_select_results *styles)
{
    const css_computed_style *base = styles->styles[0];
    int width;
    ck_assert_int_eq(css_computed_width_px(base, &units, 800, &width), CSS_WIDTH_SET);
    ck_assert_int_eq(width, 257);
    ck_assert_uint_eq(css_computed_background_layer_count(base), 3);
    const css_linear_gradient *linear = css_computed_background_gradient_layer(base, 0);
    const css_radial_gradient *radial = css_computed_background_radial_gradient_layer(base, 2);
    ck_assert_ptr_nonnull(linear);
    ck_assert_ptr_nonnull(radial);
    ck_assert_int_eq(linear->stop_count, 2);
    ck_assert_int_eq(linear->stops[0].offset, INTTOFIX(20));
    ck_assert_int_eq(linear->stops[0].unit, CSS_UNIT_PX);
    ck_assert_uint_eq(radial->stops[1].color, 0xff0000ff);
    lwc_string *url;
    css_computed_background_image_layer(base, 1, &url);
    ck_assert_ptr_nonnull(url);
    ck_assert_str_eq(lwc_string_data(url), "file:///tile.png");
    css_computed_grid_track *tracks;
    int32_t count;
    ck_assert_int_eq(css_computed_grid_template_columns(base, &count, &tracks), CSS_GRID_TEMPLATE_SET);
    ck_assert_int_eq(count, 2);
    ck_assert_int_eq(tracks[0].value, INTTOFIX(11));
    ck_assert_int_eq(tracks[1].unit, CSS_UNIT_FR);
    ck_assert_int_eq(css_computed_grid_template_rows(base, &count, &tracks), CSS_GRID_TEMPLATE_SET);
    ck_assert_int_eq(count, 2);
    ck_assert_int_eq(tracks[0].value, INTTOFIX(13));
    uint32_t functions_count;
    const css_transform_function *functions;
    ck_assert_int_eq(css_computed_transform(base, &functions_count, &functions), CSS_TRANSFORM_FUNCTIONS);
    ck_assert_uint_eq(functions_count, 2);
    ck_assert_int_eq(functions[0].value1, INTTOFIX(3));
    ck_assert_int_eq(functions[1].type, CSS_TRANSFORM_FUNC_ROTATE);
    lwc_string **families;
    css_computed_font_family(base, &families);
    ck_assert_ptr_nonnull(families);
    ck_assert_str_eq(lwc_string_data(families[0]), "Example");
    const css_computed_content_item *items;
    ck_assert_int_eq(css_computed_content(styles->styles[CSS_PSEUDO_ELEMENT_BEFORE], &items), CSS_CONTENT_SET);
    ck_assert_str_eq(lwc_string_data(items[0].data.string), "prefix");
    ck_assert_int_eq(items[1].type, CSS_COMPUTED_CONTENT_ATTR);
    ck_assert_str_eq(lwc_string_data(items[1].data.attr), "id");
    ck_assert_int_eq(items[2].type, CSS_COMPUTED_CONTENT_NONE);
    css_color color;
    ck_assert_ptr_nonnull(styles->styles[CSS_PSEUDO_ELEMENT_MARKER]);
    css_computed_color(styles->styles[CSS_PSEUDO_ELEMENT_MARKER], &color);
    ck_assert_uint_eq(color, 0xffabcdef);
}

START_TEST(test_clone_lifetime)
{
    css_select_results *untouched = NULL;
    ck_assert_int_eq(css_select_results_clone(NULL, &untouched), CSS_BADPARM);
    ck_assert_ptr_null(untouched);
    setup();
    css_select_results *styles[NODE_COUNT], *clone;
    select_tree(styles, NULL);
    check_dynamic(styles[OUTSIDE]);
    untouched = styles[OUTSIDE];
    ck_assert_int_eq(css_select_results_clone(NULL, &untouched), CSS_BADPARM);
    ck_assert_ptr_eq(untouched, styles[OUTSIDE]);
    ck_assert_int_eq(css_select_results_clone(styles[OUTSIDE], NULL), CSS_BADPARM);
    ck_assert_int_eq(css_select_results_clone(styles[OUTSIDE], &clone), CSS_OK);
    ck_assert_ptr_ne(styles[OUTSIDE], clone);
    for (unsigned i = 0; i < CSS_PSEUDO_ELEMENT_COUNT; i++)
        ck_assert_ptr_eq(styles[OUTSIDE]->styles[i], clone->styles[i]);
    if (_i == 0) {
        css_select_results_destroy(clone);
        clone = styles[OUTSIDE];
    } else {
        css_select_results_destroy(styles[OUTSIDE]);
    }
    styles[OUTSIDE] = NULL;
    release(styles);
    teardown(); /* Drop node caches, sheet and selection context too. */
    check_dynamic(clone);
    css_select_results_destroy(clone);
}
END_TEST

/* Interning may give several pseudo slots the same style pointer. Each slot
 * still owns a reference, including slots whose pseudo is not painted yet. */
START_TEST(test_all_slots_and_aliased_styles)
{
    css_select_results *styles[NODE_COUNT], *source = calloc(1, sizeof(*source)), *clone;
    ck_assert_ptr_nonnull(source);
    select_tree(styles, NULL);
    for (unsigned p = 0; p < CSS_PSEUDO_ELEMENT_COUNT; p++) {
        css_select_results *owner;
        ck_assert_int_eq(css_select_results_clone(styles[OUTSIDE], &owner), CSS_OK);
        source->styles[p] = owner->styles[0];
        owner->styles[0] = NULL; /* Transfer this reference into the test result. */
        css_select_results_destroy(owner);
    }
    ck_assert_int_eq(css_select_results_clone(source, &clone), CSS_OK);
    css_select_results_destroy(source);
    release(styles);
    ck_assert_int_eq(nscss_invalidate_document(document), NSERROR_OK);
    for (unsigned p = 0; p < CSS_PSEUDO_ELEMENT_COUNT; p++) {
        ck_assert_ptr_nonnull(clone->styles[p]);
        css_color color;
        css_computed_color(clone->styles[p], &color);
        ck_assert_msg(color == 0xff123456, "Style in slot %u lost its value", p);
    }
    css_select_results_destroy(clone);
}
END_TEST

START_TEST(test_selective_matches_full)
{
    css_select_results *old[NODE_COUNT];
    select_tree(old, NULL);
    ck_assert_ptr_null(old[CHILD]);
    bool safe;
    ck_assert_int_eq(css_select_ctx_checked_parent_scope(context.ctx, &safe, NULL), CSS_OK);
    ck_assert(safe);
    for (unsigned step = 0; step < 8; step++) {
        bool checked = (step % 2 == 0);
        ck_assert_int_eq(dom_html_input_element_set_checked((dom_html_input_element *)nodes[INPUT], checked),
            DOM_NO_ERR);
        void *outside_before, *outside_after;
        dom_node_get_user_data(nodes[OUTSIDE], corestring_dom___ns_key_libcss_node_data, &outside_before);
        ck_assert_ptr_nonnull(outside_before);
        ck_assert_int_eq(nscss_invalidate_subtree(nodes[TABS], NULL), NSERROR_OK);
        dom_node_get_user_data(nodes[OUTSIDE], corestring_dom___ns_key_libcss_node_data, &outside_after);
        ck_assert_ptr_eq(outside_before, outside_after);
        css_select_results *partial[NODE_COUNT], *full[NODE_COUNT];
        select_tree(partial, old);
        release(old);
        ck_assert_int_eq(nscss_invalidate_document(document), NSERROR_OK);
        select_tree(full, NULL);
        for (int i = 0; i < NODE_COUNT; i++) {
            ck_assert_msg((partial[i] == NULL) == (full[i] == NULL), "step %u node %s visibility", step, ids[i]);
            if (partial[i] == NULL)
                continue;
            for (unsigned p = 0; p < CSS_PSEUDO_ELEMENT_COUNT; p++) {
                const css_computed_style *a = partial[i]->styles[p], *b = full[i]->styles[p];
                ck_assert_msg((a == NULL) == (b == NULL), "step %u node %s pseudo %u presence", step, ids[i], p);
                if (a != NULL) {
                    char *actual = dump(a), *expected = dump(b);
                    ck_assert_msg(strcmp(actual, expected) == 0, "step %u node %s pseudo %u differs", step, ids[i], p);
                    free(actual);
                    free(expected);
                }
            }
        }
        if (checked) {
            css_color color;
            css_computed_color(partial[CHILD]->styles[0], &color);
            ck_assert_uint_eq(color, 0xff765432);
            css_computed_color(partial[CHILD]->styles[CSS_PSEUDO_ELEMENT_BEFORE], &color);
            ck_assert_uint_eq(color, 0xff765432);
        }
        /* The label's ordinary computed properties stay equal across toggles,
         * but its inherited variable environment changes the pseudo style. */
        css_color label_color;
        css_computed_color(partial[LABEL]->styles[0], &label_color);
        ck_assert_uint_eq(label_color, 0xff123456);
        ck_assert_ptr_nonnull(partial[LABEL]->styles[CSS_PSEUDO_ELEMENT_BEFORE]);
        css_computed_color(partial[LABEL]->styles[CSS_PSEUDO_ELEMENT_BEFORE], &label_color);
        ck_assert_uint_eq(label_color, checked ? 0xffbc1234 : 0xff1234bc);
        check_dynamic(partial[OUTSIDE]);
        release(full);
        memcpy(old, partial, sizeof(old));
    }
    release(old);
}
END_TEST

int main(void)
{
    if (nsoption_init(NULL, NULL, NULL) != NSERROR_OK || corestrings_init() != NSERROR_OK ||
        css_hint_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("CSS style reuse");
    TCase *lifetime = tcase_create("Result ownership");
    tcase_add_loop_test(lifetime, test_clone_lifetime, 0, 2);
    suite_add_tcase(suite, lifetime);
    TCase *scope = tcase_create("Selective invalidation");
    tcase_add_checked_fixture(scope, setup, teardown);
    tcase_add_test(scope, test_selective_matches_full);
    tcase_add_test(scope, test_all_slots_and_aliased_styles);
    suite_add_tcase(suite, scope);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    css_hint_fini();
    corestrings_fini();
    nsoption_finalise(NULL, NULL);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
