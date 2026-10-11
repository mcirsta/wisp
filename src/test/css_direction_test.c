/* HTML directionality and Selectors 4 :dir(), using the production handler.
 * Expectations come from HTML #the-dir-attribute and Selectors #the-dir-pseudo. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsoption.h>
#include "content/handlers/css/hints.h"
#include "content/handlers/css/select.h"
#include "content/handlers/css/internal.h"
#include "content/handlers/html/direction.h"

static dom_document *document;
static css_stylesheet *sheet;
static nscss_select_ctx context;
static const css_unit_ctx units = {
    .font_size_default = 16 << CSS_RADIX_POINT,
    .device_dpi = 96 << CSS_RADIX_POINT,
    .viewport_width = 800 << CSS_RADIX_POINT,
    .viewport_height = 600 << CSS_RADIX_POINT,
};

static dom_string *string(const char *text)
{
    dom_string *s;
    ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &s), DOM_NO_ERR);
    return s;
}

static void attribute(dom_node *node, const char *name, const char *value)
{
    dom_string *key = string(name), *data = string(value);
    ck_assert_int_eq(dom_element_set_attribute(node, key, data), DOM_NO_ERR);
    dom_string_unref(key);
    dom_string_unref(data);
}

static void text(dom_node *node, const char *value)
{
    dom_string *data = string(value);
    ck_assert_int_eq(dom_node_set_text_content(node, data), DOM_NO_ERR);
    dom_string_unref(data);
}

static dom_node *element(dom_node *parent, const char *tag, const char *dir, const char *ns)
{
    dom_element *node;
    dom_node *inserted;
    dom_string *name = string(tag), *namespace = ns ? string(ns) : NULL;
    /* A null namespace passed to createElementNS is not ordinary HTML. */
    ck_assert_int_eq(ns == NULL ? dom_document_create_element(document, name, &node) :
        dom_document_create_element_ns(document, namespace, name, &node), DOM_NO_ERR);
    dom_string_unref(namespace);
    dom_string_unref(name);
    if (parent != NULL) {
        ck_assert_int_eq(dom_node_append_child(parent, node, &inserted), DOM_NO_ERR);
        dom_node_unref(inserted);
    }
    if (dir != NULL)
        attribute((dom_node *)node, "dir", dir);
    return (dom_node *)node;
}

static void expect(dom_node *node, bool rtl, const char *description)
{
    bool actual = !rtl;
    ck_assert_int_eq(html_element_direction(node, &actual), DOM_NO_ERR);
    ck_assert_msg(actual == rtl, "%s: semantic direction expected rtl=%d, got %d", description, rtl, actual);
    css_media media = {.type = CSS_MEDIA_SCREEN};
    css_select_results *result = nscss_get_style(&context, node, &media, &units, NULL);
    ck_assert_ptr_nonnull(result);
    css_computed_style *style = result->styles[CSS_PSEUDO_ELEMENT_NONE];
    css_color color;
    ck_assert_int_eq(css_computed_color(style, &color), CSS_COLOR_COLOR);
    ck_assert_msg(color == (rtl ? 0xff556677 : 0xff223344), "%s: :dir() selected color %#x", description, color);
    ck_assert_int_eq(css_computed_direction(style), CSS_DIRECTION_RTL);
    ck_assert_int_eq(css_computed_background_color(style, &color), CSS_BACKGROUND_COLOR_COLOR);
    ck_assert_msg(color == 0xff123456, "%s: unknown identifier is valid but never matches", description);
    css_select_results_destroy(result);
}

static void setup(void)
{
    const char *css = "* { direction:rtl; color:#abcdef }"
                      ":dir( LtR ) { --dir-color:#223344; color:var(--dir-color) }"
                      ":dir(RtL) { --dir-color:#556677; color:var(--dir-color) }"
                      ":not(:dir(sideways)) { background-color:#123456 }"
                      ":dir(auto) { color:red }"
                      /* Each invalid selector must discard its whole rule. */
                      ":dir, * { color:red } :dir(), * { color:red }"
                      ":dir('rtl'), * { color:red } :dir(ltr rtl), * { color:red }"
                      ":dir(ltr, rtl), * { color:red } ::dir(rtl), * { color:red }";
    css_stylesheet_params params = {
        .params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT,
        .charset = "UTF-8",
        .url = "file:///direction.html",
        .resolve = nscss_resolve_url,
    };
    memset(&context, 0, sizeof(context));
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document),
        DOM_NO_ERR);
    ck_assert_int_eq(css_stylesheet_create(&params, &sheet), CSS_OK);
    css_error err = css_stylesheet_append_data(sheet, (const uint8_t *)css, strlen(css));
    ck_assert_msg(err == CSS_OK || err == CSS_NEEDDATA, "CSS parse error %d", err);
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
    const char *tag, *dir, *text;
    bool rtl;
} text_cases[] = {
    {"div", NULL, "א", false},
    {"div", "RtL", "Latin", true},
    {"div", "LtR", "א", false},
    {"div", " rtl ", "א", false},
    {"div", "AuTo", "123 - א Latin", true},
    {"div", "auto", "123 - ب Latin", true},
    {"div", "auto", "Latin א", false},
    {"div", "auto", "123 - +", false},
    {"div", "auto", "", false},
    {"div", "auto", "😀 𐤀", true}, /* Phoenician: non-BMP R. */
    {"div", "auto", "𐐀 א", false}, /* Deseret: non-BMP L. */
    {"div", "auto", "\u0301\u202e\u2067א", true}, /* Marks/controls are not strong. */
    {"bdi", NULL, "א", true},
    {"bdi", "invalid", "ב", true},
    {"bdi", "", "ب", true},
    {"bdi", "ltr", "א", false},
    {"bdi", "rtl", "Latin", true},
    {"x-custom", "auto", "א", true},
};

