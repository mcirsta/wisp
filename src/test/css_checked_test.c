/* Exercise Wisp's real DOM-to-libcss :checked callback, including DOM state. */
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsoption.h>
#include "content/handlers/css/hints.h"
#include "content/handlers/css/select.h"
#include "content/handlers/css/internal.h"

static dom_document *document;
static css_stylesheet *sheet;
static nscss_select_ctx context;
static const css_unit_ctx units = {
    .font_size_default = 16 << CSS_RADIX_POINT,
    .device_dpi = 96 << CSS_RADIX_POINT,
    .viewport_width = 800 << CSS_RADIX_POINT,
    .viewport_height = 600 << CSS_RADIX_POINT,
};

static dom_string *string(const char *value)
{
    dom_string *s;
    ck_assert_int_eq(dom_string_create((const uint8_t *)value, strlen(value), &s), DOM_NO_ERR);
    return s;
}

static void attribute(dom_node *node, const char *name, const char *value)
{
    dom_string *key = string(name), *data = string(value);
    ck_assert_int_eq(dom_element_set_attribute(node, key, data), DOM_NO_ERR);
    dom_string_unref(key);
    dom_string_unref(data);
}

static dom_node *element(const char *tag, const char *type, const char *ns)
{
    dom_element *node;
    dom_node *inserted;
    dom_string *name = string(tag), *namespace = ns ? string(ns) : NULL;
    ck_assert_int_eq(dom_document_create_element_ns(document, namespace, name, &node), DOM_NO_ERR);
    dom_string_unref(namespace);
    dom_string_unref(name);
    ck_assert_int_eq(dom_node_append_child(document, node, &inserted), DOM_NO_ERR);
    dom_node_unref(inserted);
    if (type != NULL)
        attribute((dom_node *)node, "type", type);
    return (dom_node *)node;
}

static void expect_checked(dom_node *node, bool checked, const char *description)
{
    css_media media = {.type = CSS_MEDIA_SCREEN};
    css_select_results *results = nscss_get_style(&context, node, &media, &units, NULL);
    ck_assert_ptr_nonnull(results);
    css_color color;
    ck_assert_int_eq(css_computed_color(results->styles[CSS_PSEUDO_ELEMENT_NONE], &color), CSS_COLOR_COLOR);
    ck_assert_msg(color == (checked ? 0xff123456 : 0xffabcdef), "%s: expected :checked=%d, got color %#x", description,
        checked, color);
    css_select_results_destroy(results);
}

static void setup(void)
{
    const char *css = ":checked { color: #123456 } :not(:checked) { color: #abcdef }";
    css_stylesheet_params params = {
        .params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT,
        .charset = "UTF-8",
        .url = "file:///checked.html",
        .resolve = nscss_resolve_url,
    };
    memset(&context, 0, sizeof(context));
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document),
        DOM_NO_ERR);
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
    dom_node_unref(document);
    css_select_ctx_destroy(context.ctx);
    css_stylesheet_destroy(sheet);
    lwc_string_unref(context.universal);
}

static const struct {
    const char *tag, *type, *state;
    bool checked;
} cases[] = {
    {"input", "checkbox", "checked", true},
    {"input", "RaDiO", "checked", true},
    {"input", "checkbox", NULL, false},
    {"input", "radio", NULL, false},
    {"input", "text", "checked", false},
    {"input", NULL, "checked", false},
    {"input", "invalid", "checked", false},
    {"input", "button", "checked", false},
    {"option", NULL, "selected", true},
    {"option", NULL, NULL, false},
    {"option", NULL, "checked", false},
    {"div", NULL, "checked", false},
};

START_TEST(test_checked_controls)
{
    dom_node *node = element(cases[_i].tag, cases[_i].type, NULL);
    if (cases[_i].state != NULL)
        attribute(node, cases[_i].state, "false"); /* Boolean attribute values do not matter. */
    expect_checked(node, cases[_i].checked, cases[_i].type ? cases[_i].type : cases[_i].tag);
    dom_node_unref(node);
}
END_TEST

START_TEST(test_current_dom_state)
{
    dom_node *node = element("input", "checkbox", "http://www.w3.org/1999/xhtml");
    /* libdom's click API sets current checkedness without adding an attribute. */
    ck_assert_int_eq(dom_html_input_element_click((dom_html_input_element *)node), DOM_NO_ERR);
    bool has_attribute;
    dom_string *checked = string("checked");
    ck_assert_int_eq(dom_element_has_attribute(node, checked, &has_attribute), DOM_NO_ERR);
    dom_string_unref(checked);
    ck_assert(!has_attribute);
    expect_checked(node, true, "current checkedness without checked attribute");
    dom_node_unref(node);
}
END_TEST

START_TEST(test_selected_option_state)
{
    dom_node *node = element("option", NULL, NULL);
    attribute(node, "selected", "");
    ck_assert_int_eq(dom_html_option_element_set_selected((dom_html_option_element *)node, _i != 0), DOM_NO_ERR);
    expect_checked(node, _i != 0, "option state set through DOM before style selection");
    dom_node_unref(node);
}
END_TEST

START_TEST(test_foreign_element)
{
    dom_node *node = element("input", "checkbox", "http://www.w3.org/2000/svg");
    attribute(node, "checked", "");
    expect_checked(node, false, "foreign element is not an HTML control");
    dom_node_unref(node);
}
END_TEST

int main(void)
{
    /* The production selector reads UA font defaults and HTML hints. */
    if (nsoption_init(NULL, NULL, NULL) != NSERROR_OK || corestrings_init() != NSERROR_OK ||
        css_hint_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("CSS checked state");
    TCase *tc = tcase_create("DOM selection");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_checked_controls, 0, sizeof(cases) / sizeof(cases[0]));
    tcase_add_test(tc, test_current_dom_state);
    tcase_add_loop_test(tc, test_selected_option_state, 0, 2);
    tcase_add_test(tc, test_foreign_element);
    suite_add_tcase(suite, tc);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    css_hint_fini();
    corestrings_fini();
    nsoption_finalise(NULL, NULL);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
