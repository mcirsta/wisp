/*
 * Tests for calc() length handling in layout helpers.
 */

#include <check.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libcss/libcss.h>
#include <libcss/select.h>
#include <libwapcaplet/libwapcaplet.h>

#include "content/handlers/html/layout_internal.h"
#include "content/handlers/html/redraw_helpers.h"

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

typedef struct test_node {
    lwc_string *name;
    void *libcss_node_data;
    struct test_node *parent;
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
    *match = ((test_node *)node)->parent == NULL;
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
    .container_width = 200 * (1 << CSS_RADIX_POINT),
    .container_height = 100 * (1 << CSS_RADIX_POINT),
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

static css_computed_style *select_style_from_css_node(const char *css, select_ctx *ctx, bool root)
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

    test_node parent = {.name = ctx->node_name};
    test_node node = {
        .name = ctx->node_name,
        .libcss_node_data = NULL,
        .parent = root ? NULL : &parent,
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

static const css_computed_style *select_style_from_css(const char *css, select_ctx *ctx)
{
    return select_style_from_css_node(css, ctx, true);
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

START_TEST(test_layout_find_dimensions_cq_units_use_box_container)
{
    select_ctx ctx;
    css_unit_ctx cq_ctx = unit_ctx;
    const css_computed_style *style =
        select_style_from_css("* { width: 50cqw; height: 25cqh; margin-left: 10cqw; }", &ctx);
    struct box box;
    int width = 0;
    int height = 0;
    int margin[4] = {0};

    memset(&box, 0, sizeof(box));
    box.type = BOX_BLOCK;
    box.style = (css_computed_style *)style;

    cq_ctx.container_width = 999 * (1 << CSS_RADIX_POINT);
    cq_ctx.container_height = 777 * (1 << CSS_RADIX_POINT);

    layout_find_dimensions(
        &cq_ctx, 200, 300, &box, style, &width, &height, NULL, NULL, NULL, NULL, margin, NULL, NULL);

    ck_assert_int_eq(width, 100);
    ck_assert_int_eq(height, 75);
    ck_assert_int_eq(margin[LEFT], 20);

    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_lh_length_to_px_cq_units_zero_container_resolve_to_zero)
{
    select_ctx ctx;
    css_unit_ctx cq_ctx = unit_ctx;
    const css_computed_style *style = select_style_from_css("* { width: 50cqw; height: 25cqh; }", &ctx);
    css_fixed_or_calc len = {.value = 0};
    css_unit unit = CSS_UNIT_PX;
    int px = -1;

    cq_ctx.container_width = 0;
    cq_ctx.container_height = 0;

    ck_assert_int_eq(css_computed_width(style, &len, &unit), CSS_WIDTH_SET);
    ck_assert_int_eq(unit, CSS_UNIT_CQW);
    ck_assert(lh__length_to_px(style, &cq_ctx, -1, len, unit, &px));
    ck_assert_int_eq(px, 0);

    len.value = 0;
    unit = CSS_UNIT_PX;
    px = -1;
    ck_assert_int_eq(css_computed_height(style, &len, &unit), CSS_HEIGHT_SET);
    ck_assert_int_eq(unit, CSS_UNIT_CQH);
    ck_assert(lh__length_to_px(style, &cq_ctx, -1, len, unit, &px));
    ck_assert_int_eq(px, 0);

    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_lh_length_to_px_cq_units_unset_container_uses_viewport)
{
    select_ctx ctx;
    css_unit_ctx cq_ctx = unit_ctx;
    const css_computed_style *style = select_style_from_css("* { width: 50cqw; height: 25cqh; }", &ctx);
    css_fixed_or_calc len = {.value = 0};
    css_unit unit = CSS_UNIT_PX;
    int px = -1;

    cq_ctx.container_width = CSS_UNIT_CTX_UNSET;
    cq_ctx.container_height = CSS_UNIT_CTX_UNSET;

    ck_assert_int_eq(css_computed_width(style, &len, &unit), CSS_WIDTH_SET);
    ck_assert_int_eq(unit, CSS_UNIT_CQW);
    ck_assert(lh__length_to_px(style, &cq_ctx, -1, len, unit, &px));
    ck_assert_int_eq(px, 100);

    len.value = 0;
    unit = CSS_UNIT_PX;
    px = -1;
    ck_assert_int_eq(css_computed_height(style, &len, &unit), CSS_HEIGHT_SET);
    ck_assert_int_eq(unit, CSS_UNIT_CQH);
    ck_assert(lh__length_to_px(style, &cq_ctx, -1, len, unit, &px));
    ck_assert_int_eq(px, 25);

    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_inset_shorthand_sets_absolute_offsets)
{
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css("* { position: absolute; inset: 0; }", &ctx);
    css_fixed_or_calc len = {.value = 123};
    css_unit unit = CSS_UNIT_EM;

    ck_assert_int_eq(css_computed_top(style, &len, &unit), CSS_TOP_SET);
    ck_assert_int_eq(unit, CSS_UNIT_PX);
    ck_assert_int_eq(len.value, 0);

    len.value = 123;
    unit = CSS_UNIT_EM;
    ck_assert_int_eq(css_computed_right(style, &len, &unit), CSS_RIGHT_SET);
    ck_assert_int_eq(unit, CSS_UNIT_PX);
    ck_assert_int_eq(len.value, 0);

    len.value = 123;
    unit = CSS_UNIT_EM;
    ck_assert_int_eq(css_computed_bottom(style, &len, &unit), CSS_BOTTOM_SET);
    ck_assert_int_eq(unit, CSS_UNIT_PX);
    ck_assert_int_eq(len.value, 0);

    len.value = 123;
    unit = CSS_UNIT_EM;
    ck_assert_int_eq(css_computed_left(style, &len, &unit), CSS_LEFT_SET);
    ck_assert_int_eq(unit, CSS_UNIT_PX);
    ck_assert_int_eq(len.value, 0);

    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_inset_inline_shorthand_sets_left_and_right)
{
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css("* { position: absolute; inset-inline: 2px 4px; }", &ctx);
    css_fixed_or_calc len = {.value = 123};
    css_unit unit = CSS_UNIT_EM;

    ck_assert_int_eq(css_computed_left(style, &len, &unit), CSS_LEFT_SET);
    ck_assert_int_eq(unit, CSS_UNIT_PX);
    ck_assert_int_eq(len.value, 2 * (1 << CSS_RADIX_POINT));

    len.value = 123;
    unit = CSS_UNIT_EM;
    ck_assert_int_eq(css_computed_right(style, &len, &unit), CSS_RIGHT_SET);
    ck_assert_int_eq(unit, CSS_UNIT_PX);
    ck_assert_int_eq(len.value, 4 * (1 << CSS_RADIX_POINT));

    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_inset_block_shorthand_sets_top_and_bottom)
{
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css("* { position: absolute; inset-block: 1px 3px; }", &ctx);
    css_fixed_or_calc len = {.value = 123};
    css_unit unit = CSS_UNIT_EM;

    ck_assert_int_eq(css_computed_top(style, &len, &unit), CSS_TOP_SET);
    ck_assert_int_eq(unit, CSS_UNIT_PX);
    ck_assert_int_eq(len.value, 1 * (1 << CSS_RADIX_POINT));

    len.value = 123;
    unit = CSS_UNIT_EM;
    ck_assert_int_eq(css_computed_bottom(style, &len, &unit), CSS_BOTTOM_SET);
    ck_assert_int_eq(unit, CSS_UNIT_PX);
    ck_assert_int_eq(len.value, 3 * (1 << CSS_RADIX_POINT));

    destroy_select_ctx(&ctx);
}
END_TEST

static const struct {
    const char *declaration;
    uint8_t type;
    css_fixed numerator, denominator;
} ratio_cases[] = {
    {"", CSS_ASPECT_RATIO_AUTO, 0, 0},
    {"aspect-ratio: 16 / 9", CSS_ASPECT_RATIO_RATIO, (16 * (1 << CSS_RADIX_POINT)), (9 * (1 << CSS_RADIX_POINT))},
    {"aspect-ratio: 1.5", CSS_ASPECT_RATIO_RATIO, (3 * (1 << CSS_RADIX_POINT) / 2), (1 * (1 << CSS_RADIX_POINT))},
    {"aspect-ratio: auto 3/2", CSS_ASPECT_RATIO_AUTO_RATIO, (3 * (1 << CSS_RADIX_POINT)), (2 * (1 << CSS_RADIX_POINT))},
    {"aspect-ratio: 3/2 auto", CSS_ASPECT_RATIO_AUTO_RATIO, (3 * (1 << CSS_RADIX_POINT)), (2 * (1 << CSS_RADIX_POINT))},
    {"aspect-ratio: 0/2", CSS_ASPECT_RATIO_RATIO, 0, (2 * (1 << CSS_RADIX_POINT))},
    {"aspect-ratio: 2/0", CSS_ASPECT_RATIO_RATIO, (2 * (1 << CSS_RADIX_POINT)), 0},
    {"aspect-ratio: -1/2", CSS_ASPECT_RATIO_AUTO, 0, 0},
    {"aspect-ratio: 1/-2", CSS_ASPECT_RATIO_AUTO, 0, 0},
    {"aspect-ratio: 1px/2", CSS_ASPECT_RATIO_AUTO, 0, 0},
    {"aspect-ratio: 1/", CSS_ASPECT_RATIO_AUTO, 0, 0},
    {"aspect-ratio: auto auto", CSS_ASPECT_RATIO_AUTO, 0, 0},
    {"aspect-ratio: 1 2", CSS_ASPECT_RATIO_AUTO, 0, 0},
    {"aspect-ratio: 2; aspect-ratio: initial", CSS_ASPECT_RATIO_AUTO, 0, 0},
    {"aspect-ratio: 2; aspect-ratio: unset", CSS_ASPECT_RATIO_AUTO, 0, 0},
    {"aspect-ratio: 2 !important; aspect-ratio: 3", CSS_ASPECT_RATIO_RATIO, (2 * (1 << CSS_RADIX_POINT)),
        (1 * (1 << CSS_RADIX_POINT))},
    {"--r: 3/2; aspect-ratio: var(--r)", CSS_ASPECT_RATIO_RATIO, (3 * (1 << CSS_RADIX_POINT)),
        (2 * (1 << CSS_RADIX_POINT))},
};

START_TEST(test_aspect_ratio_parse)
{
    select_ctx ctx;
    char css[200];
    snprintf(css, sizeof(css), "* { %s; }", ratio_cases[_i].declaration);
    const css_computed_style *style = select_style_from_css(css, &ctx);
    css_fixed numerator = 0, denominator = 0;
    ck_assert_int_eq(css_computed_aspect_ratio(style, &numerator, &denominator), ratio_cases[_i].type);
    ck_assert_int_eq(numerator, ratio_cases[_i].numerator);
    ck_assert_int_eq(denominator, ratio_cases[_i].denominator);
    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_aspect_ratio_dimensions)
{
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css(
        "* { aspect-ratio: 2; height: 100px; box-sizing: border-box; padding: 10px; border: 2px solid; }", &ctx);
    struct box box = {.type = BOX_BLOCK, .style = (css_computed_style *)style};
    layout_find_dimensions(&unit_ctx, 500, -1, &box, style, &box.width, &box.height, NULL, NULL, NULL, NULL, box.margin,
        box.padding, box.border);
    ck_assert_int_eq(box.height, 76);
    ck_assert_int_eq(box.width, 176);
    ck_assert_int_eq(layout_ratio_dimension(&box, 176, true), 76);
    ck_assert_int_eq(layout_ratio_dimension(&box, AUTO, true), AUTO);
    ck_assert_int_eq(layout_ratio_dimension(&box, INT_MAX, false), INT_MAX);
    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_aspect_ratio_content_minimum)
{
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css(_i == 0 ? "* { aspect-ratio: 2; }"
            : _i == 1                                               ? "* { aspect-ratio: 2; min-height: 0; }"
                                                                    : "* { aspect-ratio: 2; overflow: hidden; }",
        &ctx);
    struct box box = {.type = BOX_BLOCK, .style = (css_computed_style *)style, .width = 200, .height = AUTO};
    layout_prepare_aspect_ratio_height(&box);
    ck_assert_int_eq(box.height, 100);
    ck_assert(box.flags & ASPECT_RATIO_HEIGHT);
    layout_ratio_content_height(&box, 150);
    ck_assert_int_eq(box.height, _i == 0 ? 150 : 100);
    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_aspect_ratio_height_constraints)
{
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css(
        "* { aspect-ratio: 2; height: 100px; min-height: 50%; max-height: calc(50% + 20px); "
        "box-sizing: border-box; padding: 10px; border: 2px solid; }",
        &ctx);
    struct box parent = {.type = BOX_BLOCK,
        .style = (css_computed_style *)style,
        .width = 500,
        .height = 300,
        .flags = ASPECT_RATIO_HEIGHT};
    struct box box = {.type = BOX_BLOCK, .style = (css_computed_style *)style, .parent = &parent};
    int max_height;
    struct css_size min_height;
    layout_find_dimensions(&unit_ctx, 500, -1, &box, style, &box.width, &box.height, NULL, NULL, &max_height,
        &min_height, box.margin, box.padding, box.border);
    ck_assert_int_eq(min_height.type, CSS_SIZE_SET);
    ck_assert_int_eq(min_height.value, 126);
    ck_assert_int_eq(max_height, 146);
    /* Min-height participates in the transfer from height to auto width. */
    ck_assert_int_eq(box.width, 276);
    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_aspect_ratio_compose)
{
    select_ctx parent_ctx, child_ctx;
    const css_computed_style *parent = select_style_from_css("* { aspect-ratio: 3/2; }", &parent_ctx);
    const css_computed_style *child = select_style_from_css_node(
        _i == 0 ? "* { aspect-ratio: inherit; }" : "* { }", &child_ctx, false);
    css_computed_style *composed = NULL;
    ck_assert_int_eq(css_computed_style_compose(parent, child, &unit_ctx, &composed), CSS_OK);
    css_fixed width = 0, height = 0;
    ck_assert_int_eq(
        css_computed_aspect_ratio(composed, &width, &height), _i == 0 ? CSS_ASPECT_RATIO_RATIO : CSS_ASPECT_RATIO_AUTO);
    if (_i == 0) {
        ck_assert_int_eq(width, 3 * (1 << CSS_RADIX_POINT));
        ck_assert_int_eq(height, 2 * (1 << CSS_RADIX_POINT));
    }
    css_computed_style_destroy(composed);
    destroy_select_ctx(&child_ctx);
    destroy_select_ctx(&parent_ctx);
}
END_TEST

/* Resolve through the actual stylesheet parser, cascade and layout helpers.
 * The current logical-property mapping targets horizontal-tb text. */
static const struct {
    const char *declarations;
    int padding[4], margin[4]; /* top, right, bottom, left */
} logical_spacing_cases[] = {
    {"padding-block:20px", {20, 0, 20, 0}, {0}},
    {"padding-block:10px 30px", {10, 0, 30, 0}, {0}},
    {"margin-block:12px", {0}, {12, 0, 12, 0}},
    {"margin-block:-4px 8px", {0}, {-4, 0, 8, 0}},
    {"margin-block:auto 10px", {0}, {AUTO, 0, 10, 0}},
    {"padding-block:5% 10%", {20, 0, 40, 0}, {0}},
    {"margin-block:5% 10%", {0}, {20, 0, 40, 0}},
    {"padding-block:1em 2em", {16, 0, 32, 0}, {0}},
    {"padding-block:calc(5% + 2px) calc(3px * 4)", {22, 0, 12, 0}, {0}},
    {"--spacing:4px;padding-block:calc(var(--spacing)*5)", {20, 0, 20, 0}, {0}},
    {"--space:6px 14px;margin-block:var(--space)", {0}, {6, 0, 14, 0}},
    {"padding-block:10px;padding-top:7px", {7, 0, 10, 0}, {0}},
    {"padding-top:7px;padding-block:10px", {10, 0, 10, 0}, {0}},
    {"padding-block:10px !important;padding-top:7px", {10, 0, 10, 0}, {0}},
    {"padding-top:7px !important;padding-block:10px", {7, 0, 10, 0}, {0}},
    {"padding-block:10px;padding-block-end:3px", {10, 0, 3, 0}, {0}},
    {"padding-block-start:5px;padding-block-end:calc(5px + 2px)", {5, 0, 7, 0}, {0}},
    {"margin-block-start:-5px;margin-block-end:auto", {0}, {-5, 0, AUTO, 0}},
    {"--s:2px;margin-block-start:calc(var(--s)*3);margin-bottom:9px", {0}, {6, 0, 9, 0}},
    {"padding:5px;padding-block:initial", {0, 5, 0, 5}, {0}},
    {"margin:5px;margin-block:unset", {0}, {0, 5, 0, 5}},
    {"padding-block:12px;padding-block:20px -2px", {12, 0, 12, 0}, {0}},
    {"padding-block:12px;padding-block:20px auto", {12, 0, 12, 0}, {0}},
    {"padding-block:12px;padding-block:20px initial", {12, 0, 12, 0}, {0}},
    {"padding-block:12px;padding-block:10px 20px 30px", {12, 0, 12, 0}, {0}},
    {"padding-block:12px;padding-block:inherit 20px", {12, 0, 12, 0}, {0}},
    {"padding-inline:12px;padding-inline:20px initial", {0, 12, 0, 12}, {0}},
    {"margin-inline:12px;margin-inline:20px inherit", {0}, {0, 12, 0, 12}},
    {"padding-inline:10px 30px !important;padding-left:5px", {0, 30, 0, 10}, {0}},
    {"margin-inline:auto 5px", {0}, {0, 5, 0, AUTO}},
    {"--x:20px; padding-block:var(--x) !important;padding-bottom:1px", {20, 0, 20, 0}, {0}},
    {"padding-block:10px;padding-block:var(--missing)", {0}, {0}},
    {"padding:5px;padding-block:revert", {0, 5, 0, 5}, {0}},
    {"padding-block-start:7px !important;padding-top:9px", {7, 0, 0, 0}, {0}},
    {"--pad:13px;padding-block-end:var(--pad)", {0, 0, 13, 0}, {0}},
    {"direction:rtl;padding-block:10px 20px;margin-block:3px 7px", {10, 0, 20, 0}, {3, 0, 7, 0}},
};

START_TEST(test_logical_spacing)
{
    select_ctx ctx;
    char css[350];
    snprintf(
        css, sizeof(css), "* { font-size:16px; padding:0; margin:0; %s; }", logical_spacing_cases[_i].declarations);
    const css_computed_style *style = select_style_from_css(css, &ctx);
    struct box box = {.type = BOX_BLOCK, .style = (css_computed_style *)style};
    layout_find_dimensions(
        &unit_ctx, 400, -1, &box, style, NULL, NULL, NULL, NULL, NULL, NULL, box.margin, box.padding, box.border);
    for (int side = 0; side < 4; side++) {
        ck_assert_int_eq(box.padding[side], logical_spacing_cases[_i].padding[side]);
        ck_assert_int_eq(box.margin[side], logical_spacing_cases[_i].margin[side]);
    }
    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_logical_spacing_inherit)
{
    select_ctx parent_ctx, child_ctx;
    const css_computed_style *parent = select_style_from_css(
        "* { padding:11px 12px 13px 14px; margin:1px 2px 3px 4px; }", &parent_ctx);
    const css_computed_style *child = select_style_from_css_node(
        "* { padding:5px; margin:6px; padding-block:inherit; margin-block:inherit; }", &child_ctx, false);
    css_computed_style *composed = NULL;
    ck_assert_int_eq(css_computed_style_compose(parent, child, &unit_ctx, &composed), CSS_OK);
    struct box box = {.type = BOX_BLOCK, .style = composed};
    layout_find_dimensions(
        &unit_ctx, 400, -1, &box, composed, NULL, NULL, NULL, NULL, NULL, NULL, box.margin, box.padding, box.border);
    ck_assert_int_eq(box.padding[TOP], 11);
    ck_assert_int_eq(box.padding[BOTTOM], 13);
    ck_assert_int_eq(box.padding[LEFT], 5);
    ck_assert_int_eq(box.margin[TOP], 1);
    ck_assert_int_eq(box.margin[BOTTOM], 3);
    ck_assert_int_eq(box.margin[RIGHT], 6);
    css_computed_style_destroy(composed);
    destroy_select_ctx(&child_ctx);
    destroy_select_ctx(&parent_ctx);
}
END_TEST

START_TEST(test_logical_spacing_specificity)
{
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css(
        "div { padding-top:7px; margin-bottom:9px; } * { padding-block:20px; margin-block:10px; }", &ctx);
    struct box box = {.type = BOX_BLOCK, .style = (css_computed_style *)style};
    layout_find_dimensions(
        &unit_ctx, 400, -1, &box, style, NULL, NULL, NULL, NULL, NULL, NULL, box.margin, box.padding, box.border);
    ck_assert_int_eq(box.padding[TOP], 7);
    ck_assert_int_eq(box.padding[BOTTOM], 20);
    ck_assert_int_eq(box.margin[TOP], 10);
    ck_assert_int_eq(box.margin[BOTTOM], 9);
    destroy_select_ctx(&ctx);
}
END_TEST

/* Parse real CSS, then resolve against a used gradient line length. */
static const struct {
    const char *css;
    unsigned int count;
    float positions[CSS_LINEAR_GRADIENT_MAX_STOPS];
} gradient_cases[] = {
    {"background-image:linear-gradient(red 0px,blue 70px)", 2, {0, .35f}},
    {"background:linear-gradient(180deg,transparent 5px,#2145ac 160px)!important;padding-left:7px", 2, {.025f, .8f}},
    {"--brand:#2145ac;--end:70px;background-image:linear-gradient(transparent 0px,var(--brand) var(--end))", 2,
        {0, .35f}},
    {"background-image:linear-gradient(red,blue)", 2, {0, 1}},
    {"background-image:linear-gradient(red 40px,green,white,blue 80%)", 4, {.2f, .4f, .6f, .8f}},
    {"background-image:linear-gradient(red 100px,green 20%,blue)", 3, {.5f, .5f, 1}},
    {"background-image:linear-gradient(red -1%,blue 150%)", 2, {-.01f, 1.5f}},
    {"background-image:linear-gradient(red -50px,green,blue 250px)", 3, {-.25f, .5f, 1.25f}},
    {"background-image:linear-gradient(red 250px,blue)", 2, {1.25f, 1.25f}},
    {"background-image:linear-gradient(red,blue -10px)", 2, {0, 0}},
    {"font-size:20px;background-image:linear-gradient(red 1em,blue 4em)", 2, {.1f, .4f}},
    {"background-image:linear-gradient(red 0,blue 75%)", 2, {0, .75f}},
    {"background-image:linear-gradient(red 0px,blue 70px)!important;background-image:linear-gradient(red,blue)", 2,
        {0, .35f}},
    {"background-image:linear-gradient(red 0px,blue 70px);--other:42px;width:var(--other)", 2, {0, .35f}},
    {"background-image:radial-gradient(circle,red 0px,blue 70px)!important", 2, {0, .35f}},
    {"background-image:linear-gradient(red,red,red,red,red,red,red,red,red,blue)", 10,
        {0, 1.f / 9, 2.f / 9, 3.f / 9, 4.f / 9, 5.f / 9, 6.f / 9, 7.f / 9, 8.f / 9, 1}},
};

START_TEST(test_gradient_stops)
{
    char css[1024];
    snprintf(css, sizeof(css), "* {%s}", gradient_cases[_i].css);
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css(css, &ctx);
    const css_linear_gradient *linear = css_computed_background_gradient(style);
    const css_radial_gradient *radial = css_computed_background_radial_gradient(style);
    const css_gradient_stop *stops = linear ? linear->stops : radial ? radial->stops : NULL;
    ck_assert_ptr_nonnull(stops);
    unsigned int count = linear ? linear->stop_count : radial->stop_count;
    ck_assert_uint_eq(count, gradient_cases[_i].count);
    float positions[CSS_LINEAR_GRADIENT_MAX_STOPS];
    html_gradient_positions(stops, count, style, &unit_ctx, 200, 1, positions);
    for (unsigned int i = 0; i < count; i++)
        ck_assert_float_eq_tol(positions[i], gradient_cases[_i].positions[i], .001f);
    /* Zoom changes both lengths and the drawing area, preserving proportions. */
    html_gradient_positions(stops, count, style, &unit_ctx, 400, 2, positions);
    for (unsigned int i = 0; i < count; i++)
        ck_assert_float_eq_tol(positions[i], gradient_cases[_i].positions[i], .001f);
    if (_i == 1 || _i == 2)
        ck_assert_uint_eq(stops[0].color, 0);
    destroy_select_ctx(&ctx);
}
END_TEST

static const char *invalid_gradients[] = {
    "linear-gradient(red)",
    "linear-gradient(red 3s,blue)",
    "linear-gradient(red 1fr,blue)",
    "linear-gradient(red 2,blue)",
    "linear-gradient(red 0px,blue,)",
    "linear-gradient(red 0px,blue 70px) junk",
    "linear-gradient(red 0px blue 70px)",
    "linear-gradient(red,red,red,red,red,red,red,red,red,red,blue)",
    "linear-gradient(red,red,red,red,red,red,red,red,red,blue",
    "radial-gradient(red)",
};
START_TEST(test_invalid_gradient)
{
    char css[1024];
    snprintf(
        css, sizeof(css), "* {background-image:linear-gradient(red,blue);background-image:%s}", invalid_gradients[_i]);
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css(css, &ctx);
    const css_linear_gradient *g = css_computed_background_gradient(style);
    ck_assert_ptr_nonnull(g);
    ck_assert_uint_eq(g->stop_count, 2);
    ck_assert(g->stops[0].auto_offset && g->stops[1].auto_offset);
    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_gradient_resize_and_color)
{
    select_ctx ctx;
    const css_computed_style *style = select_style_from_css(
        "* {background-image:linear-gradient(transparent 40px,#0000ff 50%)}", &ctx);
    const css_linear_gradient *g = css_computed_background_gradient(style);
    ck_assert_ptr_nonnull(g);
    float positions[2];
    html_gradient_positions(g->stops, 2, style, &unit_ctx, 200, 1, positions);
    ck_assert_float_eq_tol(positions[0], .2f, .001f);
    ck_assert_float_eq_tol(positions[1], .5f, .001f);
    css_color midpoint = html_gradient_color_at(g->stops, positions, 2, .35f);
    ck_assert_uint_eq(midpoint & 0xffffff, 0x0000ff);
    ck_assert_int_le(abs((int)(midpoint >> 24) - 128), 1);
    html_gradient_positions(g->stops, 2, style, &unit_ctx, 50, 1, positions);
    ck_assert_float_eq_tol(positions[0], .8f, .001f);
    ck_assert_float_eq_tol(positions[1], .8f, .001f);
    ck_assert_uint_eq(html_gradient_color_at(g->stops, positions, 2, .7f), 0);
    ck_assert_uint_eq(html_gradient_color_at(g->stops, positions, 2, .8f), 0xff0000ff);
    destroy_select_ctx(&ctx);
}
END_TEST

START_TEST(test_gradient_inheritance)
{
    select_ctx parent_ctx, child_ctx;
    const css_computed_style *parent = select_style_from_css(
        "* {font-size:20px;background-image:linear-gradient(red 1em,blue 4em)}", &parent_ctx);
    const css_computed_style *child = select_style_from_css_node(
        "* {font-size:40px;background-image:inherit}", &child_ctx, false);
    css_computed_style *composed = NULL;
    ck_assert_int_eq(css_computed_style_compose(parent, child, &unit_ctx, &composed), CSS_OK);
    const css_linear_gradient *g = css_computed_background_gradient(composed);
    ck_assert_ptr_nonnull(g);
    ck_assert_int_eq(g->stops[0].unit, CSS_UNIT_PX);
    ck_assert_int_eq(g->stops[0].offset, INTTOFIX(20));
    ck_assert_int_eq(g->stops[1].offset, INTTOFIX(80));
    css_computed_style_destroy(composed);
    destroy_select_ctx(&child_ctx);
    destroy_select_ctx(&parent_ctx);
}
END_TEST

START_TEST(test_gradient_style_sharing)
{
    select_ctx contexts[3];
    const css_computed_style *styles[3] = {
        select_style_from_css("* {background-image:linear-gradient(red 0px,blue 50px)}", &contexts[0]),
        select_style_from_css("* {background-image:linear-gradient(red 0px,blue 50%)}", &contexts[1]),
        select_style_from_css("* {background-image:linear-gradient(red 0px,blue)}", &contexts[2]),
    };
    for (int i = 0; i < 3; i++) {
        const css_linear_gradient *g = css_computed_background_gradient(styles[i]);
        ck_assert_ptr_nonnull(g);
        ck_assert_int_eq(g->stops[1].unit, i == 0 ? CSS_UNIT_PX : CSS_UNIT_PCT);
        ck_assert_int_eq(g->stops[1].auto_offset, i == 2);
    }
    for (int i = 0; i < 3; i++)
        destroy_select_ctx(&contexts[i]);
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
    tcase_add_test(tc_core, test_layout_find_dimensions_cq_units_use_box_container);
    tcase_add_test(tc_core, test_lh_length_to_px_cq_units_zero_container_resolve_to_zero);
    tcase_add_test(tc_core, test_lh_length_to_px_cq_units_unset_container_uses_viewport);
    tcase_add_test(tc_core, test_inset_shorthand_sets_absolute_offsets);
    tcase_add_test(tc_core, test_inset_inline_shorthand_sets_left_and_right);
    tcase_add_test(tc_core, test_inset_block_shorthand_sets_top_and_bottom);

    tcase_add_loop_test(tc_core, test_aspect_ratio_parse, 0, sizeof(ratio_cases) / sizeof(ratio_cases[0]));
    tcase_add_test(tc_core, test_aspect_ratio_dimensions);
    tcase_add_test(tc_core, test_aspect_ratio_height_constraints);
    tcase_add_loop_test(tc_core, test_aspect_ratio_compose, 0, 2);
    tcase_add_loop_test(tc_core, test_aspect_ratio_content_minimum, 0, 3);
    tcase_add_loop_test(
        tc_core, test_logical_spacing, 0, sizeof(logical_spacing_cases) / sizeof(logical_spacing_cases[0]));
    tcase_add_test(tc_core, test_logical_spacing_inherit);
    tcase_add_test(tc_core, test_logical_spacing_specificity);
    tcase_add_loop_test(tc_core, test_gradient_stops, 0, sizeof(gradient_cases) / sizeof(gradient_cases[0]));
    tcase_add_loop_test(tc_core, test_invalid_gradient, 0, sizeof(invalid_gradients) / sizeof(invalid_gradients[0]));
    tcase_add_test(tc_core, test_gradient_resize_and_color);
    tcase_add_test(tc_core, test_gradient_inheritance);
    tcase_add_test(tc_core, test_gradient_style_sharing);
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