START_TEST(test_text_direction)
{
    dom_node *node = element((dom_node *)document, text_cases[_i].tag, text_cases[_i].dir, NULL);
    text(node, text_cases[_i].text);
    expect(node, text_cases[_i].rtl, text_cases[_i].text);
    dom_node_unref(node);
}
END_TEST

START_TEST(test_inheritance_and_foreign_nodes)
{
    dom_node *root = element((dom_node *)document, "div", "rtl", NULL);
    dom_node *foreign = element(root, "svg", "ltr", "http://www.w3.org/2000/svg");
    dom_node *child = element(foreign, "span", "invalid", "http://www.w3.org/1999/xhtml");
    expect(foreign, true, "foreign dir attribute is ignored");
    expect(child, true, "inherit through foreign ancestor");
    dom_node *no_namespace = element(root, "div", "ltr", "");
    expect(no_namespace, true, "null-namespace dir attribute is ignored");
    dom_node *neutral = element(child, "bdi", NULL, NULL);
    text(neutral, "123");
    expect(neutral, false, "neutral bdi does not inherit RTL");
    dom_node *automatic = element(child, "div", "auto", NULL);
    expect(automatic, false, "empty auto does not inherit RTL");
    dom_node *detached = element(NULL, "div", "invalid", NULL);
    expect(detached, false, "detached root defaults to LTR");
    dom_node_unref(detached);
    dom_node_unref(automatic);
    dom_node_unref(neutral);
    dom_node_unref(child);
    dom_node_unref(no_namespace);
    dom_node_unref(foreign);
    dom_node_unref(root);
}
END_TEST

static const struct {
    const char *tag, *dir, *ns;
    bool excluded;
} islands[] = {
    {"bdi", NULL, NULL, true},
    {"script", NULL, NULL, true},
    {"style", NULL, NULL, true},
    {"textarea", NULL, NULL, true},
    {"span", "ltr", NULL, true},
    {"span", "RTL", NULL, true},
    {"span", "auto", NULL, true},
    {"span", "invalid", NULL, false},
    {"span", "", NULL, false},
    {"span", NULL, NULL, false},
    {"g", "rtl", "http://www.w3.org/2000/svg", false},
    {"bdi", "rtl", "", false}, /* Null-namespace names do not gain HTML semantics. */
};

START_TEST(test_contained_text_exclusions)
{
    dom_node *root = element((dom_node *)document, "div", "auto", NULL);
    dom_node *island = element(root, islands[_i].tag, islands[_i].dir, islands[_i].ns);
    attribute(island, "style", "display:none"); /* Semantics do not depend on CSS. */
    dom_node *nested = element(island, "span", NULL, NULL);
    text(nested, "א");
    dom_node *later = element(root, "span", NULL, NULL);
    text(later, "Latin");
    expect(root, !islands[_i].excluded, islands[_i].tag);
    dom_node_unref(later);
    dom_node_unref(nested);
    dom_node_unref(island);
    dom_node_unref(root);
}
END_TEST

static const char *const input_types[] = {NULL, "invalid", "hidden", "text", "search", "TeL", "url", "email",
    "password", "submit", "reset", "button", "date", "month", "week", "time", "datetime-local", "number", "range",
    "color", "checkbox", "radio", "file", "image"};

START_TEST(test_input_values)
{
    dom_node *root = element((dom_node *)document, "div", "rtl", NULL);
    dom_node *input = element(root, "input", "auto", NULL);
    if (input_types[_i] != NULL)
        attribute(input, "type", input_types[_i]);
    attribute(input, "value", "123 א");
    expect(input, _i < 12, input_types[_i] ? input_types[_i] : "missing type defaults to text");
    /* Explicit direction always wins, including on telephone inputs. */
    attribute(input, "dir", "rtl");
    ck_assert_int_eq(nscss_invalidate_document(document), NSERROR_OK);
    expect(input, true, "explicit RTL control");
    dom_node_unref(input);
    dom_node_unref(root);
}
END_TEST

