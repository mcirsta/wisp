/*
 * Tests for calc() length handling in layout helpers.
 */

#include <check.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libcss/libcss.h>
#include <libcss/select.h>
#include <libwapcaplet/libwapcaplet.h>

#include "content/handlers/html/layout_internal.h"

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

typedef struct test_node {
    lwc_string *name;
    void *libcss_node_data;
} test_node;

typedef struct select_ctx {
    css_stylesheet *sheet;
    css_select_ctx *select_ctx;
    css_select_results *results;
    lwc_string *node_name;
} select_ctx;

static css_error resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    UNUSED(pw);
    UNUSED(base);
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}

static css_error resolve_font(void *pw, lwc_string *name, css_system_font *system_font)
{
    UNUSED(pw);
    UNUSED(name);
    UNUSED(system_font);
    return CSS_OK;
}

static css_error node_name(void *pw, void *node, css_qname *qname)
{
    test_node *n = node;
    UNUSED(pw);
    qname->ns = NULL;
    qname->name = lwc_string_ref(n->name);
    return CSS_OK;
}

static css_error node_classes(void *pw, void *node, lwc_string ***classes, uint32_t *n_classes)
{
    UNUSED(pw);
    UNUSED(node);
    *classes = NULL;
    *n_classes = 0;
    return CSS_OK;
}

static css_error node_id(void *pw, void *node, lwc_string **id)
{
    UNUSED(pw);
    UNUSED(node);
    *id = NULL;
    return CSS_OK;
}

static css_error named_ancestor_node(void *pw, void *node, const css_qname *qname, void **ancestor)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    *ancestor = NULL;
    return CSS_OK;
}

static css_error named_parent_node(void *pw, void *node, const css_qname *qname, void **parent)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    *parent = NULL;
    return CSS_OK;
}

static css_error named_sibling_node(void *pw, void *node, const css_qname *qname, void **sibling)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    *sibling = NULL;
    return CSS_OK;
}

static css_error named_generic_sibling_node(void *pw, void *node, const css_qname *qname, void **sibling)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    *sibling = NULL;
    return CSS_OK;
}

static css_error parent_node(void *pw, void *node, void **parent)
{
    UNUSED(pw);
    UNUSED(node);
    *parent = NULL;
    return CSS_OK;
}

static css_error sibling_node(void *pw, void *node, void **sibling)
{
    UNUSED(pw);
    UNUSED(node);
    *sibling = NULL;
    return CSS_OK;
}

static css_error node_has_name(void *pw, void *node, const css_qname *qname, bool *match)
{
    test_node *n = node;
    UNUSED(pw);
    bool eq = false;
    (void)lwc_string_caseless_isequal(qname->name, n->name, &eq);
    *match = eq;
    return CSS_OK;
}

static css_error node_has_class(void *pw, void *node, lwc_string *name, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(name);
    *match = false;
    return CSS_OK;
}

