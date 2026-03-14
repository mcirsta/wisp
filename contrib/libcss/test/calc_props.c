#include <stdio.h>
#include <string.h>

#include <libcss/computed.h>
#include <libcss/libcss.h>
#include <libcss/select.h>

#include <libwapcaplet/libwapcaplet.h>

#include "utils/css_utils.h"
#include "testutils.h"

typedef struct test_node {
    lwc_string *name;
    void *libcss_node_data;
} test_node;

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

static css_error node_has_attribute_prefix(void *pw, void *node, const css_qname *qname, lwc_string *value,
    bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);
    *match = false;
    return CSS_OK;
}

static css_error node_has_attribute_suffix(void *pw, void *node, const css_qname *qname, lwc_string *value,
    bool *match)
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

typedef uint8_t (*calc_len_getter)(const css_computed_style *style, css_fixed_or_calc *length, css_unit *unit);

typedef struct {
    css_stylesheet *sheet;
    css_select_ctx *select_ctx;
    css_select_results *results;
    lwc_string *node_name;
} style_case;

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

static css_computed_style *select_style_from_css(const char *css, style_case *ctx)
{
    css_stylesheet_params params = {
        .params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_3,
        .charset = "UTF-8",
        .url = "calc_props",
        .title = "calc_props",
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
    assert(css_stylesheet_create(&params, &ctx->sheet) == CSS_OK);
    {
        css_error err = css_stylesheet_append_data(ctx->sheet, (const uint8_t *)css, strlen(css));
        assert(err == CSS_OK || err == CSS_NEEDDATA);
    }
    css_stylesheet_data_done(ctx->sheet);

    assert(css_select_ctx_create(&ctx->select_ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx->select_ctx, ctx->sheet, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);

    assert(lwc_intern_string("div", 3, &ctx->node_name) == lwc_error_ok);

    test_node node = {.name = ctx->node_name, .libcss_node_data = NULL};
    assert(css_select_style(ctx->select_ctx, &node, &unit_ctx, &media, NULL, &select_handler, NULL, &ctx->results) ==
        CSS_OK);
    if (node.libcss_node_data != NULL) {
        assert(css_libcss_node_data_handler(
                   &select_handler, CSS_NODE_DELETED, NULL, &node, NULL, node.libcss_node_data) == CSS_OK);
        node.libcss_node_data = NULL;
    }
    assert(ctx->results != NULL);
    assert(ctx->results->styles[CSS_PSEUDO_ELEMENT_NONE] != NULL);

    return ctx->results->styles[CSS_PSEUDO_ELEMENT_NONE];
}

static void destroy_style_case(style_case *ctx)
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

static void expect_calc_px(const char *label, const css_computed_style *style, const css_unit_ctx *unit_ctx,
    int available_px, calc_len_getter getter, uint8_t expected_type, int expected_px)
{
    css_fixed_or_calc length = {.value = 0};
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = getter(style, &length, &unit);

    assert(type == expected_type);
    if (unit == CSS_UNIT_CALC) {
        assert(length.calc != NULL);
    }

    int px = -1;
    css_error err = css_computed_length_to_px(style, unit_ctx, available_px, length, unit, &px);
    if (err != CSS_OK) {
        printf("FAIL - %s calc evaluation failed (%d)\n", label, err);
        exit(EXIT_FAILURE);
    }
    if (px != expected_px) {
        printf("FAIL - %s px mismatch: got=%d expected=%d unit=%d type=%u\n", label, px, expected_px, unit, type);
        exit(EXIT_FAILURE);
    }
}

static void test_calc_property_getters(void)
{
    style_case ctx;
    const char *css =
        "* {"
        "  column-gap: calc(10px + 5px);"
        "  row-gap: calc(2em + 4px);"
        "  column-width: calc(100px + 20px);"
        "}";
    const css_computed_style *style = select_style_from_css(css, &ctx);

    expect_calc_px("column-gap", style, &unit_ctx, 200, css_computed_column_gap, CSS_COLUMN_GAP_SET, 15);
    expect_calc_px("row-gap", style, &unit_ctx, 200, css_computed_row_gap, CSS_ROW_GAP_SET, 36);
    expect_calc_px("column-width", style, &unit_ctx, 200, css_computed_column_width, CSS_COLUMN_WIDTH_SET, 120);
    destroy_style_case(&ctx);
}

static void test_length_to_px_invalid_percent(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css("* { margin-left: 50%; }", &ctx);

    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    int px = 0;
    uint8_t type = css_computed_margin_left(style, &length, &unit);
    assert(type == CSS_MARGIN_SET);
    assert(unit == CSS_UNIT_PCT);
    assert(css_computed_length_to_px(style, &unit_ctx, -1, length, unit, &px) == CSS_INVALID);

    destroy_style_case(&ctx);
}

static void test_relative_offsets_with_calc(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css("* { position: relative; top: auto; bottom: auto; }", &ctx);

    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    assert(css_computed_top(style, &length, &unit) == CSS_TOP_SET);
    assert(unit == CSS_UNIT_PX);
    assert(length.value == 0);
    destroy_style_case(&ctx);
}

static void test_width_px_calc(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css("* { width: calc(50% + 10px); }", &ctx);
    int px = 0;
    css_fixed_or_calc length = (css_fixed_or_calc)0;
    css_unit unit = CSS_UNIT_PX;
    uint8_t width_type = css_computed_width(style, &length, &unit);

    assert(css_computed_width_px(style, &unit_ctx, 200, &px) == CSS_WIDTH_SET);
    assert(px == 110);
    assert(css_computed_width_px(style, &unit_ctx, -1, &px) == CSS_WIDTH_AUTO);
    assert(width_type == CSS_WIDTH_SET);
    assert(unit == CSS_UNIT_CALC);
    assert(css_computed_length_to_px(style, &unit_ctx, -1, length, unit, &px) == CSS_INVALID);

    destroy_style_case(&ctx);
}

static void test_flex_basis_px_calc(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css("* { flex-basis: calc(50% + 10px); }", &ctx);
    int px = 0;

    assert(css_computed_flex_basis_px(style, &unit_ctx, 200, &px) == CSS_FLEX_BASIS_SET);
    assert(px == 110);
    assert(css_computed_flex_basis_px(style, &unit_ctx, -1, &px) == CSS_FLEX_BASIS_AUTO);

    destroy_style_case(&ctx);
}

int main(void)
{
    test_calc_property_getters();
    test_length_to_px_invalid_percent();
    test_relative_offsets_with_calc();
    test_width_px_calc();
    test_flex_basis_px_calc();

    printf("PASS\n");
    return 0;
}
