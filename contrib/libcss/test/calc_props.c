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
    struct test_node *parent;
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
    *parent = ((test_node *)node)->parent;
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
    .container_width = 200 * (1 << CSS_RADIX_POINT),
    .container_height = 100 * (1 << CSS_RADIX_POINT),
    .font_size_default = 16 * (1 << CSS_RADIX_POINT),
    .font_size_minimum = 0,
    .device_dpi = 96 * (1 << CSS_RADIX_POINT),
    .root_style = NULL,
    .pw = NULL,
    .measure = NULL,
};

static css_computed_style *select_style_from_css_for_node(const char *css, style_case *ctx, bool is_child)
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

    test_node parent = {.name = ctx->node_name};
    test_node node = {.name = ctx->node_name, .parent = is_child ? &parent : NULL};
    assert(css_select_style(ctx->select_ctx, &node, &unit_ctx, &media, NULL, &select_handler, NULL, &ctx->results) ==
        CSS_OK);
    if (node.libcss_node_data != NULL) {
        assert(css_libcss_node_data_handler(
                   &select_handler, CSS_NODE_DELETED, NULL, &node, NULL, node.libcss_node_data) == CSS_OK);
        node.libcss_node_data = NULL;
    }
    /* Selection may create a bloom filter on the synthetic parent too. */
    if (parent.libcss_node_data != NULL) {
        assert(css_libcss_node_data_handler(
                   &select_handler, CSS_NODE_DELETED, NULL, &parent, NULL, parent.libcss_node_data) == CSS_OK);
    }
    assert(ctx->results != NULL);
    assert(ctx->results->styles[CSS_PSEUDO_ELEMENT_NONE] != NULL);

    return ctx->results->styles[CSS_PSEUDO_ELEMENT_NONE];
}

static css_computed_style *select_style_from_css(const char *css, style_case *ctx)
{
    return select_style_from_css_for_node(css, ctx, false);
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

static void test_length_is_zero(void)
{
    static const struct {
        const char *value;
        bool zero;
    } cases[] = {
        {"0px", true}, {"0%", true}, {"50%", false}, {"0.5px", false},
        {"calc(5px - 5px)", true}, {"calc(10px - 5px)", false},
        {"calc(5px - 10px)", false}, {"calc(0.75px - 0.25px)", false},
        {"calc(1em - 16px)", true}, {"calc(50% - 100px)", false},
        {"calc(1px / 3 - 1px / 3)", true},
        {"calc(0%)", true}, {"calc(0% + 0px)", true}, {"calc(50% - 50%)", true},
        {"calc((50% + 5px) - (50% + 5px))", true},
        {"calc(2 * (25% + 2px) - 50% - 4px)", true},
        {"calc((25% + 2px) * 2 - 50% - 4px)", true},
        {"calc((100% + 20px) / 2 - 50% - 10px)", true},
        {"calc(50% - 50% + 1em - 16px)", true},
        {"calc((50% + 10px) * 0)", true},
        {"calc(0 * (50% + 10px))", true},
        {"calc(50% - 25%)", false}, {"calc(50% - 50% + 0.5px)", false},
        {"calc(50% - 50% - 0.5px)", false}, {"calc(50% + 5px - 5px)", false},
        /* Lost precision and saturation must not prove a coefficient zero. */
        {"calc(1% / 2048)", false}, {"calc((1 / 2048) * 1%)", false},
        {"calc(0.001% * 0.5)", false},
        {"calc(1500000% + 1500000% - 1500000% - 1500000%)", false},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char css[128];
        snprintf(css, sizeof(css), "* { font-size:16px; margin-left:%s; }", cases[i].value);
        style_case ctx;
        const css_computed_style *style = select_style_from_css(css, &ctx);
        css_fixed_or_calc length = {.value = 0};
        css_unit unit = CSS_UNIT_PX;
        assert(css_computed_margin_left(style, &length, &unit) == CSS_MARGIN_SET);
        if (css_computed_length_is_zero(style, &unit_ctx, length, unit) != cases[i].zero) {
            printf("FAIL - zero classification for %s\n", cases[i].value);
            exit(EXIT_FAILURE);
        }
        destroy_style_case(&ctx);
    }
}

static void test_zero_analysis_preserves_percentage_resolution(void)
{
    const char *values[] = {"calc(0%)", "calc(50% - 50%)", "calc((50% + 10px) - (50% + 10px))"};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        char css[128];
        snprintf(css, sizeof(css), "* { width:%s; }", values[i]);
        style_case ctx;
        const css_computed_style *style = select_style_from_css(css, &ctx);
        css_fixed_or_calc length;
        css_unit unit;
        assert(css_computed_width(style, &length, &unit) == CSS_WIDTH_SET);
        assert(unit == CSS_UNIT_CALC);
        /* Repeat to check calculator-stack reuse and ensure the zero query
         * never rewrites the expression or discards its percentage type. */
        for (int pass = 0; pass < 2; pass++) {
            int px;
            assert(css_computed_length_is_zero(style, &unit_ctx, length, unit));
            assert(css_computed_width_px(style, &unit_ctx, -1, &px) == CSS_WIDTH_AUTO);
            assert(css_computed_length_to_px(style, &unit_ctx, -1, length, unit, &px) == CSS_INVALID);
            assert(css_computed_width_px(style, &unit_ctx, 200, &px) == CSS_WIDTH_SET);
            assert(px == 0);
        }
        destroy_style_case(&ctx);
    }
}