static css_error node_has_id(void *pw, void *node, lwc_string *name, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(name);
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute(void *pw, void *node, const css_qname *qname, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_equal(void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_dashmatch(void *pw, void *node, const css_qname *qname, lwc_string *value,
    bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_includes(void *pw, void *node, const css_qname *qname, lwc_string *value,
    bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_prefix(void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_suffix(void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_substring(void *pw, void *node, const css_qname *qname, lwc_string *value,
    bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);
    *match = false;
    return CSS_OK;
}

static css_error node_is_root(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = true;
    return CSS_OK;
}

static css_error node_count_siblings(void *pw, void *node, bool same_name, bool after, int32_t *count)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(same_name);
    UNUSED(after);
    *count = 0;
    return CSS_OK;
}

static css_error node_is_empty(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = true;
    return CSS_OK;
}

static css_error node_is_link(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = false;
    return CSS_OK;
}

static css_error node_is_visited(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = false;
    return CSS_OK;
}

static css_error node_is_hover(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = false;
    return CSS_OK;
}

static css_error node_is_active(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = false;
    return CSS_OK;
}

static css_error node_is_focus(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = false;
    return CSS_OK;
}

static css_error node_is_enabled(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = false;
    return CSS_OK;
}

static css_error node_is_disabled(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = false;
    return CSS_OK;
}

static css_error node_is_checked(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = false;
    return CSS_OK;
}

static css_error node_is_target(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    *match = false;
    return CSS_OK;
}

static css_error node_is_lang(void *pw, void *node, lwc_string *lang, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(lang);
    *match = false;
    return CSS_OK;
}

static css_error node_presentational_hint(void *pw, void *node, uint32_t *nhints, css_hint **hints)
{
    UNUSED(pw);
    UNUSED(node);
    *nhints = 0;
    *hints = NULL;
    return CSS_OK;
}

static css_error ua_default_for_property(void *pw, uint32_t property, css_hint *hint)
{
    UNUSED(pw);

    if (property == CSS_PROP_COLOR) {
        hint->data.color = 0xff000000;
        hint->status = CSS_COLOR_COLOR;
    } else if (property == CSS_PROP_FONT_FAMILY) {
        hint->data.strings = NULL;
        hint->status = CSS_FONT_FAMILY_SANS_SERIF;
    } else if (property == CSS_PROP_QUOTES) {
        hint->data.strings = NULL;
        hint->status = CSS_QUOTES_NONE;
    } else if (property == CSS_PROP_VOICE_FAMILY) {
        hint->data.strings = NULL;
        hint->status = 0;
    } else {
        return CSS_INVALID;
    }

    return CSS_OK;
}

static css_error set_libcss_node_data(void *pw, void *node, void *libcss_node_data)
{
    test_node *n = node;
    UNUSED(pw);
    n->libcss_node_data = libcss_node_data;
    return CSS_OK;
}

static css_error get_libcss_node_data(void *pw, void *node, void **libcss_node_data)
{
    test_node *n = node;
    UNUSED(pw);
    *libcss_node_data = n->libcss_node_data;
    return CSS_OK;
}

static css_select_handler select_handler = {
    CSS_SELECT_HANDLER_VERSION_1,
    node_name,
    node_classes,
    node_id,
    named_ancestor_node,
    named_parent_node,
    named_sibling_node,
    named_generic_sibling_node,
    parent_node,
    sibling_node,
    node_has_name,
    node_has_class,
    node_has_id,
    node_has_attribute,
    node_has_attribute_equal,
    node_has_attribute_dashmatch,
    node_has_attribute_includes,
    node_has_attribute_prefix,
    node_has_attribute_suffix,
    node_has_attribute_substring,
    node_is_root,
    node_count_siblings,
    node_is_empty,
    node_is_link,
    node_is_visited,
    node_is_hover,
    node_is_active,
    node_is_focus,
    node_is_enabled,
    node_is_disabled,
    node_is_checked,
    node_is_target,
    node_is_lang,
    node_presentational_hint,
    ua_default_for_property,
    set_libcss_node_data,
    get_libcss_node_data,
};

static css_unit_ctx unit_ctx = {
    .viewport_width = 200 * (1 << CSS_RADIX_POINT),
    .viewport_height = 100 * (1 << CSS_RADIX_POINT),
    .font_size_default = 16 * (1 << CSS_RADIX_POINT),
    .font_size_minimum = 0,
    .device_dpi = 96 * (1 << CSS_RADIX_POINT),
    .root_style = NULL,
    .pw = NULL,
    .measure = NULL,
};

const css_len_func margin_funcs[4] = {
    [TOP] = css_computed_margin_top,
    [RIGHT] = css_computed_margin_right,
    [BOTTOM] = css_computed_margin_bottom,
    [LEFT] = css_computed_margin_left,
};

const css_len_func padding_funcs[4] = {
    [TOP] = css_computed_padding_top,
    [RIGHT] = css_computed_padding_right,
    [BOTTOM] = css_computed_padding_bottom,
    [LEFT] = css_computed_padding_left,
};

const css_len_func border_width_funcs[4] = {
    [TOP] = css_computed_border_top_width,
    [RIGHT] = css_computed_border_right_width,
    [BOTTOM] = css_computed_border_bottom_width,
    [LEFT] = css_computed_border_left_width,
};

const css_border_style_func border_style_funcs[4] = {
    [TOP] = css_computed_border_top_style,
    [RIGHT] = css_computed_border_right_style,
    [BOTTOM] = css_computed_border_bottom_style,
    [LEFT] = css_computed_border_left_style,
};

const css_border_color_func border_color_funcs[4] = {
    [TOP] = css_computed_border_top_color,
    [RIGHT] = css_computed_border_right_color,
    [BOTTOM] = css_computed_border_bottom_color,
    [LEFT] = css_computed_border_left_color,
};

static css_computed_style *select_style_from_css(const char *css, select_ctx *ctx)
{
    css_stylesheet_params params = {
        .params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_3,
        .charset = "UTF-8",
        .url = "layout_calc_test",
        .title = "layout_calc_test",
        .allow_quirks = false,
        .inline_style = false,
        .resolve = resolve_url,
        .resolve_pw = NULL,
        .import = NULL,
        .import_pw = NULL,
        .color = NULL,
        .color_pw = NULL,
        .font = NULL,
        .font_pw = NULL,
    };
    css_media media = {.type = CSS_MEDIA_ALL};

    memset(ctx, 0, sizeof(*ctx));
    ck_assert_int_eq(css_stylesheet_create(&params, &ctx->sheet), CSS_OK);
    {
        css_error err = css_stylesheet_append_data(ctx->sheet, (const uint8_t *)css, strlen(css));
        ck_assert_msg(err == CSS_OK || err == CSS_NEEDDATA, "append_data returned %d", err);
    }
    css_stylesheet_data_done(ctx->sheet);

    ck_assert_int_eq(css_select_ctx_create(&ctx->select_ctx), CSS_OK);
    ck_assert_int_eq(css_select_ctx_append_sheet(ctx->select_ctx, ctx->sheet, CSS_ORIGIN_AUTHOR, NULL), CSS_OK);

    ck_assert_int_eq(lwc_intern_string("div", 3, &ctx->node_name), lwc_error_ok);

    test_node node = {
        .name = ctx->node_name,
        .libcss_node_data = NULL,
    };

    ck_assert_int_eq(
        css_select_style(ctx->select_ctx, &node, &unit_ctx, &media, NULL, &select_handler, NULL, &ctx->results),
        CSS_OK);
    if (node.libcss_node_data != NULL) {
        ck_assert_int_eq(
            css_libcss_node_data_handler(&select_handler, CSS_NODE_DELETED, NULL, &node, NULL, node.libcss_node_data),
            CSS_OK);
        node.libcss_node_data = NULL;
    }
    ck_assert_ptr_nonnull(ctx->results);
    ck_assert_ptr_nonnull(ctx->results->styles[CSS_PSEUDO_ELEMENT_NONE]);

    return ctx->results->styles[CSS_PSEUDO_ELEMENT_NONE];
}

static void destroy_select_ctx(select_ctx *ctx)
{
    if (ctx->results != NULL)
        css_select_results_destroy(ctx->results);
    if (ctx->select_ctx != NULL)
        css_select_ctx_destroy(ctx->select_ctx);
    if (ctx->sheet != NULL)
        css_stylesheet_destroy(ctx->sheet);
    if (ctx->node_name != NULL)
        lwc_string_unref(ctx->node_name);
}

START_TEST(test_lh_length_to_px_calc)
{
    select_ctx ctx;
    const char *css = "* { margin-left: calc(10px + 5px); }";
    const css_computed_style *style = select_style_from_css(css, &ctx);

    css_fixed_or_calc len = {.value = 0};
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = css_computed_margin_left(style, &len, &unit);

    ck_assert_int_eq(type, CSS_MARGIN_SET);
    ck_assert_int_eq(unit, CSS_UNIT_CALC);

    int px = -1;
    bool ok = lh__length_to_px(style, &unit_ctx, 200, len, unit, &px);
    ck_assert(ok);
    ck_assert_int_eq(px, 15);

    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_lh_length_to_px_pct_invalid)
{
    select_ctx ctx;
    const char *css = "* { margin-left: 50%; }";
    const css_computed_style *style = select_style_from_css(css, &ctx);

    css_fixed_or_calc len = {.value = 0};
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = css_computed_margin_left(style, &len, &unit);

    ck_assert_int_eq(type, CSS_MARGIN_SET);
    ck_assert_int_eq(unit, CSS_UNIT_PCT);

    int px = -1;
    bool ok = lh__length_to_px(style, &unit_ctx, -1, len, unit, &px);
    ck_assert_int_eq(ok, 0);

    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_calculate_mbp_width_calc)
{
    select_ctx ctx;
    const char *css =
        "* {"
        "  margin-left: calc(10px + 5px);"
        "  padding-left: calc(2px + 3px);"
        "  border-left-width: calc(1px + 2px);"
        "  border-left-style: solid;"
        "}";

    const css_computed_style *style = select_style_from_css(css, &ctx);

    int fixed = 0;
    float frac = 0.0f;
    calculate_mbp_width(&unit_ctx, style, LEFT, true, true, true, &fixed, &frac);

    ck_assert_int_eq(fixed, 23);
    ck_assert_msg(frac == 0.0f, "expected frac=0.0f, got %f", frac);

    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_layout_find_dimensions_calc_height_unknown_is_auto)
{
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css("* { height: calc(50% + 10px); }", &ctx);
    struct box box;
    int height = 0;

    memset(&box, 0, sizeof(box));
    box.type = BOX_BLOCK;
    box.style = (css_computed_style *)style;

    layout_find_dimensions(&unit_ctx, 200, -1, &box, style, NULL, &height, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    ck_assert_int_eq(height, AUTO);

    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_layout_find_dimensions_calc_height_uses_viewport)
{
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css("* { height: calc(50% + 10px); }", &ctx);
    struct box box;
    int height = 0;

    memset(&box, 0, sizeof(box));
    box.type = BOX_BLOCK;
    box.style = (css_computed_style *)style;

    layout_find_dimensions(&unit_ctx, 200, 300, &box, style, NULL, &height, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    ck_assert_int_eq(height, 160);

    destroy_select_ctx(&ctx);
}
END_TEST

Suite *layout_calc_suite(void)
{
    Suite *s = suite_create("LayoutCalc");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_lh_length_to_px_calc);
    tcase_add_test(tc_core, test_lh_length_to_px_pct_invalid);
    tcase_add_test(tc_core, test_calculate_mbp_width_calc);
    tcase_add_test(tc_core, test_layout_find_dimensions_calc_height_unknown_is_auto);
    tcase_add_test(tc_core, test_layout_find_dimensions_calc_height_uses_viewport);

    suite_add_tcase(s, tc_core);
    return s;
}

int main(void)
{
    int number_failed;
    Suite *s = layout_calc_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