START_TEST(test_value_and_text_mutation)
{
    dom_node *root = element((dom_node *)document, "div", "auto", NULL);
    dom_node *child = element(root, "span", NULL, NULL);
    text(child, "א");
    expect(root, true, "initial descendant text");
    text(child, "Latin");
    ck_assert_int_eq(nscss_invalidate_document(document), NSERROR_OK);
    expect(root, false, "changed descendant text");
    dom_node *textarea = element(root, "textarea", "auto", NULL);
    text(textarea, "א");
    for (int i = 0; i < 4; i++) {
        /* Repeated getter use must not cache/leak the original text. */
        expect(textarea, true, "textarea child text before live edits");
        ck_assert_int_eq(nscss_invalidate_document(document), NSERROR_OK);
    }
    text(textarea, "Latin");
    expect(textarea, false, "textarea child mutation before live value");
    dom_string *default_value = NULL;
    ck_assert_int_eq(
        dom_html_text_area_element_get_default_value((dom_html_text_area_element *)textarea, &default_value),
        DOM_NO_ERR);
    ck_assert_ptr_nonnull(default_value);
    ck_assert_str_eq(dom_string_data(default_value), "Latin");
    dom_string_unref(default_value);
    dom_string *value = string("ב");
    ck_assert_int_eq(dom_html_text_area_element_set_value((dom_html_text_area_element *)textarea, value), DOM_NO_ERR);
    dom_string_unref(value);
    ck_assert_int_eq(nscss_invalidate_document(document), NSERROR_OK);
    expect(textarea, true, "textarea current value");
    text(textarea, "Different default text");
    ck_assert_int_eq(nscss_invalidate_document(document), NSERROR_OK);
    expect(textarea, true, "dirty textarea uses its live value, not new child text");
    dom_node *input = element(root, "input", "auto", NULL);
    attribute(input, "value", "Latin");
    expect(input, false, "input initial value");
    value = string("א");
    ck_assert_int_eq(dom_html_input_element_set_value((dom_html_input_element *)input, value), DOM_NO_ERR);
    dom_string_unref(value);
    ck_assert_int_eq(nscss_invalidate_document(document), NSERROR_OK);
    expect(input, true, "input updated current value");
    attribute(input, "type", "TeL");
    attribute(input, "dir", "invalid");
    attribute(root, "dir", "rtl");
    ck_assert_int_eq(nscss_invalidate_document(document), NSERROR_OK);
    expect(input, false, "telephone undefined direction resets to LTR");
    expect(child, true, "ancestor dir mutation affects inherited direction");
    dom_node_unref(input);
    dom_node_unref(textarea);
    dom_node_unref(child);
    dom_node_unref(root);
}
END_TEST

START_TEST(test_sibling_style_sharing)
{
    dom_node *root = element((dom_node *)document, "div", NULL, NULL);
    dom_node *first = element(root, "bdi", NULL, NULL);
    dom_node *second = element(root, "bdi", NULL, NULL);
    text(first, "Latin");
    text(second, "א");
    expect(first, false, "first bdi sibling");
    expect(second, true, "identical sibling attributes cannot share direction-dependent style");
    dom_node_unref(second);
    dom_node_unref(first);
    dom_node_unref(root);
}
END_TEST

START_TEST(test_checked_scope)
{
    bool safe = false;
    ck_assert_int_eq(css_select_ctx_checked_parent_scope(context.ctx, &safe, NULL), CSS_OK);
    ck_assert_msg(safe, ":dir() is unaffected by checkedness-only changes");
}
END_TEST

int main(void)
{
    if (nsoption_init(NULL, NULL, NULL) != NSERROR_OK || corestrings_init() != NSERROR_OK ||
        css_hint_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("HTML semantic direction and CSS dir");
    TCase *tc = tcase_create("DOM selection");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_text_direction, 0, sizeof(text_cases) / sizeof(text_cases[0]));
    tcase_add_test(tc, test_inheritance_and_foreign_nodes);
    tcase_add_loop_test(tc, test_contained_text_exclusions, 0, sizeof(islands) / sizeof(islands[0]));
    tcase_add_loop_test(tc, test_input_values, 0, sizeof(input_types) / sizeof(input_types[0]));
    tcase_add_test(tc, test_value_and_text_mutation);
    tcase_add_test(tc, test_sibling_style_sharing);
    tcase_add_test(tc, test_checked_scope);
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