static void test_calc_division_by_zero(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css("* { width:calc(1px / 0); }", &ctx);
    css_fixed_or_calc length;
    css_unit unit;
    int px;
    assert(css_computed_width(style, &length, &unit) == CSS_WIDTH_SET);
    assert(unit == CSS_UNIT_CALC);
    assert(!css_computed_length_is_zero(style, &unit_ctx, length, unit));
    assert(css_computed_length_to_px(style, &unit_ctx, 200, length, unit, &px) == CSS_INVALID);
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

/* Invalid math must be discarded at parse time, retaining the earlier
 * cascade value. It must not replace that value and fail only during layout. */
static void test_calc_grammar_cascade(void)
{
    const char *invalid[] = {"calc(0 + 2px)", "calc(2px + 1)", "calc(2 * 3)", "calc(1px * 2px)",
        "calc(1fr)", "calc(1px+ 2px)", "calc(1px +(2px))"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char css[160];
        snprintf(css, sizeof(css), "* { width: 35px; width: %s !important; }", invalid[i]);
        style_case ctx;
        const css_computed_style *style = select_style_from_css(css, &ctx);
        expect_calc_px(invalid[i], style, &unit_ctx, 200, css_computed_width, CSS_WIDTH_SET, 35);
        destroy_style_case(&ctx);
    }
    const struct { const char *value; int px; } valid[] = {
        {"calc(12px / 2 + 1px)", 7}, {"calc(12px / (1 + 2))", 4},
        {"calc(12px / calc(1 + 2))", 4}, {"calc(calc(12px + 3px) * 2)", 30},
        {"calc(2 * (50% - 5px))", 190}, {"calc(50% + calc(2em / 2))", 116},
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        char css[160];
        snprintf(css, sizeof(css), "* { font-size:16px; width:%s; }", valid[i].value);
        style_case ctx;
        const css_computed_style *style = select_style_from_css(css, &ctx);
        expect_calc_px(valid[i].value, style, &unit_ctx, 200, css_computed_width, CSS_WIDTH_SET, valid[i].px);
        destroy_style_case(&ctx);
    }
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

static void test_font_size_calc_roundtrip(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css("* { font-size: calc(10px + 6px); }", &ctx);

    /* Regression: calc pointer must survive compute_absolute_values on 64-bit builds. */
    expect_calc_px("font-size", style, &unit_ctx, 200, css_computed_font_size, CSS_FONT_SIZE_DIMENSION, 16);

    destroy_style_case(&ctx);
}

static void test_em_uses_calc_font_size(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("* { font-size: calc(10px + 10px); width: 2em; }", &ctx);
    int px = 0;

    assert(css_computed_width_px(style, &unit_ctx, 200, &px) == CSS_WIDTH_SET);
    assert(px == 40);

    destroy_style_case(&ctx);
}

static void test_ic_calc_width(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css("* { width: calc(1ic + 1px); }", &ctx);
    int px = 0;

    /* Regression: calc() lengths using ic must convert through css__to_css_unit(). */
    assert(css_computed_width_px(style, &unit_ctx, 200, &px) == CSS_WIDTH_SET);
    assert(px == 17);

    destroy_style_case(&ctx);
}

static void test_cqw_calc_width_uses_container(void)
{
    style_case ctx;
    css_unit_ctx cqw_ctx = unit_ctx;
    const css_computed_style *style = select_style_from_css("* { width: calc(50cqw + 10px); }", &ctx);
    int px = 0;

    cqw_ctx.container_width = 300 * (1 << CSS_RADIX_POINT);

    assert(css_computed_width_px(style, &cqw_ctx, 200, &px) == CSS_WIDTH_SET);
    assert(px == 160);

    destroy_style_case(&ctx);
}

static void test_cq_units_zero_container_resolve_to_zero(void)
{
    style_case ctx;
    css_unit_ctx cq_ctx = unit_ctx;
    const css_computed_style *style = select_style_from_css("* { width: 50cqw; height: 25cqh; }", &ctx);
    int px = 0;

    cq_ctx.container_width = 0;
    cq_ctx.container_height = 0;

    assert(css_computed_width_px(style, &cq_ctx, 200, &px) == CSS_WIDTH_SET);
    assert(px == 0);
    expect_calc_px("cqh-zero-container", style, &cq_ctx, -1, css_computed_height, CSS_HEIGHT_SET, 0);

    destroy_style_case(&ctx);
}

static void test_cq_units_unset_container_falls_back_to_viewport(void)
{
    style_case ctx;
    css_unit_ctx cq_ctx = unit_ctx;
    const css_computed_style *style = select_style_from_css("* { width: 50cqw; height: 25cqh; }", &ctx);
    int px = 0;

    cq_ctx.container_width = CSS_UNIT_CTX_UNSET;
    cq_ctx.container_height = CSS_UNIT_CTX_UNSET;

    assert(css_computed_width_px(style, &cq_ctx, 200, &px) == CSS_WIDTH_SET);
    assert(px == 100);
    expect_calc_px("cqh-unset-container", style, &cq_ctx, -1, css_computed_height, CSS_HEIGHT_SET, 25);

    destroy_style_case(&ctx);
}

static void test_clip_units_roundtrip(void)
{
    static const struct {
        const char *css;
        css_unit units[4];
        unsigned auto_mask;
    } cases[] = {
        {"* { clip: rect(1px, 2px, 3px, 4px); }", {CSS_UNIT_PX, CSS_UNIT_PX, CSS_UNIT_PX, CSS_UNIT_PX}, 0},
        {"* { clip: rect(1em, 2rem, 3vh, 4q); }", {CSS_UNIT_EM, CSS_UNIT_REM, CSS_UNIT_VH, CSS_UNIT_Q}, 0},
        {"* { clip: rect(auto, 2cqw, auto, 4px); }", {CSS_UNIT_PX, CSS_UNIT_CQW, CSS_UNIT_PX, CSS_UNIT_PX}, 5},
        {"* { clip: rect(1px, auto, 3cqh, auto); }", {CSS_UNIT_PX, CSS_UNIT_PX, CSS_UNIT_CQH, CSS_UNIT_PX}, 10},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        style_case ctx;
        const css_computed_style *selected = select_style_from_css(cases[i].css, &ctx);
        css_computed_style *composed;
        assert(css_computed_style_compose(selected, selected, &unit_ctx, &composed) == CSS_OK);
        const css_computed_style *styles[] = {selected, composed};
        for (size_t pass = 0; pass < 2; pass++) {
            css_computed_clip_rect r = {0};
            assert(css_computed_clip(styles[pass], &r) == CSS_CLIP_RECT);
            css_unit units[] = {r.tunit, r.runit, r.bunit, r.lunit};
            css_fixed values[] = {r.top, r.right, r.bottom, r.left};
            bool automatic[] = {r.top_auto, r.right_auto, r.bottom_auto, r.left_auto};
            for (unsigned edge = 0; edge < 4; edge++) {
                assert(automatic[edge] == !!(cases[i].auto_mask & (1u << edge)));
                if (!automatic[edge]) {
                    assert(units[edge] == cases[i].units[edge]);
                    assert(values[edge] == INTTOFIX(edge + 1));
                }
            }
        }
        css_computed_style_destroy(composed);
        destroy_style_case(&ctx);
    }
}

static void test_disclosure_counter_styles(void)
{
    static const struct {
        const char *css;
        uint8_t type;
        const char *text;
    } cases[] = {
        {"* { list-style:disclosure-open inside }", CSS_LIST_STYLE_TYPE_DISCLOSURE_OPEN, "\xe2\x96\xbe "},
        {"* { list-style:disclosure-closed inside }", CSS_LIST_STYLE_TYPE_DISCLOSURE_CLOSED, "\xe2\x96\xb8 "},
        {"* { direction:rtl; list-style:disclosure-closed inside }",
            CSS_LIST_STYLE_TYPE_DISCLOSURE_CLOSED, "\xe2\x97\x82 "},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        style_case ctx;
        const css_computed_style *selected = select_style_from_css(cases[i].css, &ctx);
        css_computed_style *composed;
        assert(css_computed_style_compose(selected, selected, &unit_ctx, &composed) == CSS_OK);
        const css_computed_style *styles[] = {selected, composed};
        for (size_t pass = 0; pass < 2; pass++) {
            assert(css_computed_list_style_type(styles[pass]) == cases[i].type);
            assert(css_computed_list_style_position(styles[pass]) == CSS_LIST_STYLE_POSITION_INSIDE);
            for (int value = -1; value <= 1; value++) {
                char text[16] = {0};
                size_t length;
                assert(css_computed_format_list_style(styles[pass], value, text, sizeof(text), &length) == CSS_OK);
                assert(length == strlen(cases[i].text));
                assert(memcmp(text, cases[i].text, length) == 0);
            }
        }
        css_computed_style_destroy(composed);
        destroy_style_case(&ctx);
    }
}

static void expect_line_height(const char *label, const char *stage, const css_computed_style *style,
    uint8_t expected_type, css_fixed expected_value)
{
    css_fixed_or_calc value = {0};
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = css_computed_line_height(style, &value, &unit);
    if (type != expected_type || (type != CSS_LINE_HEIGHT_NORMAL && value.value != expected_value) ||
        (type == CSS_LINE_HEIGHT_DIMENSION && unit != CSS_UNIT_PX)) {
        printf("FAIL - line-height %s (%s): type=%u unit=%d value=%d; expected type=%u value=%d\n",
            label, stage, type, unit, value.value, expected_type, expected_value);
        exit(EXIT_FAILURE);
    }
}

static void test_line_height_inheritance(void)
{
    /* CSS 2.2 10.8.1: lengths/percentages inherit as absolute lengths;
     * numbers and normal retain their type. Expected values are CSS pixels,
     * including fractional values, independent of device DPI. */
    const struct {
        const char *declarations;
        uint8_t type;
        css_fixed value;
    } cases[] = {
        {"line-height:1em", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(40)},
        {"line-height:125%", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(50)},
        {"line-height:1rem", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(40)},
        {"line-height:1ic", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(40)},
        {"line-height:1ex", CSS_LINE_HEIGHT_DIMENSION, FMUL(INTTOFIX(40), FLTTOFIX(.6))},
        {"line-height:1ch", CSS_LINE_HEIGHT_DIMENSION, FMUL(INTTOFIX(40), FLTTOFIX(.4))},
        {"line-height:40px", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(40)},
        {"line-height:1.25", CSS_LINE_HEIGHT_NUMBER, FLTTOFIX(1.25)},
        {"line-height:normal", CSS_LINE_HEIGHT_NORMAL, 0},
        {"font-size:15.5px;line-height:200%", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(31)},
        {"font-size:15.5px;line-height:1em", CSS_LINE_HEIGHT_DIMENSION, FLTTOFIX(15.5)},
        {"font-size:0;line-height:1em", CSS_LINE_HEIGHT_DIMENSION, 0},
        {"font-size:calc(30px + 10px);line-height:1em", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(40)},
        {"font-size:calc(30px + 10px);line-height:calc(10px + 1em)", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(50)},
        {"line-height:calc(1em + 10px)", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(50)},
        {"line-height:calc(125% + 10px)", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(60)},
        {"font-size:15.5px;line-height:calc(200% + 1px)", CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(32)},
        {"line-height:calc(1 + .25)", CSS_LINE_HEIGHT_NUMBER, FLTTOFIX(1.25)},
        {"line-height:calc(1em - 50px)", CSS_LINE_HEIGHT_DIMENSION, 0},
    };
    style_case child_ctx, grandchild_ctx, override_ctx;
    const css_computed_style *child = select_style_from_css_for_node("* {font-size:20px}", &child_ctx, true);
    const css_computed_style *grandchild = select_style_from_css_for_node(
        "* {font-size:10px;line-height:inherit}", &grandchild_ctx, true);
    const css_computed_style *override = select_style_from_css_for_node(
        "* {font-size:20px;line-height:1em}", &override_ctx, true);
    for (int dpi = 96; dpi <= 192; dpi += 96) {
        unit_ctx.device_dpi = INTTOFIX(dpi);
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[160];
            snprintf(css, sizeof(css), "* {font-size:40px;%s}", cases[i].declarations);
            style_case parent_ctx;
            const css_computed_style *parent = select_style_from_css(css, &parent_ctx);
            expect_line_height(cases[i].declarations, "root selection", parent, cases[i].type, cases[i].value);
            css_unit_ctx compose_ctx = unit_ctx;
            compose_ctx.root_style = parent;
            css_computed_style *composed_child, *composed_grandchild, *overridden;
            assert(css_computed_style_compose(parent, child, &compose_ctx, &composed_child) == CSS_OK);
            expect_line_height(cases[i].declarations, "implicit inheritance", composed_child,
                cases[i].type, cases[i].value);
            assert(css_computed_style_compose(
                       composed_child, grandchild, &compose_ctx, &composed_grandchild) == CSS_OK);
            expect_line_height(cases[i].declarations, "explicit inheritance", composed_grandchild,
                cases[i].type, cases[i].value);
            assert(css_computed_style_compose(parent, override, &compose_ctx, &overridden) == CSS_OK);
            expect_line_height(cases[i].declarations, "child declaration", overridden,
                CSS_LINE_HEIGHT_DIMENSION, INTTOFIX(20));
            css_computed_style_destroy(overridden);
            css_computed_style_destroy(composed_grandchild);
            css_computed_style_destroy(composed_child);
            destroy_style_case(&parent_ctx);
        }
    }
    unit_ctx.device_dpi = F_96;
    destroy_style_case(&override_ctx);
    destroy_style_case(&grandchild_ctx);
    destroy_style_case(&child_ctx);
}

int main(void)
{
    test_calc_property_getters();
    test_length_to_px_invalid_percent();
    test_length_is_zero();
    test_zero_analysis_preserves_percentage_resolution();
    test_calc_division_by_zero();
    test_relative_offsets_with_calc();
    test_width_px_calc();
    test_calc_grammar_cascade();
    test_flex_basis_px_calc();
    test_font_size_calc_roundtrip();
    test_em_uses_calc_font_size();
    test_ic_calc_width();
    test_cqw_calc_width_uses_container();
    test_cq_units_zero_container_resolve_to_zero();
    test_cq_units_unset_container_falls_back_to_viewport();
    test_clip_units_roundtrip();
    test_disclosure_counter_styles();
    test_line_height_inheritance();

    printf("PASS\n");
    return 0;
}
