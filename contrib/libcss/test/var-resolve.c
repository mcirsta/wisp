/*
 * Regression tests for var() resolution timing during selection.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libwapcaplet/libwapcaplet.h>

#include <libcss/computed.h>
#include <libcss/libcss.h>
#include <libcss/select.h>

#include "testutils.h"
#include "select/select.h"
#include "select/computed.h"
#include "stylesheet.h"
#include <nsutils/time.h>

typedef struct test_node {
    lwc_string *name;
    struct test_node *parent;
    struct test_node *previous;
    void *libcss_node_data;
    lwc_string **classes;
    uint32_t n_classes;
} test_node;

typedef struct attribute_checks {
    unsigned ordinary, deferred, custom;
} attribute_checks;

static bool test_node_name_matches(test_node *n, const css_qname *qname)
{
    bool match = false;

    if (n == NULL)
        return false;

    if (lwc_string_length(qname->name) == 1 &&
            lwc_string_data(qname->name)[0] == '*') {
        return true;
    }

    assert(lwc_string_caseless_isequal(qname->name, n->name,
        &match) == lwc_error_ok);

    return match;
}

static css_error resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    UNUSED(pw);
    UNUSED(base);

    *abs = lwc_string_ref(rel);

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
    test_node *n = node;
    UNUSED(pw);

    *classes = n->classes;
    *n_classes = n->n_classes;
    for (uint32_t i = 0; i < n->n_classes; i++)
        lwc_string_ref(n->classes[i]);

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
    test_node *n = node;
    UNUSED(pw);

    *ancestor = NULL;
    for (n = n->parent; n != NULL; n = n->parent) {
        if (test_node_name_matches(n, qname)) {
            *ancestor = n;
            break;
        }
    }

    return CSS_OK;
}

static css_error named_parent_node(void *pw, void *node, const css_qname *qname, void **parent)
{
    test_node *n = node;
    UNUSED(pw);

    *parent = test_node_name_matches(n->parent, qname) ? n->parent : NULL;

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
    test_node *n = node;
    UNUSED(pw);
    *sibling = NULL;
    for (n = n->previous; n != NULL; n = n->previous) {
        if (test_node_name_matches(n, qname)) {
            *sibling = n;
            break;
        }
    }
    return CSS_OK;
}

static css_error parent_node(void *pw, void *node, void **parent)
{
    test_node *n = node;
    UNUSED(pw);

    *parent = n->parent;

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

    if (lwc_string_length(qname->name) == 1 &&
            lwc_string_data(qname->name)[0] == '*') {
        *match = true;
    } else {
        assert(lwc_string_caseless_isequal(qname->name, n->name, match) == lwc_error_ok);
    }

    return CSS_OK;
}

static css_error node_has_class(void *pw, void *node, lwc_string *name, bool *match)
{
    test_node *n = node;
    UNUSED(pw);

    *match = false;
    for (uint32_t i = 0; i < n->n_classes && !*match; i++)
        assert(lwc_string_isequal(n->classes[i], name, match) == lwc_error_ok);

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
    UNUSED(node);

    if (pw != NULL) {
        attribute_checks *checks = pw;
        const char *name = lwc_string_data(qname->name);
        if (strcmp(name, "ordinary") == 0)
            checks->ordinary++;
        else if (strcmp(name, "deferred") == 0)
            checks->deferred++;
        else if (strcmp(name, "custom") == 0)
            checks->custom++;
    }

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_equal(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_dashmatch(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_includes(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_prefix(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_suffix(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_substring(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
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
    test_node *n = node;
    UNUSED(pw);

    *match = (n->parent == NULL);

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

typedef struct style_case {
    css_stylesheet *sheet;
    css_stylesheet *import_sheet;
    css_stylesheet *inline_style;
    css_select_ctx *select_ctx;
    css_select_results *results;
    css_select_results *parent_results;
    lwc_string *node_name;
    lwc_string *parent_name;
    test_node node;
    test_node parent_node;
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

static css_stylesheet *create_stylesheet_from_css(
    const char *css,
    bool inline_style,
    bool allow_pending_imports)
{
    css_stylesheet_params params = {
        .params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_3,
        .charset = "UTF-8",
        .url = "var_resolve",
        .title = "var_resolve",
        .allow_quirks = false,
        .inline_style = inline_style,
        .resolve = resolve_url,
        .resolve_pw = NULL,
        .import = NULL,
        .import_pw = NULL,
        .font_face = NULL,
        .font_face_pw = NULL,
        .color = NULL,
        .color_pw = NULL,
        .font = NULL,
        .font_pw = NULL,
        .error = NULL,
        .error_pw = NULL,
    };
    css_stylesheet *sheet;
    css_error err;

    assert(css_stylesheet_create(&params, &sheet) == CSS_OK);
    err = css_stylesheet_append_data(sheet, (const uint8_t *)css, strlen(css));
    assert(err == CSS_OK || err == CSS_NEEDDATA);
    err = css_stylesheet_data_done(sheet);
    assert(err == CSS_OK || (allow_pending_imports && err == CSS_IMPORTS_PENDING));

    return sheet;
}

static void destroy_node_data(test_node *node)
{
    if (node->libcss_node_data != NULL) {
        assert(css_libcss_node_data_handler(&select_handler,
            CSS_NODE_DELETED, NULL, node, NULL,
            node->libcss_node_data) == CSS_OK);
        node->libcss_node_data = NULL;
    }
}

static const css_computed_style *select_style_for_node(
    style_case *ctx,
    test_node *node,
    const css_stylesheet *inline_style,
    css_select_results **results)
{
    css_media media = {.type = CSS_MEDIA_ALL};

    assert(css_select_style(ctx->select_ctx, node, &unit_ctx, &media,
            inline_style, &select_handler, NULL, results) == CSS_OK);

    assert(*results != NULL);
    assert((*results)->styles[CSS_PSEUDO_ELEMENT_NONE] != NULL);

    return (*results)->styles[CSS_PSEUDO_ELEMENT_NONE];
}

static void setup_style_case(style_case *ctx, const char *css)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->sheet = create_stylesheet_from_css(css, false, false);

    assert(css_select_ctx_create(&ctx->select_ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx->select_ctx, ctx->sheet, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);

    assert(lwc_intern_string("div", 3, &ctx->node_name) == lwc_error_ok);
    ctx->node.name = ctx->node_name;
    ctx->node.parent = NULL;
    ctx->node.libcss_node_data = NULL;
}

static const css_computed_style *select_style_from_css(const char *css, style_case *ctx)
{
    setup_style_case(ctx, css);

    return select_style_for_node(ctx, &ctx->node, NULL, &ctx->results);
}

static void destroy_style_case(style_case *ctx)
{
    destroy_node_data(&ctx->node);
    destroy_node_data(&ctx->parent_node);
    if (ctx->results != NULL)
        css_select_results_destroy(ctx->results);
    if (ctx->parent_results != NULL)
        css_select_results_destroy(ctx->parent_results);
    if (ctx->select_ctx != NULL)
        css_select_ctx_destroy(ctx->select_ctx);
    if (ctx->inline_style != NULL)
        css_stylesheet_destroy(ctx->inline_style);
    if (ctx->sheet != NULL)
        css_stylesheet_destroy(ctx->sheet);
    if (ctx->import_sheet != NULL)
        css_stylesheet_destroy(ctx->import_sheet);
    if (ctx->node_name != NULL)
        lwc_string_unref(ctx->node_name);
    if (ctx->parent_name != NULL)
        lwc_string_unref(ctx->parent_name);
}

typedef uint8_t (*length_getter)(
    const css_computed_style *style,
    css_fixed_or_calc *length,
    css_unit *unit);

static void expect_length_px(const char *label,
    const css_computed_style *style,
    length_getter getter,
    uint8_t expected_type,
    int expected_px)
{
    css_fixed_or_calc length = {.value = 0};
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = getter(style, &length, &unit);
    int px = -1;

    if (type != expected_type) {
        printf("FAIL - %s type mismatch: got=%u expected=%u\n",
            label, type, expected_type);
        exit(EXIT_FAILURE);
    }

    if (css_computed_length_to_px(style, &unit_ctx, 200, length, unit, &px) != CSS_OK) {
        printf("FAIL - %s conversion failed: unit=%d\n", label, unit);
        exit(EXIT_FAILURE);
    }

    if (px != expected_px) {
        printf("FAIL - %s px mismatch: got=%d expected=%d\n",
            label, px, expected_px);
        exit(EXIT_FAILURE);
    }
}

static void expect_length_type(const char *label,
    const css_computed_style *style,
    length_getter getter,
    uint8_t expected_type)
{
    css_fixed_or_calc length = {.value = 0};
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = getter(style, &length, &unit);

    if (type != expected_type) {
        printf("FAIL - %s type mismatch: got=%u expected=%u\n",
            label, type, expected_type);
        exit(EXIT_FAILURE);
    }
}

static void test_same_rule_custom_property_after_use(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("div { width: var(--w); --w: 12px; }", &ctx);

    expect_length_px("same-rule width", style,
        css_computed_width, CSS_WIDTH_SET, 12);

    destroy_style_case(&ctx);
}

static void test_identifier_custom_property_names(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --9:12px; \\2d -Tone:13px; --tone:14px; --:99px;"
        "width:var(--9); height:var(--Tone); margin-left:var(--tone);"
        "padding-left:var(\\2d -missing, 15px); }", &ctx);
    expect_length_px("numeric dashed name", style, css_computed_width, CSS_WIDTH_SET, 12);
    expect_length_px("escaped declaration name", style, css_computed_height, CSS_HEIGHT_SET, 13);
    expect_length_px("case-sensitive name", style, css_computed_margin_left, CSS_MARGIN_SET, 14);
    expect_length_px("escaped fallback reference", style, css_computed_padding_left, CSS_PADDING_SET, 15);
    destroy_style_case(&ctx);
}

static void test_later_rule_custom_property_after_use(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("div { width: var(--w); } div { --w: 13px; }", &ctx);

    expect_length_px("later-rule width", style,
        css_computed_width, CSS_WIDTH_SET, 13);

    destroy_style_case(&ctx);
}

static void test_nested_fallback_uses_final_context(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("div { width: var(--missing, var(--w)); --w: 14px; }", &ctx);

    expect_length_px("nested fallback width", style,
        css_computed_width, CSS_WIDTH_SET, 14);

    destroy_style_case(&ctx);
}

static void test_var_shorthand_keeps_declaration_order(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { margin: var(--m); margin-left: 5px; --m: 10px; }", &ctx);

    expect_length_px("margin top", style,
        css_computed_margin_top, CSS_MARGIN_SET, 10);
    expect_length_px("margin right", style,
        css_computed_margin_right, CSS_MARGIN_SET, 10);
    expect_length_px("margin bottom", style,
        css_computed_margin_bottom, CSS_MARGIN_SET, 10);
    expect_length_px("margin left", style,
        css_computed_margin_left, CSS_MARGIN_SET, 5);

    destroy_style_case(&ctx);
}

static void test_reused_custom_property_expansion(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --size: 36px; width: var(--size); height: var(--size); }", &ctx);

    expect_length_px("reused variable width", style,
        css_computed_width, CSS_WIDTH_SET, 36);
    expect_length_px("reused variable height", style,
        css_computed_height, CSS_HEIGHT_SET, 36);

    destroy_style_case(&ctx);
}

static void test_important_custom_property_wins(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --w: 12px !important; }"
        "div { width: var(--w); --w: 13px; }", &ctx);

    expect_length_px("important custom property width", style,
        css_computed_width, CSS_WIDTH_SET, 12);

    destroy_style_case(&ctx);
}

static void test_invalid_var_overrides_earlier_width(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("div { width: 7px; width: var(--missing); }", &ctx);

    expect_length_type("invalid var width", style,
        css_computed_width, CSS_WIDTH_AUTO);

    destroy_style_case(&ctx);
}

static void test_custom_property_inherits_from_parent_node(void)
{
    style_case ctx;
    const css_computed_style *style;

    setup_style_case(&ctx,
        ":root { --w: 21px; }"
        "div { width: var(--w); }");

    assert(lwc_intern_string("html", 4, &ctx.parent_name) == lwc_error_ok);
    ctx.parent_node.name = ctx.parent_name;
    ctx.parent_node.parent = NULL;
    ctx.parent_node.libcss_node_data = NULL;

    select_style_for_node(&ctx, &ctx.parent_node, NULL, &ctx.parent_results);

    ctx.node.parent = &ctx.parent_node;
    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);

    expect_length_px("inherited custom property width", style,
        css_computed_width, CSS_WIDTH_SET, 21);

    destroy_style_case(&ctx);
}

static void test_inherited_custom_property_keeps_parent_value(void)
{
    style_case ctx;
    test_node plain_node = {0};
    test_node shadow_node = {0};
    css_select_results *plain_results = NULL;
    css_select_results *shadow_results = NULL;
    lwc_string *plain_name = NULL;
    lwc_string *shadow_name = NULL;
    const css_computed_style *style;

    setup_style_case(&ctx,
        ":root { --base: 40px; --w: var(--base); }"
        "div { width: var(--w); }"
        "section { --base: 41px; width: var(--w); }");

    assert(lwc_intern_string("html", 4, &ctx.parent_name) == lwc_error_ok);
    ctx.parent_node.name = ctx.parent_name;
    ctx.parent_node.parent = NULL;
    ctx.parent_node.libcss_node_data = NULL;

    select_style_for_node(&ctx, &ctx.parent_node, NULL, &ctx.parent_results);

    ctx.node.parent = &ctx.parent_node;
    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_px("inherited variable without shadow", style,
        css_computed_width, CSS_WIDTH_SET, 40);

    assert(lwc_intern_string("section", 7, &shadow_name) == lwc_error_ok);
    shadow_node.name = shadow_name;
    shadow_node.parent = &ctx.parent_node;
    shadow_node.libcss_node_data = NULL;

    style = select_style_for_node(&ctx, &shadow_node, NULL, &shadow_results);
    expect_length_px("inherited variable with child shadow", style,
        css_computed_width, CSS_WIDTH_SET, 40);

    assert(lwc_intern_string("div", 3, &plain_name) == lwc_error_ok);
    plain_node.name = plain_name;
    plain_node.parent = &ctx.parent_node;
    plain_node.libcss_node_data = NULL;

    style = select_style_for_node(&ctx, &plain_node, NULL, &plain_results);
    expect_length_px("inherited variable after child shadow", style,
        css_computed_width, CSS_WIDTH_SET, 40);

    destroy_node_data(&plain_node);
    destroy_node_data(&shadow_node);
    if (plain_results != NULL)
        css_select_results_destroy(plain_results);
    if (shadow_results != NULL)
        css_select_results_destroy(shadow_results);
    lwc_string_unref(plain_name);
    lwc_string_unref(shadow_name);
    destroy_style_case(&ctx);
}

static void test_inline_style_var_resolution(void)
{
    style_case ctx;
    const css_computed_style *style;

    setup_style_case(&ctx, "div { width: 5px; }");
    ctx.inline_style = create_stylesheet_from_css(
        "width: var(--w); --w: 22px;", true, false);

    style = select_style_for_node(&ctx, &ctx.node,
        ctx.inline_style, &ctx.results);

    expect_length_px("inline style width", style,
        css_computed_width, CSS_WIDTH_SET, 22);

    destroy_style_case(&ctx);
}

static void test_imported_sheet_custom_property(void)
{
    style_case ctx;
    lwc_string *url = NULL;
    const css_computed_style *style;

    memset(&ctx, 0, sizeof(ctx));

    ctx.sheet = create_stylesheet_from_css(
        "@import url(\"vars.css\");"
        "div { width: var(--w); }", false, true);
    ctx.import_sheet = create_stylesheet_from_css(
        "div { --w: 23px; }", false, false);

    assert(css_stylesheet_next_pending_import(ctx.sheet, &url) == CSS_OK);
    lwc_string_unref(url);
    assert(css_stylesheet_register_import(ctx.sheet, ctx.import_sheet) == CSS_OK);

    assert(css_select_ctx_create(&ctx.select_ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx.select_ctx, ctx.sheet,
        CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);

    assert(lwc_intern_string("div", 3, &ctx.node_name) == lwc_error_ok);
    ctx.node.name = ctx.node_name;
    ctx.node.parent = NULL;
    ctx.node.libcss_node_data = NULL;

    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);

    expect_length_px("imported custom property width", style,
        css_computed_width, CSS_WIDTH_SET, 23);

    destroy_style_case(&ctx);
}

static void test_variables_imported_by_literal_sheet(void)
{
    style_case ctx = {0};
    lwc_string *url = NULL;

    /* The outer sheet contributes only ordinary declarations. Skipping its
     * variable-collection work must still visit imports and later cascade it. */
    ctx.sheet = create_stylesheet_from_css(
        "@import url(\"vars.css\"); div { height: 31px; }", false, true);
    ctx.import_sheet = create_stylesheet_from_css(
        "div { --w: 23px; width: var(--w); height: 19px; }"
        "div::before { width: var(--local); height: var(--w); }"
        "div::before { --local: 29px; }", false, false);
    assert(!ctx.sheet->uses_variables);
    assert(css_stylesheet_next_pending_import(ctx.sheet, &url) == CSS_OK);
    lwc_string_unref(url);
    assert(css_stylesheet_register_import(ctx.sheet, ctx.import_sheet) == CSS_OK);
    assert(css_select_ctx_create(&ctx.select_ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx.select_ctx, ctx.sheet,
        CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);
    assert(lwc_intern_string("div", 3, &ctx.node_name) == lwc_error_ok);
    ctx.node.name = ctx.node_name;

    const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_px("variable imported by literal sheet", style,
        css_computed_width, CSS_WIDTH_SET, 23);
    expect_length_px("literal outer declaration still cascades last", style,
        css_computed_height, CSS_HEIGHT_SET, 31);
    assert(ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE] != NULL);
    expect_length_px("imported pseudo defers local variables",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE], css_computed_width, CSS_WIDTH_SET, 29);
    expect_length_px("imported pseudo inherits variables",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE], css_computed_height, CSS_HEIGHT_SET, 23);
    destroy_style_case(&ctx);
}

static void test_variable_import_prefix(void)
{
    style_case ctx = {0};
    lwc_string *url = NULL;

    /* A variable-free first import must not hide a later import's descendants.
     * Charset rules are part of the prefix; imports after style rules are not. */
    ctx.sheet = create_stylesheet_from_css(
        "@charset \"UTF-8\"; @import 'empty.css'; @import 'bridge.css';"
        "div { height: 31px; } @import 'ignored.css';", false, true);
    css_stylesheet *empty = create_stylesheet_from_css("", false, false);
    css_stylesheet *bridge = create_stylesheet_from_css(
        "@charset \"UTF-8\"; @import 'vars.css'; div { height: 27px; }", false, true);
    ctx.import_sheet = create_stylesheet_from_css(
        "div { --w: 23px; width: var(--w); height: 19px; }"
        "div::before { width: var(--w); }", false, false);
    assert(!ctx.sheet->uses_variables && !bridge->uses_variables && !empty->uses_variables);
    assert(ctx.import_sheet->uses_variables);
    assert(css_stylesheet_register_import(ctx.sheet, empty) == CSS_OK);
    assert(css_stylesheet_register_import(ctx.sheet, bridge) == CSS_OK);
    assert(css_stylesheet_next_pending_import(ctx.sheet, &url) == CSS_INVALID);
    assert(css_select_ctx_create(&ctx.select_ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx.select_ctx, ctx.sheet, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);
    assert(lwc_intern_string("div", 3, &ctx.node_name) == lwc_error_ok);
    ctx.node.name = ctx.node_name;

    const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_type("unresolved nested import has no width", style, css_computed_width, CSS_WIDTH_AUTO);
    expect_length_px("outer declaration before import arrives", style,
        css_computed_height, CSS_HEIGHT_SET, 31);
    destroy_node_data(&ctx.node);
    css_select_results_destroy(ctx.results);
    ctx.results = NULL;

    /* No cached negative result: the same context must see a newly attached
     * nested import when the caller requests fresh style selection. */
    assert(css_stylesheet_next_pending_import(bridge, &url) == CSS_OK);
    lwc_string_unref(url);
    assert(css_stylesheet_register_import(bridge, ctx.import_sheet) == CSS_OK);
    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_px("variable through second and nested imports", style,
        css_computed_width, CSS_WIDTH_SET, 23);
    expect_length_px("outer declaration after import arrives", style,
        css_computed_height, CSS_HEIGHT_SET, 31);
    assert(ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE] != NULL);
    expect_length_px("nested import pseudo inherits variables",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE], css_computed_width, CSS_WIDTH_SET, 23);

    destroy_style_case(&ctx);
    assert(css_stylesheet_destroy(bridge) == CSS_OK);
    assert(css_stylesheet_destroy(empty) == CSS_OK);
}

static void test_unmatched_sheet_import(void)
{
    for (unsigned variables = 0; variables < 2; variables++) {
        style_case ctx = {0};
        /* The outer sheet cannot match div. Its late import still can, in
         * both the ordinary-only pass and the custom-property collection pass. */
        ctx.sheet = create_stylesheet_from_css(
            "@import 'late.css'; span {width:99px}", false, true);
        ctx.import_sheet = create_stylesheet_from_css(variables ?
            "div {width:var(--w); --w:23px} div::before {width:var(--w)}" :
            "div {width:23px} div::before {width:23px}", false, false);
        assert(css_select_ctx_create(&ctx.select_ctx) == CSS_OK);
        assert(css_select_ctx_append_sheet(ctx.select_ctx, ctx.sheet, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);
        assert(lwc_intern_string("div", 3, &ctx.node_name) == lwc_error_ok);
        ctx.node.name = ctx.node_name;

        const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
        expect_length_type("unmatched sheet before import", style, css_computed_width, CSS_WIDTH_AUTO);
        destroy_node_data(&ctx.node);
        css_select_results_destroy(ctx.results);
        ctx.results = NULL;

        assert(css_stylesheet_register_import(ctx.sheet, ctx.import_sheet) == CSS_OK);
        style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
        expect_length_px("late import through unmatched sheet", style, css_computed_width, CSS_WIDTH_SET, 23);
        assert(ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE] != NULL);
        expect_length_px("late imported pseudo through unmatched sheet",
            ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE], css_computed_width, CSS_WIDTH_SET, 23);
        destroy_style_case(&ctx);
    }
}

static css_error fail_class_match(void *pw, void *node, lwc_string *name, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(name);
    UNUSED(match);
    return CSS_NOMEM;
}

static void test_many_class_selectors(void)
{
    const uint32_t counts[] = {0, 1, 8, 9, 16, 17, 40};
    lwc_string *names[40], *classes[40];
    char css[4096] = "div { width: var(--w, 7px); height: 6px; }";
    size_t used = strlen(css);
    css_media media = {.type = CSS_MEDIA_ALL};
    css_select_handler failing_handler = select_handler;
    failing_handler.node_has_class = fail_class_match;

    for (unsigned i = 0; i < 40; i++) {
        char name[16];
        snprintf(name, sizeof(name), "c%u", i);
        assert(lwc_intern_string(name, strlen(name), &names[i]) == lwc_error_ok);
        int written = snprintf(css + used, sizeof(css) - used,
            ".%s { --w: %upx; height: %upx; }", name, 10 + i, 100 + i);
        assert(written > 0 && (size_t)written < sizeof(css) - used);
        used += written;
    }
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        style_case ctx;
        setup_style_case(&ctx, css);
        ctx.node.n_classes = counts[i];
        ctx.node.classes = counts[i] != 0 ? classes : NULL;
        for (unsigned reversed = 0; reversed < 2; reversed++) {
            for (uint32_t j = 0; j < counts[i]; j++)
                classes[j] = names[reversed ? counts[i] - j - 1 : j];

            /* Exercise cleanup after full matching fails, for both short
             * class lists and lists large enough to require heap scratch. */
            if (counts[i] != 0) {
                assert(css_select_style(ctx.select_ctx, &ctx.node, &unit_ctx, &media,
                    NULL, &failing_handler, NULL, &ctx.results) == CSS_NOMEM);
                assert(ctx.results == NULL);
            }
            const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
            char label[80];
            snprintf(label, sizeof(label), "class count=%u reversed=%u variable cascade", counts[i], reversed);
            expect_length_px(label, style, css_computed_width, CSS_WIDTH_SET, counts[i] ? 9 + counts[i] : 7);
            snprintf(label, sizeof(label), "class count=%u reversed=%u ordinary cascade", counts[i], reversed);
            expect_length_px(label, style, css_computed_height, CSS_HEIGHT_SET, counts[i] ? 99 + counts[i] : 6);
            destroy_node_data(&ctx.node);
            css_select_results_destroy(ctx.results);
            ctx.results = NULL;
        }
        destroy_style_case(&ctx);
    }
    for (unsigned i = 0; i < 40; i++)
        lwc_string_unref(names[i]);
}

static void test_collection_skips_ordinary_rules(void)
{
    style_case ctx;
    attribute_checks checks = {0};
    css_media media = {.type = CSS_MEDIA_ALL};

    setup_style_case(&ctx,
        "div[ordinary] { width: 99px; }"
        "div[deferred] { width: var(--w); }"
        "div[custom] { --w: 99px; }"
        "@media all { div { --w: 7px; width: var(--w); } }"
        "div::before { width: var(--w); }");
    assert(css_select_style(ctx.select_ctx, &ctx.node, &unit_ctx, &media,
        NULL, &select_handler, &checks, &ctx.results) == CSS_OK);

    /* Attribute selectors do not match this node. Custom-property rules must
     * still be tested in both passes, while ordinary/deferred rules need only
     * the normal pass. This counts real matching work, not just stored flags. */
    if (checks.ordinary != 1 || checks.deferred != 1 || checks.custom != 2) {
        fprintf(stderr, "Attribute checks: ordinary=%u deferred=%u custom=%u (expected 1/1/2)\n",
            checks.ordinary, checks.deferred, checks.custom);
        abort();
    }
    expect_length_px("element still resolves variables", ctx.results->styles[CSS_PSEUDO_ELEMENT_NONE],
        css_computed_width, CSS_WIDTH_SET, 7);
    assert(ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE] != NULL);
    expect_length_px("pseudo created during normal cascade still inherits variables",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE], css_computed_width, CSS_WIDTH_SET, 7);
    destroy_style_case(&ctx);
}

static void test_pseudo_element_uses_element_vars(void)
{
    style_case ctx;

    select_style_from_css(
        "div { --w: 24px; }"
        "div::before { width: var(--w); }", &ctx);

    assert(ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE] != NULL);
    expect_length_px("before pseudo width",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE],
        css_computed_width, CSS_WIDTH_SET, 24);

    destroy_style_case(&ctx);
}

static void test_literal_fallback_value(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("div { width: var(--missing, 25px); }", &ctx);

    expect_length_px("literal fallback width", style,
        css_computed_width, CSS_WIDTH_SET, 25);

    destroy_style_case(&ctx);
}

static void test_shorthand_fallback_value(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { padding: var(--missing, 6px 7px); }", &ctx);

    expect_length_px("padding fallback top", style,
        css_computed_padding_top, CSS_PADDING_SET, 6);
    expect_length_px("padding fallback right", style,
        css_computed_padding_right, CSS_PADDING_SET, 7);
    expect_length_px("padding fallback bottom", style,
        css_computed_padding_bottom, CSS_PADDING_SET, 6);
    expect_length_px("padding fallback left", style,
        css_computed_padding_left, CSS_PADDING_SET, 7);

    destroy_style_case(&ctx);
}

static void test_variable_cycle_is_invalid(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b); --b: var(--a); width: var(--a); }", &ctx);

    expect_length_type("cycle width", style,
        css_computed_width, CSS_WIDTH_AUTO);

    destroy_style_case(&ctx);
}

static void test_cycle_uses_property_fallback(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b); --b: var(--a); width: var(--a, 31px); }", &ctx);

    expect_length_px("cycle property fallback width", style,
        css_computed_width, CSS_WIDTH_SET, 31);

    destroy_style_case(&ctx);
}

static void test_cyclic_var_fallback_does_not_poison_later_use(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b); --b: var(--a);"
        " width: var(--a, 42px); height: var(--a); }", &ctx);

    expect_length_px("cycle fallback width", style,
        css_computed_width, CSS_WIDTH_SET, 42);
    expect_length_type("cycle later height", style,
        css_computed_height, CSS_HEIGHT_AUTO);

    destroy_style_case(&ctx);
}

static void test_custom_property_fallback_cycle_is_invalid(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b, 32px); --b: var(--a); width: var(--a); }", &ctx);

    expect_length_type("custom property fallback cycle width", style,
        css_computed_width, CSS_WIDTH_AUTO);

    destroy_style_case(&ctx);
}

static void test_noncyclic_var_can_fallback_from_cycle(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b); --b: var(--a);"
        " --c: var(--a, 33px); width: var(--c); }", &ctx);

    expect_length_px("noncyclic fallback from cycle width", style,
        css_computed_width, CSS_WIDTH_SET, 33);

    destroy_style_case(&ctx);
}

static void test_self_reference_in_fallback_is_cycle(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--missing, var(--a)); width: var(--a, 34px); }", &ctx);

    expect_length_px("self fallback cycle width", style,
        css_computed_width, CSS_WIDTH_SET, 34);

    destroy_style_case(&ctx);
}

static void test_inherited_invalid_value_stays_invalid(void)
{
    style_case ctx;
    const css_computed_style *style;

    setup_style_case(&ctx,
        ":root { --b: var(--a); }"
        "div { --a: var(--b); width: var(--a, 35px); }");

    assert(lwc_intern_string("html", 4, &ctx.parent_name) == lwc_error_ok);
    ctx.parent_node.name = ctx.parent_name;
    ctx.parent_node.parent = NULL;
    ctx.parent_node.libcss_node_data = NULL;

    select_style_for_node(&ctx, &ctx.parent_node, NULL, &ctx.parent_results);

    ctx.node.parent = &ctx.parent_node;
    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);

    expect_length_px("inherited invalid value fallback width", style,
        css_computed_width, CSS_WIDTH_SET, 35);

    destroy_style_case(&ctx);
}

static void attach_parent(style_case *ctx)
{
    assert(lwc_intern_string("html", 4, &ctx->parent_name) == lwc_error_ok);
    ctx->parent_node.name = ctx->parent_name;
    select_style_for_node(ctx, &ctx->parent_node, NULL, &ctx->parent_results);
    ctx->node.parent = &ctx->parent_node;
}

static void test_cross_generation_dependencies(void)
{
    style_case ctx;
    setup_style_case(&ctx,
        "html { --a: 17px; --b: var(--a); --bad: var(--missing); }"
        "div { --a: var(--b); --missing: 20px; width: var(--a); height: var(--bad, 19px); }");
    attach_parent(&ctx);
    const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_px("cross generation is not a cycle", style, css_computed_width, CSS_WIDTH_SET, 17);
    expect_length_px("child cannot repair parent", style, css_computed_height, CSS_HEIGHT_SET, 19);
    destroy_style_case(&ctx);
}

static void test_pseudo_local_scopes(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --w: 10px; --alias: var(--w); width: var(--w); }"
        "div::before { --w: 20px; width: var(--w); height: var(--alias); }"
        "div::after { --w: 30px; width: var(--w); }", &ctx);
    expect_length_px("pseudo does not change base", style, css_computed_width, CSS_WIDTH_SET, 10);
    expect_length_px("before local width", ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE],
        css_computed_width, CSS_WIDTH_SET, 20);
    expect_length_px("before inherits completed alias", ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE],
        css_computed_height, CSS_HEIGHT_SET, 10);
    expect_length_px("after independent width", ctx.results->styles[CSS_PSEUDO_ELEMENT_AFTER],
        css_computed_width, CSS_WIDTH_SET, 30);
    test_node child = {.name = ctx.node_name, .parent = &ctx.node};
    css_select_results *child_results;
    css_stylesheet *inline_style = create_stylesheet_from_css("--w: inherit; width: var(--w)", true, false);
    style = select_style_for_node(&ctx, &child, inline_style, &child_results);
    expect_length_px("child inherits base rather than pseudo", style, css_computed_width, CSS_WIDTH_SET, 10);
    destroy_node_data(&child);
    css_select_results_destroy(child_results);
    css_stylesheet_destroy(inline_style);
    destroy_style_case(&ctx);
}

static void test_deferred_pseudo_variable_cascade(void)
{
    style_case ctx;
    select_style_from_css(
        "div { --w: 10px; --alias: var(--w); }"
        "div::before { width: var(--w); height: var(--alias); margin-left: var(--w); }"
        "::before { --w: 20px !important; width: 99px; }"
        "div::before { --w: 30px !important; --alias: inherit; }"
        "div::after { width: var(--w, 41px); height: var(--alias); --w: initial; }"
        "div::backdrop { width: var(--w); --w: 50px; }", &ctx);
    const css_computed_style *before = ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE];
    expect_length_px("deferred declarations preserve order and consumer specificity",
        before, css_computed_width, CSS_WIDTH_SET, 30);
    expect_length_px("deferred pseudo inherits completed element alias",
        before, css_computed_height, CSS_HEIGHT_SET, 10);
    expect_length_px("second use reuses completed pseudo variables",
        before, css_computed_margin_left, CSS_MARGIN_SET, 30);
    expect_length_px("after has its own invalid local binding",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_AFTER], css_computed_width, CSS_WIDTH_SET, 41);
    expect_length_px("after inherits element rather than before",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_AFTER], css_computed_height, CSS_HEIGHT_SET, 10);
    expect_length_px("deferred variables also work on backdrop",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_BACKDROP], css_computed_width, CSS_WIDTH_SET, 50);
    destroy_style_case(&ctx);

    setup_style_case(&ctx,
        "div::before { --w: revert; --i: 32px !important; width: 99px !important; height: var(--i); }");
    css_stylesheet *user = create_stylesheet_from_css(
        "div::before { --w: 16px; --i: 19px !important; width: var(--w) !important; }", false, false);
    assert(css_select_ctx_insert_sheet(ctx.select_ctx, user, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
    select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    before = ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE];
    expect_length_px("deferred revert and consumer origin preserved",
        before, css_computed_width, CSS_WIDTH_SET, 16);
    expect_length_px("deferred custom property origin and importance preserved",
        before, css_computed_height, CSS_HEIGHT_SET, 19);
    destroy_style_case(&ctx);
    css_stylesheet_destroy(user);
}

static void test_deferred_pseudo_variable_cycles(void)
{
    style_case ctx;
    select_style_from_css(
        "div { --a: 10px; }"
        "div::before { width: var(--a, 21px); height: var(--b, 22px); }"
        "div::before { --a: var(--b); --b: var(--a); }"
        "div::after { --a: 30px; width: var(--a); }"
        "div::marker { --a: var(--a); width: 7px; }", &ctx);
    expect_length_px("deferred cycle masks inherited value",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE], css_computed_width, CSS_WIDTH_SET, 21);
    expect_length_px("deferred cycle remains invalid on subsequent use",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE], css_computed_height, CSS_HEIGHT_SET, 22);
    expect_length_px("pseudo cycle does not escape its environment",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_AFTER], css_computed_width, CSS_WIDTH_SET, 30);
    expect_length_px("unused cyclic declarations leave literal properties intact",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_MARKER], css_computed_width, CSS_WIDTH_SET, 7);
    destroy_style_case(&ctx);
}

static void test_empty_and_invalid_values(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --empty: ; --invalid: initial; width: var(--empty, 12px);"
        "height: var(--invalid, 13px); margin: 6px var(--missing,); }", &ctx);
    expect_length_type("empty does not use fallback", style, css_computed_width, CSS_WIDTH_AUTO);
    expect_length_px("initial is invalid", style, css_computed_height, CSS_HEIGHT_SET, 13);
    expect_length_px("empty fallback in shorthand", style, css_computed_margin_left, CSS_MARGIN_SET, 6);
    destroy_style_case(&ctx);

    setup_style_case(&ctx, "html { --w: 9px; } div { --w: initial; width: var(--w, 14px); }");
    attach_parent(&ctx);
    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_px("invalid local masks parent", style, css_computed_width, CSS_WIDTH_SET, 14);
    destroy_style_case(&ctx);
}

static void test_css_wide_custom_values(void)
{
    const char *keywords[] = {"inherit", "unset", "revert"};
    for (unsigned i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        char css[256];
        style_case ctx;
        snprintf(css, sizeof(css), "html { --w: 15px !important; } div { --w: %s; width: var(--w); }", keywords[i]);
        setup_style_case(&ctx, css);
        attach_parent(&ctx);
        const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
        expect_length_px(keywords[i], style, css_computed_width, CSS_WIDTH_SET, 15);
        destroy_style_case(&ctx);
    }
}

static void test_custom_property_origins(void)
{
    style_case ctx;
    setup_style_case(&ctx, "div { --w: revert; width: var(--w); }");
    css_stylesheet *user = create_stylesheet_from_css("div { --w: 16px; }", false, false);
    assert(css_select_ctx_insert_sheet(ctx.select_ctx, user, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
    const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_px("revert restores user origin", style, css_computed_width, CSS_WIDTH_SET, 16);
    destroy_style_case(&ctx);
    css_stylesheet_destroy(user);

    setup_style_case(&ctx, "div { --w: 18px !important; width: var(--w); }");
    user = create_stylesheet_from_css("div { --w: 19px !important; }", false, false);
    assert(css_select_ctx_insert_sheet(ctx.select_ctx, user, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_px("user important beats author", style, css_computed_width, CSS_WIDTH_SET, 19);
    destroy_style_case(&ctx);
    css_stylesheet_destroy(user);
}

static void test_token_preservation(void)
{
    style_case ctx;
    const css_computed_content_item *content;
    select_style_from_css(
        "div { --label: \"a\\\"b\"; --n: 12; width: var(--n)px;"
        "height: 7px; height: var(--n) junk; }"
        "div::before { content: var(--label); }"
        "div::after { content: var(--missing, \"fallback text\"); }", &ctx);
    expect_length_type("tokens cannot merge into dimension", ctx.results->styles[0], css_computed_width, CSS_WIDTH_AUTO);
    expect_length_type("trailing tokens invalidate entire value", ctx.results->styles[0], css_computed_height, CSS_HEIGHT_AUTO);
    assert(css_computed_content(ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE], &content) == CSS_CONTENT_SET);
    assert(content[0].type == CSS_COMPUTED_CONTENT_STRING);
    assert(lwc_string_length(content[0].data.string) == 3);
    assert(memcmp(lwc_string_data(content[0].data.string), "a\"b", 3) == 0);
    assert(css_computed_content(ctx.results->styles[CSS_PSEUDO_ELEMENT_AFTER], &content) == CSS_CONTENT_SET);
    assert(content[0].type == CSS_COMPUTED_CONTENT_STRING);
    assert(strcmp(lwc_string_data(content[0].data.string), "fallback text") == 0);
    destroy_style_case(&ctx);
}

static void test_full_cyclic_component(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b, var(--c)); --b: var(--a); --c: var(--b);"
        "width: var(--c, 23px); --ok: var(--c, 24px); height: var(--ok); }", &ctx);
    expect_length_px("entire cyclic component invalid", style, css_computed_width, CSS_WIDTH_SET, 23);
    expect_length_px("dependent fallback remains valid", style, css_computed_height, CSS_HEIGHT_SET, 24);
    destroy_style_case(&ctx);
}

static void test_long_dependency_chain(void)
{
    unsigned count = 4096;
    size_t capacity = count * 48 + 128, offset = 0;
    char *css = malloc(capacity);
    style_case ctx;
    assert(css != NULL);
    offset += snprintf(css + offset, capacity - offset, "div {");
    for (unsigned i = 0; i < count; i++)
        offset += snprintf(css + offset, capacity - offset, "--v%u:var(--v%u);", i, i + 1);
    snprintf(css + offset, capacity - offset, "--v%u:27px; width:var(--v0); }", count);
    const css_computed_style *style = select_style_from_css(css, &ctx);
    expect_length_px("long iterative dependency chain", style, css_computed_width, CSS_WIDTH_SET, 27);
    destroy_style_case(&ctx);
    free(css);
}

static void test_expansion_limit(void)
{
    char css[4096];
    size_t offset = snprintf(css, sizeof(css), "div { --v0:0;");
    style_case ctx;
    for (unsigned i = 1; i <= 17; i++)
        offset += snprintf(css + offset, sizeof(css) - offset,
            "--v%u:var(--v%u) var(--v%u);", i, i - 1, i - 1);
    snprintf(css + offset, sizeof(css) - offset,
        "width:var(--v15, 28px); height:var(--v16, 29px); }");
    const css_computed_style *style = select_style_from_css(css, &ctx);
    /* v15 has 65,535 tokens: present but not a width. v16 is over the cap. */
    expect_length_type("below limit does not use fallback", style, css_computed_width, CSS_WIDTH_AUTO);
    expect_length_px("over limit becomes invalid", style, css_computed_height, CSS_HEIGHT_SET, 29);
    destroy_style_case(&ctx);
}

static void test_sharing_and_parent_reselection(void)
{
    style_case ctx;
    setup_style_case(&ctx, "html { --w: 30px; } div { width: var(--w); }");
    attach_parent(&ctx);
    select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    test_node sibling = {.name = ctx.node_name, .parent = &ctx.parent_node, .previous = &ctx.node};
    css_select_results *results;
    const css_computed_style *style = select_style_for_node(&ctx, &sibling, NULL, &results);
    expect_length_px("shared sibling", style, css_computed_width, CSS_WIDTH_SET, 30);
    assert(((struct css_node_data *)sibling.libcss_node_data)->var_ctx ==
        ((struct css_node_data *)ctx.node.libcss_node_data)->var_ctx);
    destroy_node_data(&sibling);
    css_select_results_destroy(results);

    destroy_node_data(&ctx.parent_node);
    css_select_results_destroy(ctx.parent_results);
    ctx.inline_style = create_stylesheet_from_css("--w:31px", true, false);
    select_style_for_node(&ctx, &ctx.parent_node, ctx.inline_style, &ctx.parent_results);
    style = select_style_for_node(&ctx, &sibling, NULL, &results);
    expect_length_px("stale sibling environment not shared", style, css_computed_width, CSS_WIDTH_SET, 31);
    expect_length_px("old snapshot unchanged", ctx.results->styles[0], css_computed_width, CSS_WIDTH_SET, 30);
    destroy_node_data(&sibling);
    css_select_results_destroy(results);
    destroy_style_case(&ctx);
}

static void test_nested_important_and_case_sensitive_names(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a:32px; --A:33px; --w:var(--a, !important);"
        "width:var(--w); height:var(--A); }", &ctx);
    expect_length_px("nested bang retained in fallback", style, css_computed_width, CSS_WIDTH_SET, 32);
    expect_length_px("case sensitive name", style, css_computed_height, CSS_HEIGHT_SET, 33);
    destroy_style_case(&ctx);
}

static void test_deferred_important(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --w: 34px; --m: 4px 5px; width:var(--w) !important; width:35px;"
        "margin:var(--m) !important; margin-left:36px;"
        "height:var(--missing) !important; height:37px; }", &ctx);
    expect_length_px("deferred important longhand", style, css_computed_width, CSS_WIDTH_SET, 34);
    expect_length_px("deferred important shorthand", style, css_computed_margin_left, CSS_MARGIN_SET, 5);
    expect_length_type("invalid important still wins", style, css_computed_height, CSS_HEIGHT_AUTO);
    destroy_style_case(&ctx);
}

static void test_property_handler_integration(void)
{
    style_case ctx;
    css_fixed number;
    css_color color;
    int32_t n_tracks;
    css_computed_grid_track *tracks;
    lwc_string *url;
    setup_style_case(&ctx,
        "html { --size:2em; --half:calc(50% + 10px); --flex:2 1 30px;"
        "--tracks:40px 1fr; --ink:#123456; --image:url(\"icon.svg\"); font-size:30px; }"
        "div { font-size:12px; width:var(--half); height:var(--size);"
        "display:var(--missing, flex); flex:var(--flex); grid-template-columns:var(--tracks);"
        "stroke:var(--ink); stroke-width:var(--size); background-image:var(--image); }");
    attach_parent(&ctx);
    const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_px("calc through generated width parser", style, css_computed_width, CSS_WIDTH_SET, 110);
    expect_length_px("inherited tokens use consumer font", style, css_computed_height, CSS_HEIGHT_SET, 24);
    assert(css_computed_display(style, false) == CSS_DISPLAY_FLEX);
    assert(css_computed_flex_grow(style, &number) == CSS_FLEX_GROW_SET && number == INTTOFIX(2));
    expect_length_px("flex shorthand basis", style, css_computed_flex_basis, CSS_FLEX_BASIS_SET, 30);
    css_computed_grid_template_columns(style, &n_tracks, &tracks);
    assert(n_tracks == 2 && tracks != NULL);
    assert(tracks[0].unit == CSS_UNIT_PX && tracks[0].value == INTTOFIX(40));
    assert(tracks[1].unit == CSS_UNIT_FR && tracks[1].value == INTTOFIX(1));
    assert(css_computed_stroke(style, &color) == CSS_STROKE_COLOR && color == 0xff123456);
    expect_length_px("SVG stroke width", style, css_computed_stroke_width, CSS_STROKE_WIDTH_SET, 24);
    assert(css_computed_background_image(style, &url) == CSS_BACKGROUND_IMAGE_IMAGE);
    assert(url != NULL && strcmp(lwc_string_data(url), "icon.svg") == 0);
    destroy_style_case(&ctx);
}

/* The variable collection pass walks ordinary bytecode too. Grid integers
 * must not be decoded as OPVs: 32768's payload resembles an angle opcode,
 * which used to skip the following custom-property declaration. */
static void test_grid_placement_payloads(void)
{
    static const struct {
        const char *name;
        uint8_t (*get)(const css_computed_style *, int32_t *);
    } properties[] = {
        {"grid-row-start", css_computed_grid_row_start},
        {"grid-row-end", css_computed_grid_row_end},
        {"grid-column-start", css_computed_grid_column_start},
        {"grid-column-end", css_computed_grid_column_end},
    };
    static const struct { const char *text; uint8_t type; int number; } forms[] = {
        {"auto", CSS_GRID_LINE_AUTO, 0}, {"2", CSS_GRID_LINE_SET, 2},
        {"-3", CSS_GRID_LINE_SET, -3}, {"span 2", CSS_GRID_LINE_SPAN, 2},
        {"32768", CSS_GRID_LINE_SET, 32768}, {"span 32768", CSS_GRID_LINE_SPAN, 32768},
        {"initial", CSS_GRID_LINE_AUTO, 0},
    };
    for (size_t p = 0; p < sizeof(properties) / sizeof(properties[0]); p++) {
        for (size_t f = 0; f < sizeof(forms) / sizeof(forms[0]); f++) {
            for (unsigned important = 0; important < 2; important++) {
                style_case ctx;
                char css[256];
                snprintf(css, sizeof(css), "div { %s:%s%s; --size:37px; width:var(--size); }",
                    properties[p].name, forms[f].text, important ? "!important" : "");
                const css_computed_style *style = select_style_from_css(css, &ctx);
                expect_length_px(css, style, css_computed_width, CSS_WIDTH_SET, 37);
                int32_t number;
                uint8_t type = properties[p].get(style, &number);
                if (type != forms[f].type || (type != CSS_GRID_LINE_AUTO && number != INTTOFIX(forms[f].number))) {
                    fprintf(stderr, "Grid payload changed: %s (type %u, number %d)\n", css, type, number);
                    assert(false);
                }
                destroy_style_case(&ctx);
            }
        }
    }
}

/* Ordinary counted payloads must be skipped intact while collecting custom
 * properties, including when their values are independently important. */
static void test_counted_payloads(void)
{
    const char *const properties[] = {"grid-template-columns", "grid-template-rows"};
    for (size_t p = 0; p < 2; p++) {
        for (unsigned important = 0; important < 2; important++) {
            style_case ctx;
            char css[512];
            snprintf(css, sizeof(css), "div { %s:repeat(2,minmax(32768px,1fr) 20px)%s;"
                "--size:37px; width:var(--size); }", properties[p], important ? "!important" : "");
            const css_computed_style *style = select_style_from_css(css, &ctx);
            expect_length_px(css, style, css_computed_width, CSS_WIDTH_SET, 37);
            int32_t count;
            css_computed_grid_track *tracks;
            uint8_t type = p == 0 ? css_computed_grid_template_columns(style, &count, &tracks) :
                css_computed_grid_template_rows(style, &count, &tracks);
            assert(type == CSS_GRID_TEMPLATE_SET && count == 4 && tracks != NULL);
            for (int i = 0; i < count; i++) {
                if (i % 2 == 0) {
                    assert(tracks[i].unit == CSS_UNIT_MINMAX && tracks[i].value == INTTOFIX(32768));
                    assert(tracks[i].min_unit == CSS_UNIT_PX && tracks[i].max_unit == CSS_UNIT_FR);
                    assert(tracks[i].max_value == INTTOFIX(1));
                } else {
                    assert(tracks[i].unit == CSS_UNIT_PX && tracks[i].value == INTTOFIX(20));
                }
            }
            destroy_style_case(&ctx);
        }
    }
    for (unsigned important = 0; important < 2; important++) {
        style_case ctx;
        char css[512];
        snprintf(css, sizeof(css), "div { transform:translate(1px,2px) scale(2,0) rotate(30deg) "
            "translateX(3px) translateY(4px) scaleX(5) scaleY(6)%s; --size:37px; width:var(--size); }",
            important ? "!important" : "");
        const css_computed_style *style = select_style_from_css(css, &ctx);
        expect_length_px(css, style, css_computed_width, CSS_WIDTH_SET, 37);
        destroy_style_case(&ctx);
    }
}

static void test_svg_paint_payloads(void)
{
    for (unsigned important = 0; important < 2; important++) {
        style_case ctx;
        char css[512];
        const char *priority = important ? "!important" : "";
        snprintf(css, sizeof(css), "div { fill:#ff000001%s;stroke:#80008080%s;stroke-width:32768px%s;"
            "fill-opacity:2;stroke-opacity:-.5;--size:37px;width:var(--size); }", priority, priority, priority);
        const css_computed_style *style = select_style_from_css(css, &ctx);
        expect_length_px(css, style, css_computed_width, CSS_WIDTH_SET, 37);
        css_color color;
        css_fixed alpha;
        assert(css_computed_fill(style, &color) == CSS_FILL_COLOR && color == 0x01ff0000);
        assert(css_computed_stroke(style, &color) == CSS_STROKE_COLOR && color == 0x80800080);
        expect_length_px(css, style, css_computed_stroke_width, CSS_STROKE_WIDTH_SET, 32768);
        assert(css_computed_fill_opacity(style, &alpha) == CSS_FILL_OPACITY_SET && alpha == INTTOFIX(1));
        assert(css_computed_stroke_opacity(style, &alpha) == CSS_STROKE_OPACITY_SET && alpha == 0);
        destroy_style_case(&ctx);
    }
    const char *const forms[] = {"", "fill:initial;stroke:initial", "fill:currentcolor;stroke:currentcolor",
        "fill:none;stroke:none"};
    for (unsigned i = 0; i < 4; i++) {
        style_case ctx;
        char css[128];
        snprintf(css, sizeof(css), "div { %s; --size:37px;width:var(--size); }", forms[i]);
        const css_computed_style *style = select_style_from_css(css, &ctx);
        expect_length_px(css, style, css_computed_width, CSS_WIDTH_SET, 37);
        css_color color;
        if (i < 2) {
            assert(css_computed_fill(style, &color) == CSS_FILL_COLOR && color == 0xff000000);
            assert(css_computed_stroke(style, &color) == CSS_STROKE_NONE);
        } else if (i == 2) {
            assert(css_computed_fill(style, &color) == CSS_FILL_CURRENT_COLOR);
            assert(css_computed_stroke(style, &color) == CSS_STROKE_CURRENT_COLOR);
        } else {
            assert(css_computed_fill(style, &color) == CSS_FILL_NONE);
            assert(css_computed_stroke(style, &color) == CSS_STROKE_NONE);
        }
        destroy_style_case(&ctx);
    }
}

struct selection_counts { unsigned attempts, publications, fail_on_attempt; };

static css_error counted_hints(void *pw, void *node, uint32_t *nhints, css_hint **hints)
{
    struct selection_counts *counts = pw;
    if (++counts->attempts == counts->fail_on_attempt)
        return CSS_NOMEM;
    return node_presentational_hint(NULL, node, nhints, hints);
}

static css_error counted_publication(void *pw, void *node, void *data)
{
    ((struct selection_counts *)pw)->publications++;
    return set_libcss_node_data(NULL, node, data);
}

static void test_deferred_revert(void)
{
    static const struct {
        const char *author, *user, *inline_css;
        unsigned attempts;
        int width;
        bool pseudo;
    } cases[] = {
        {"div {--w:49px;width:var(--w)}", "div {width:37px}", NULL, 1, 49, false},
        {"div {width:revert}", "div {width:37px}", NULL, 1, 37, false},
        {"div {width:var(--missing,revert)}", "div {width:37px}", NULL, 2, 37, false},
        {"div::before {width:var(--missing,revert)}", "div::before {width:41px}", NULL, 2, 41, true},
        {NULL, "div {width:37px}", "width:var(--missing,revert)", 2, 37, false},
        {NULL, "div {width:37px}", "width:revert", 1, 37, false},
        {"div {width:99px}", "div {width:var(--missing,revert)!important}", NULL, 2, 23, false},
        {"div {width:var(--missing,revert)!important}", "div {width:37px!important}", NULL, 1, 37, false},
    };
    const size_t n_cases = sizeof(cases) / sizeof(cases[0]);
    for (size_t variant = 0; variant < 2 * n_cases; variant++) {
        size_t i = variant % n_cases;
        bool child = variant >= n_cases;
        style_case ctx;
        setup_style_case(&ctx, cases[i].author != NULL ? cases[i].author : "");
        if (cases[i].author == NULL)
            assert(css_select_ctx_remove_sheet(ctx.select_ctx, ctx.sheet) == CSS_OK);
        css_stylesheet *ua = create_stylesheet_from_css("div {width:23px} div::before {width:29px}", false, false);
        css_stylesheet *user = create_stylesheet_from_css(cases[i].user, false, false);
        assert(css_select_ctx_insert_sheet(ctx.select_ctx, user, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
        assert(css_select_ctx_insert_sheet(ctx.select_ctx, ua, 0, CSS_ORIGIN_UA, NULL) == CSS_OK);
        css_bloom saved_bloom[CSS_BLOOM_SIZE];
        struct css_node_data *parent_data = NULL;
        if (child) {
            attach_parent(&ctx);
            parent_data = ctx.parent_node.libcss_node_data;
            memcpy(saved_bloom, parent_data->bloom, sizeof(saved_bloom));
        }
        css_stylesheet *inline_sheet = cases[i].inline_css != NULL ?
            create_stylesheet_from_css(cases[i].inline_css, true, false) : NULL;
        css_select_handler handler = select_handler;
        handler.node_presentational_hint = counted_hints;
        handler.set_libcss_node_data = counted_publication;
        struct selection_counts counts = {0};
        css_media media = {.type = CSS_MEDIA_ALL};
        if (i == 2) {
            for (unsigned fail = 1; fail <= 2; fail++) {
                counts.fail_on_attempt = fail;
                assert(css_select_style(ctx.select_ctx, &ctx.node, &unit_ctx, &media, inline_sheet,
                    &handler, &counts, &ctx.results) == CSS_NOMEM);
                assert(ctx.results == NULL && ctx.node.libcss_node_data == NULL);
                assert(counts.attempts == fail && counts.publications == 0);
                if (child)
                    assert(memcmp(saved_bloom, parent_data->bloom, sizeof(saved_bloom)) == 0);
                counts = (struct selection_counts){0};
            }
        }
        assert(css_select_style(ctx.select_ctx, &ctx.node, &unit_ctx, &media, inline_sheet,
            &handler, &counts, &ctx.results) == CSS_OK);
        char label[64];
        snprintf(label, sizeof(label), "revert case %zu (%s)", i, child ? "child" : "root");
        expect_length_px(label, ctx.results->styles[cases[i].pseudo ? CSS_PSEUDO_ELEMENT_BEFORE : CSS_PSEUDO_ELEMENT_NONE],
            css_computed_width, CSS_WIDTH_SET, cases[i].width);
        if (counts.attempts != cases[i].attempts || counts.publications != 1) {
            fprintf(stderr, "%s: attempts=%u (expected %u), publications=%u (expected 1)\n",
                label, counts.attempts, cases[i].attempts, counts.publications);
            assert(false);
        }
        if (child) {
            /* Failure and replay must leave the borrowed ancestor filter
             * intact for subsequent selections and eventual parent cleanup. */
            assert(ctx.parent_node.libcss_node_data == parent_data);
            assert(memcmp(saved_bloom, parent_data->bloom, sizeof(saved_bloom)) == 0);
            test_node sibling = {.name = ctx.node_name, .parent = &ctx.parent_node};
            css_select_results *results = NULL;
            select_style_for_node(&ctx, &sibling, inline_sheet, &results);
            expect_length_px(label, results->styles[cases[i].pseudo ? CSS_PSEUDO_ELEMENT_BEFORE : CSS_PSEUDO_ELEMENT_NONE],
                css_computed_width, CSS_WIDTH_SET, cases[i].width);
            destroy_node_data(&sibling);
            css_select_results_destroy(results);
        }
        destroy_style_case(&ctx);
        css_stylesheet_destroy(ua);
        css_stylesheet_destroy(user);
        if (inline_sheet != NULL)
            css_stylesheet_destroy(inline_sheet);
    }
}

static void test_gap_aliases(void)
{
    static const struct { const char *declarations; int row, column; } cases[] = {
        {"gap:2px 6px!important;gap:9px", 2, 6},
        {"grid-gap:2px 6px!important;gap:9px", 2, 6},
        {"row-gap:2px!important;grid-gap:9px", 2, 9},
        {"grid-row-gap:2px!important;gap:9px", 2, 9},
        {"--g:3px 4px;grid-gap:var(--g)", 3, 4},
        {"--g:3px 4px;grid-gap:var(--g);row-gap:5px", 5, 4},
        {"--g:3px 4px;gap:7px;grid-gap:var(--g)", 3, 4},
        {"--g:3px 4px;grid-gap:var(--g)!important;gap:7px", 3, 4},
        {"--g:3px 4px;gap:7px!important;grid-gap:var(--g)", 7, 7},
        {"--r:8px;--c:9px;grid-row-gap:var(--r);grid-column-gap:var(--c)", 8, 9},
        {"--g:3px;grid-row-gap:var(--g)!important;gap:7px", 3, 7},
        {"--g:3px;row-gap:var(--g)!important;grid-gap:7px", 3, 7},
        {"grid-gap:var(--missing, 8px 9px)", 8, 9},
        {"--r:8px;grid-row-gap:var(--r);grid-column-gap:var(--r)", 8, 8},
        {"--grid-gap:11px;--Grid-gap:12px;grid-row-gap:var(--grid-gap);column-gap:var(--Grid-gap)", 11, 12},
        /* The custom-property scan must skip the whole row-gap payload before
         * reading subsequent definitions, even when they follow a shorthand. */
        {"row-gap:2px;--c:6px;column-gap:var(--c)", 2, 6},
        {"grid-row-gap:2px;--c:6px;column-gap:var(--c)", 2, 6},
        {"gap:2px 6px;--r:4px;row-gap:var(--r)", 4, 6},
        {"grid-gap:2px 6px;--r:4px;grid-row-gap:var(--r)", 4, 6},
        {"row-gap:32768px;--c:6px;column-gap:var(--c)", 32768, 6},
        {"grid-gap:32768px 6px;--r:4px;grid-row-gap:var(--r)", 4, 6},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        style_case ctx;
        char css[256];
        snprintf(css, sizeof(css), "div {%s}", cases[i].declarations);
        const css_computed_style *style = select_style_from_css(css, &ctx);
        expect_length_px(css, style, css_computed_row_gap, CSS_ROW_GAP_SET, cases[i].row);
        expect_length_px(css, style, css_computed_column_gap, CSS_COLUMN_GAP_SET, cases[i].column);
        destroy_style_case(&ctx);
    }
    /* A deferred invalid value resets the winning property. It must not bring
     * back an earlier declaration under the other spelling. */
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div {gap:9px;grid-gap:var(--missing)}", &ctx);
    css_fixed_or_calc value;
    css_unit unit;
    assert(css_computed_row_gap(style, &value, &unit) == CSS_ROW_GAP_NORMAL);
    assert(css_computed_column_gap(style, &value, &unit) == CSS_COLUMN_GAP_NORMAL);
    destroy_style_case(&ctx);
}

static void test_repeat_and_decoration_values(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --repeat: repeat no-repeat,no-repeat repeat; --lines: overline underline;"
        "background-image:url(a.png),url(b.png);background-repeat:var(--repeat)!important;"
        "background-repeat:repeat; text-decoration:var(--lines)!important;text-decoration:none; }", &ctx);
    assert(css_computed_background_repeat_layer(style, 0) == CSS_BACKGROUND_REPEAT_REPEAT_X);
    assert(css_computed_background_repeat_layer(style, 1) == CSS_BACKGROUND_REPEAT_REPEAT_Y);
    assert(css_computed_text_decoration(style) == (CSS_TEXT_DECORATION_UNDERLINE | CSS_TEXT_DECORATION_OVERLINE));
    destroy_style_case(&ctx);

    const char *const keywords[] = {"initial", "unset", "revert"};
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        for (unsigned deferred = 0; deferred < 2; deferred++) {
            char css[256];
            snprintf(css, sizeof(css), "div {text-decoration:underline; text-decoration:%s%s%s}",
                deferred ? "var(--missing, " : "", keywords[i], deferred ? ")" : "");
            setup_style_case(&ctx, css);
            css_stylesheet *user = create_stylesheet_from_css("div {text-decoration:overline}", false, false);
            assert(css_select_ctx_insert_sheet(ctx.select_ctx, user, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
            style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
            uint8_t expected = i == 2 ? CSS_TEXT_DECORATION_OVERLINE : CSS_TEXT_DECORATION_NONE;
            assert(css_computed_text_decoration(style) == expected);
            destroy_style_case(&ctx);
            css_stylesheet_destroy(user);
        }
    }
}

/* CSS Backgrounds 4, repeat longhands: lists retain independent lengths,
 * cascade and inheritance. Expected letters: b=both, x/y=one axis, n=neither. */
static void expect_background_repeats(const css_computed_style *style, const char *expected, const char *css)
{
    for (uint32_t layer = 0; expected[layer] != '\0'; layer++) {
        uint8_t want = expected[layer] == 'b' ? CSS_BACKGROUND_REPEAT_REPEAT :
            expected[layer] == 'x' ? CSS_BACKGROUND_REPEAT_REPEAT_X :
            expected[layer] == 'y' ? CSS_BACKGROUND_REPEAT_REPEAT_Y : CSS_BACKGROUND_REPEAT_NO_REPEAT;
        uint8_t actual = css_computed_background_repeat_layer(style, layer);
        if (want != actual) {
            fprintf(stderr, "repeat layer %u: expected %c (%u), got %u: %s\n",
                layer, expected[layer], want, actual, css);
            assert(false);
        }
    }
    assert(css_computed_background_repeat(style) == css_computed_background_repeat_layer(style, 0));
}

static void test_background_repeat_axes(void)
{
    static const struct { const char *css, *expected; } cases[] = {
        {"background-repeat-x:no-repeat", "yyyyyy"},
        {"background-repeat-y:no-repeat", "xxxxxx"},
        {"background-repeat:repeat-x,repeat-y;background-repeat-x:no-repeat", "nynyny"},
        {"background-repeat-x:repeat,no-repeat;background-repeat-y:repeat,repeat,no-repeat", "byxybn"},
        {"background-repeat-x:no-repeat;background-repeat:repeat-x", "xxxxxx"},
        {"background-repeat-x:no-repeat!important;background-repeat:repeat-x,repeat-y", "nynyny"},
        {"background-repeat:repeat-x!important;background-repeat-x:no-repeat,no-repeat", "xxxxxx"},
        {"background-repeat-x:no-repeat,no-repeat;background-repeat-x:initial", "bbbbbb"},
        {"background-repeat-y:no-repeat,no-repeat;background-repeat-y:unset", "bbbbbb"},
        {"background-repeat-x:no-repeat,no-repeat;background:blue", "bbbbbb"},
        {"--r:repeat-x,repeat-y;background-repeat:var(--r);background-repeat-x:no-repeat", "nynyny"},
        {"--r:no-repeat,repeat;background-repeat-x:var(--r);background-repeat-y:no-repeat", "nxnxnx"},
        {"--r:no-repeat,repeat;background-repeat-x:var(--r);background-repeat:repeat", "bbbbbb"},
        {"--r:no-repeat,repeat;background-repeat-x:var(--r)!important;background-repeat:repeat-x", "nxnxnx"},
        {"background-repeat:no-repeat,no-repeat;background-repeat-x:var(--missing)", "xxxxxx"},
        {"--r:invalid;background-repeat:no-repeat,no-repeat;background-repeat-y:var(--r)", "yyyyyy"},
        {"background-repeat:no-repeat;background-repeat:var(--missing);background-repeat-y:no-repeat", "xxxxxx"},
        {"background-repeat:repeat-x,repeat-y;background-repeat-x:var(--missing,initial)", "xbxbxb"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (unsigned inlined = 0; inlined < 2; inlined++) {
            char css[512];
            snprintf(css, sizeof(css), "div {%s;--w:37px;width:var(--w)}", cases[i].css);
            style_case ctx;
            setup_style_case(&ctx, inlined ? "" : css);
            if (inlined)
                ctx.inline_style = create_stylesheet_from_css(cases[i].css, true, false);
            const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, ctx.inline_style, &ctx.results);
            expect_background_repeats(style, cases[i].expected, cases[i].css);
            if (!inlined)
                expect_length_px(css, style, css_computed_width, CSS_WIDTH_SET, 37);
            destroy_style_case(&ctx);
        }
    }
    const char *const children[] = {
        "background-repeat-x:inherit;background-repeat-y:no-repeat",
        "background-repeat-x:no-repeat;background-repeat-y:inherit",
        "background-repeat:inherit",
        "background-repeat:var(--missing,inherit)",
    };
    const char *const expected[] = {"xnxnxn", "yynyyn", "byxybn", "byxybn"};
    for (size_t i = 0; i < sizeof(children) / sizeof(children[0]); i++) {
        style_case p, c;
        const css_computed_style *parent = select_style_from_css(
            "div {background-repeat-x:repeat,no-repeat;background-repeat-y:repeat,repeat,no-repeat}", &p);
        char css[256];
        snprintf(css, sizeof(css), "div {%s}", children[i]);
        setup_style_case(&c, css);
        c.node.parent = &p.node; /* Root inherit is initial; exercise a real child. */
        const css_computed_style *child = select_style_for_node(&c, &c.node, NULL, &c.results);
        css_computed_style *composed;
        assert(css_computed_style_compose(parent, child, &unit_ctx, &composed) == CSS_OK);
        destroy_style_case(&p);
        destroy_style_case(&c);
        expect_background_repeats(composed, expected[i], children[i]);
        css_computed_style_destroy(composed);
    }
    /* Revert must fall back independently to the lower origin's list. */
    for (unsigned deferred = 0; deferred < 2; deferred++) {
        style_case ctx;
        setup_style_case(&ctx, deferred ?
            "div {background-repeat:no-repeat;background-repeat-x:var(--missing,revert)}" :
            "div {background-repeat:no-repeat;background-repeat-x:revert}");
        css_stylesheet *user = create_stylesheet_from_css("div {background-repeat:repeat,no-repeat}", false, false);
        assert(css_select_ctx_insert_sheet(ctx.select_ctx, user, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
        const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
        expect_background_repeats(style, "xnxnxn", "axis revert to user origin");
        destroy_style_case(&ctx);
        css_stylesheet_destroy(user);
    }
}

/* Backgrounds 4: position axes cascade independently, and each list repeats
 * against the image list. The expected offsets/units come from the CSS values. */
struct position_expectation {
    const char *css;
    int x[3], y[3];
    unsigned nx, ny;
    css_unit xu, yu;
};

static void expect_background_positions(const css_computed_style *style, const struct position_expectation *want)
{
    for (unsigned layer = 0; layer < 6; layer++) {
        css_fixed_or_calc x, y;
        css_unit xu, yu;
        uint8_t type = css_computed_background_position_layer(style, layer, &x, &xu, &y, &yu);
        if (type != CSS_BACKGROUND_POSITION_SET || x.value != INTTOFIX(want->x[layer % want->nx]) ||
            y.value != INTTOFIX(want->y[layer % want->ny]) || xu != want->xu || yu != want->yu) {
            fprintf(stderr, "position layer %u: got %d/%u %d/%u: %s\n", layer, x.value, xu, y.value, yu, want->css);
            assert(false);
        }
    }
}

static void test_background_position_axes(void)
{
    static const struct position_expectation cases[] = {
        {"background-position:10px 20px,30px 40px;background-position-x:70px", {70}, {20,40}, 1,2,
            CSS_UNIT_PX,CSS_UNIT_PX},
        {"background-position-x:1px,2px;background-position-y:3px,4px,5px", {1,2}, {3,4,5}, 2,3,
            CSS_UNIT_PX,CSS_UNIT_PX},
        {"background-position-x:7px!important;background-position:1px 2px,3px 4px", {7}, {2,4}, 1,2,
            CSS_UNIT_PX,CSS_UNIT_PX},
        {"background-position:1px 2px!important;background-position-y:8px", {1}, {2}, 1,1,
            CSS_UNIT_PX,CSS_UNIT_PX},
        {"background-position-x:7px,8px;background-position:top,right bottom", {50,100}, {0,100}, 2,2,
            CSS_UNIT_PCT,CSS_UNIT_PCT},
        {"background-position-x:right,left,center;background-position-y:bottom,center", {100,0,50}, {100,50}, 3,2,
            CSS_UNIT_PCT,CSS_UNIT_PCT},
        {"background-position:1px 2px,3px 4px;background-position-x:initial", {0}, {2,4}, 1,2,
            CSS_UNIT_PCT,CSS_UNIT_PX},
        {"background-position:1px 2px,3px 4px;background-position-y:unset", {1,3}, {0}, 2,1,
            CSS_UNIT_PX,CSS_UNIT_PCT},
        {"background-position:1px 2px,3px 4px;background:blue", {0}, {0}, 1,1,
            CSS_UNIT_PCT,CSS_UNIT_PCT},
        {"--p:1px 2px,3px 4px;background-position:var(--p);background-position-x:7px", {7}, {2,4}, 1,2,
            CSS_UNIT_PX,CSS_UNIT_PX},
        {"--p:1px,2px;background-position-x:var(--p);background-position-y:7px", {1,2}, {7}, 2,1,
            CSS_UNIT_PX,CSS_UNIT_PX},
        {"--p:1px,2px;background-position-x:var(--p);background:blue", {0}, {0}, 1,1,
            CSS_UNIT_PCT,CSS_UNIT_PCT},
        {"--p:1px,2px;background-position-x:var(--p)!important;background-position:5px 7px", {1,2}, {7}, 2,1,
            CSS_UNIT_PX,CSS_UNIT_PX},
        {"background-position:1px 2px,3px 4px;background-position-x:var(--missing)", {0}, {2,4}, 1,2,
            CSS_UNIT_PCT,CSS_UNIT_PX},
        {"--p:left;background-position:1px 2px,3px 4px;background-position-y:var(--p)", {1,3}, {0}, 2,1,
            CSS_UNIT_PX,CSS_UNIT_PCT},
        {"background-position:var(--missing);background-position-y:7px", {0}, {7}, 1,1,
            CSS_UNIT_PCT,CSS_UNIT_PX},
        /* These fixed-point payloads resemble opcodes: the bytecode walkers
         * must skip each axis's operands before reaching the width variable. */
        {"background-position:32768px 32768px,32768px 32768px!important", {32768}, {32768}, 1,1,
            CSS_UNIT_PX,CSS_UNIT_PX},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (unsigned inlined = 0; inlined < 2; inlined++) {
            char declarations[512], css[520];
            snprintf(declarations, sizeof(declarations), "%s;--w:37px;width:var(--w)", cases[i].css);
            snprintf(css, sizeof(css), "div {%s}", declarations);
            style_case ctx;
            setup_style_case(&ctx, inlined ? "" : css);
            if (inlined)
                ctx.inline_style = create_stylesheet_from_css(declarations, true, false);
            const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, ctx.inline_style, &ctx.results);
            expect_background_positions(style, &cases[i]);
            expect_length_px(css, style, css_computed_width, CSS_WIDTH_SET, 37);
            destroy_style_case(&ctx);
        }
    }
    for (unsigned deferred = 0; deferred < 2; deferred++) {
        style_case ctx;
        setup_style_case(&ctx, deferred ?
            "div {background-position:7px 8px;background-position-x:var(--missing,revert)}" :
            "div {background-position:7px 8px;background-position-x:revert}");
        css_stylesheet *user = create_stylesheet_from_css("div {background-position:1px 2px,3px 4px}", false, false);
        assert(css_select_ctx_insert_sheet(ctx.select_ctx, user, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
        const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
        const struct position_expectation want = {"axis revert to user origin", {1,3}, {8}, 2,1,
            CSS_UNIT_PX,CSS_UNIT_PX};
        expect_background_positions(style, &want);
        destroy_style_case(&ctx);
        css_stylesheet_destroy(user);
    }
}

/* Spec-derived edge mapping cases shared by the three logical groups. */
static const struct {
    const char *names[5]; /* shorthand, start, end, physical left, physical right */
    length_getter left, right;
    unsigned set, initial;
} inline_groups[] = {
    {{"margin-inline", "margin-inline-start", "margin-inline-end", "margin-left", "margin-right"},
        css_computed_margin_left, css_computed_margin_right, CSS_MARGIN_SET, CSS_MARGIN_SET},
    {{"padding-inline", "padding-inline-start", "padding-inline-end", "padding-left", "padding-right"},
        css_computed_padding_left, css_computed_padding_right, CSS_PADDING_SET, CSS_PADDING_SET},
    {{"inset-inline", "inset-inline-start", "inset-inline-end", "left", "right"},
        css_computed_left, css_computed_right, CSS_LEFT_SET, CSS_LEFT_AUTO},
};

static void inline_declarations(char *out, size_t size, unsigned group, const char *source)
{
    const char *keys = "pselr";
    size_t used = 0;
    while (*source) {
        if (*source != '$') {
            assert(used + 1 < size);
            out[used++] = *source++;
            continue;
        }
        const char *key = strchr(keys, source[1]);
        assert(key != NULL && source[1] != 0);
        const char *name = inline_groups[group].names[key - keys];
        size_t length = strlen(name);
        assert(used + length < size);
        memcpy(out + used, name, length);
        used += length;
        source += 2;
    }
    out[used] = 0;
}

enum { INLINE_INITIAL = 9999 };
static void expect_inline_edges(const css_computed_style *style, unsigned group, const char *css, int left, int right)
{
    int values[] = {left, right};
    length_getter getters[] = {inline_groups[group].left, inline_groups[group].right};
    for (unsigned side = 0; side < 2; side++) {
        if (values[side] == INLINE_INITIAL && inline_groups[group].initial != inline_groups[group].set)
            expect_length_type(css, style, getters[side], inline_groups[group].initial);
        else
            expect_length_px(css, style, getters[side], inline_groups[group].set,
                values[side] == INLINE_INITIAL ? 0 : values[side]);
    }
}

static void test_inline_edges_cascade(void)
{
    static const struct { const char *css; int ltr[2], rtl[2]; } cases[] = {
        {"$p:1px 2px", {1,2}, {2,1}},
        {"$s:1px;$l:3px;$e:2px", {3,2}, {2,1}},
        {"$s:1px;$r:3px;$e:2px", {1,2}, {2,3}},
        {"$l:3px;$s:1px;$e:2px", {1,2}, {2,1}},
        {"$p:1px 2px;$l:3px", {3,2}, {3,1}},
        {"$l:3px!important;$p:1px 2px", {3,2}, {3,1}},
        {"$s:1px!important;$p:3px 4px", {1,4}, {4,1}},
        {"$p:1px!important;$e:4px!important", {1,4}, {4,1}},
        {"--x:5px;$s:calc(var(--x) + 2px);$e:2px", {7,2}, {2,7}},
        {"--x:1px 2px;$p:var(--x);$e:5px", {1,5}, {5,1}},
        {"$p:1px 2px;$s:initial", {INLINE_INITIAL,2}, {2,INLINE_INITIAL}},
        {"$p:1px 2px;$s:unset", {INLINE_INITIAL,2}, {2,INLINE_INITIAL}},
        {"$p:1px 2px;$s:inherit", {INLINE_INITIAL,2}, {2,INLINE_INITIAL}},
        {"$p:1px 2px;$s:var(--missing)", {INLINE_INITIAL,2}, {2,INLINE_INITIAL}},
        {"$p:1px 2px;$s:calc(1em + 5%)", {26,2}, {2,26}},
        {"$p:1px 2px;$s:32768px;--w:3px;width:var(--w)", {32768,2}, {2,32768}},
    };
    for (unsigned group = 0; group < 3; group++) {
        for (unsigned rtl = 0; rtl < 2; rtl++) {
            for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                for (unsigned inlined = 0; inlined < 2; inlined++) {
                    char values[512], declarations[640], css[680];
                    inline_declarations(values, sizeof(values), group, cases[i].css);
                    snprintf(declarations, sizeof(declarations),
                        "position:absolute;font-size:16px;direction:ltr;%s;direction:var(--dir);--dir:%s",
                        values, rtl ? "rtl" : "ltr");
                    snprintf(css, sizeof(css), "div {%s}", declarations);
                    style_case ctx;
                    setup_style_case(&ctx, inlined ? "" : css);
                    if (inlined)
                        ctx.inline_style = create_stylesheet_from_css(declarations, true, false);
                    const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, ctx.inline_style, &ctx.results);
                    const int *expected = rtl ? cases[i].rtl : cases[i].ltr;
                    expect_inline_edges(style, group, css, expected[0], expected[1]);
                    assert(style->inline_edges == NULL); /* Root is fully resolved. */
                    destroy_style_case(&ctx);
                }
            }
        }
    }
}

static void test_inline_edges_compose(void)
{
    for (unsigned group = 0; group < 3; group++) {
        for (unsigned parent_rtl = 0; parent_rtl < 2; parent_rtl++) {
            for (unsigned child_rtl = 0; child_rtl < 2; child_rtl++) {
                for (unsigned logical = 0; logical < 2; logical++) {
                    style_case p, c;
                    char edges[256], css[384];
                    inline_declarations(edges, sizeof(edges), group, "$l:11px;$r:17px");
                    snprintf(css, sizeof(css), "div {position:absolute;direction:%s;%s}", parent_rtl ? "rtl" : "ltr", edges);
                    const css_computed_style *parent = select_style_from_css(css, &p);
                    inline_declarations(edges, sizeof(edges), group, logical ? "$p:inherit" : "$p:5px;$l:inherit;$r:inherit");
                    snprintf(css, sizeof(css), "div {position:absolute;direction:%s;%s}", child_rtl ? "rtl" : "ltr", edges);
                    setup_style_case(&c, css);
                    c.node.parent = &p.node;
                    const css_computed_style *child = select_style_for_node(&c, &c.node, NULL, &c.results);
                    assert(child->inline_edges != NULL);
                    css_computed_style *composed;
                    assert(css_computed_style_compose(parent, child, &unit_ctx, &composed) == CSS_OK);
                    destroy_style_case(&c);
                    destroy_style_case(&p);
                    bool swap = logical && parent_rtl != child_rtl;
                    expect_inline_edges(composed, group, css, swap ? 17 : 11, swap ? 11 : 17);
                    assert(composed->inline_edges == NULL);
                    css_computed_style_destroy(composed);
                }
            }
        }
    }
    /* A single immutable partial style must compose under either direction,
     * even after the originating sheet and node data have been destroyed. */
    style_case c, p[2];
    setup_style_case(&c, "div {position:absolute;font-size:16px;margin-inline:calc(1em + 5%) 2px}");
    test_node dummy = {0};
    c.node.parent = &dummy;
    const css_computed_style *child = select_style_for_node(&c, &c.node, NULL, &c.results);
    css_computed_style *copy;
    assert(css__computed_style_clone(child, &copy) == CSS_OK);
    destroy_style_case(&c);
    destroy_node_data(&dummy);
    for (unsigned rtl = 0; rtl < 2; rtl++) {
        const css_computed_style *parent = select_style_from_css(rtl ? "div {direction:rtl}" : "div {direction:ltr}", &p[rtl]);
        css_computed_style *composed;
        assert(css_computed_style_compose(parent, copy, &unit_ctx, &composed) == CSS_OK);
        destroy_style_case(&p[rtl]);
        expect_inline_edges(composed, 0, "reused partial calc", rtl ? 2 : 26, rtl ? 26 : 2);
        css_computed_style_destroy(composed);
    }
    css_computed_style_destroy(copy);
}

static void test_inline_edges_revert(void)
{
    for (unsigned group = 0; group < 3; group++) {
        for (unsigned deferred = 0; deferred < 2; deferred++) {
            for (unsigned lower_logical = 0; lower_logical < 2; lower_logical++) {
                char values[256], author_css[384], user_css[256];
                inline_declarations(values, sizeof(values), group, deferred ?
                    "$p:11px 17px;$s:var(--missing,revert)" : "$p:11px 17px;$s:revert");
                snprintf(author_css, sizeof(author_css), "div {position:absolute;direction:rtl;%s}", values);
                inline_declarations(values, sizeof(values), group, lower_logical ? "$p:3px 5px" : "$l:3px;$r:5px");
                snprintf(user_css, sizeof(user_css), "div {%s}", values);
                style_case ctx;
                setup_style_case(&ctx, author_css);
                css_stylesheet *user = create_stylesheet_from_css(user_css, false, false);
                assert(css_select_ctx_insert_sheet(ctx.select_ctx, user, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
                const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
                expect_inline_edges(style, group, author_css, 17, lower_logical ? 3 : 5);
                destroy_style_case(&ctx);
                css_stylesheet_destroy(user);
            }
        }
    }
}

static void test_inline_edges_interning(void)
{
    static const struct { const char *a, *b; int left_a, right_a, left_b, right_b; } cases[] = {
        {"margin-inline-start:1px;margin-left:3px;margin-inline-end:2px",
            "margin-inline-start:7px;margin-left:3px;margin-inline-end:2px", 2,1,2,7},
        {"direction:ltr;margin-inline:inherit",
            "direction:ltr;margin-inline:inherit;margin-left:inherit;margin-right:inherit", 17,11,11,17},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        style_case p, a, b;
        const css_computed_style *parent = select_style_from_css("div {direction:rtl;margin-left:11px;margin-right:17px}", &p);
        char css[256];
        snprintf(css, sizeof(css), "div {%s}", cases[i].a);
        setup_style_case(&a, css);
        snprintf(css, sizeof(css), "div {%s}", cases[i].b);
        setup_style_case(&b, css);
        a.node.parent = b.node.parent = &p.node;
        const css_computed_style *first = select_style_for_node(&a, &a.node, NULL, &a.results);
        const css_computed_style *second = select_style_for_node(&b, &b.node, NULL, &b.results);
        /* Both the RTL values and the logical-inherit masks distinguish styles
         * whose main scalar fields would otherwise look identical. */
        assert(first != second);
        css_computed_style *out;
        assert(css_computed_style_compose(parent, first, &unit_ctx, &out) == CSS_OK);
        expect_inline_edges(out, 0, cases[i].a, cases[i].left_a, cases[i].right_a);
        css_computed_style_destroy(out);
        assert(css_computed_style_compose(parent, second, &unit_ctx, &out) == CSS_OK);
        expect_inline_edges(out, 0, cases[i].b, cases[i].left_b, cases[i].right_b);
        css_computed_style_destroy(out);
        destroy_style_case(&a);
        destroy_style_case(&b);
        destroy_style_case(&p);
    }
}

static void test_inline_edges_pseudo(void)
{
    for (unsigned group = 0; group < 3; group++) {
        for (unsigned rtl = 0; rtl < 2; rtl++) {
            char values[256], author[768], user[384];
            inline_declarations(values, sizeof(values), group, "$l:11px;$r:17px");
            snprintf(author, sizeof(author), "div {direction:rtl;position:absolute;%s}", values);
            size_t used = strlen(author);
            inline_declarations(values, sizeof(values), group, "$p:var(--missing,inherit)");
            snprintf(author + used, sizeof(author) - used,
                "div::before {direction:%s;position:absolute;%s}", rtl ? "rtl" : "ltr", values);
            style_case ctx;
            const css_computed_style *base = select_style_from_css(author, &ctx);
            css_computed_style *pseudo;
            assert(css_computed_style_compose(base, ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE],
                &unit_ctx, &pseudo) == CSS_OK);
            expect_inline_edges(pseudo, group, author, rtl ? 11 : 17, rtl ? 17 : 11);
            css_computed_style_destroy(pseudo);
            destroy_style_case(&ctx);

            /* Direction can itself revert. Deferred logical revert also needs
             * the pseudo's alternative snapshot from the lower origin. */
            inline_declarations(values, sizeof(values), group, "$p:3px 5px");
            snprintf(user, sizeof(user), "div::before {direction:%s;%s}", rtl ? "rtl" : "ltr", values);
            inline_declarations(values, sizeof(values), group, "$p:11px 17px;$s:var(--missing,revert)");
            snprintf(author, sizeof(author),
                "div {direction:ltr} div::before {position:absolute;direction:var(--missing,revert);%s}", values);
            setup_style_case(&ctx, author);
            css_stylesheet *sheet = create_stylesheet_from_css(user, false, false);
            assert(css_select_ctx_insert_sheet(ctx.select_ctx, sheet, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
            base = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
            assert(css_computed_style_compose(base, ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE],
                &unit_ctx, &pseudo) == CSS_OK);
            expect_inline_edges(pseudo, group, author, rtl ? 17 : 3, rtl ? 3 : 17);
            css_computed_style_destroy(pseudo);
            destroy_style_case(&ctx);
            css_stylesheet_destroy(sheet);
        }
    }
}

/* CSS Logical 1: block edges map to top/bottom in horizontal-tb for both
 * directions. Physical and logical declarations still compete in one cascade. */
static void test_block_inset_longhands(void)
{
    static const struct { const char *css; int top, bottom; bool automatic; } cases[] = {
        {"inset-block-start:11px;inset-block-end:17px", 11, 17},
        {"direction:rtl;inset-block-start:11px;inset-block-end:17px", 11, 17},
        {"inset-block:11px 17px;inset-block-start:23px", 23, 17},
        {"inset-block-start:23px;inset-block:11px 17px", 11, 17},
        {"inset-block-start:11px;top:23px;inset-block-end:17px", 23, 17},
        {"top:23px;inset-block-start:11px;inset-block-end:17px", 11, 17},
        {"inset-block-start:11px!important;top:23px;inset-block-end:17px", 11, 17},
        {"top:23px!important;inset-block:11px 17px", 23, 17},
        {"--edges:11px 17px;inset-block:var(--edges);inset-block-end:23px", 11, 23},
        {"--edge:13px;inset-block-start:calc(var(--edge) + 2px);inset-block-end:-17px", 15, -17},
        {"inset-block:11px 17px;inset-block:1px inherit", 11, 17},
        {"inset-block:11px 17px;inset-block:1px initial", 11, 17},
        {"inset-block:11px 17px;inset-block-start:initial", 0, 17, true},
        {"inset-block:11px 17px;inset-block-start:unset", 0, 17, true},
        {"inset-block:11px 17px;inset-block-start:var(--missing)", 0, 17, true},
        {"--bad:1px inherit;inset-block:11px 17px;inset-block:var(--bad);inset-block-end:17px", 0, 17, true},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (unsigned inlined = 0; inlined < 2; inlined++) {
            char declarations[512], css[520];
            snprintf(declarations, sizeof(declarations), "position:absolute;%s", cases[i].css);
            snprintf(css, sizeof(css), "div {%s}", declarations);
            style_case ctx;
            setup_style_case(&ctx, inlined ? "" : css);
            if (inlined)
                ctx.inline_style = create_stylesheet_from_css(declarations, true, false);
            const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, ctx.inline_style, &ctx.results);
            if (cases[i].automatic)
                expect_length_type(css, style, css_computed_top, CSS_TOP_AUTO);
            else
                expect_length_px(css, style, css_computed_top, CSS_TOP_SET, cases[i].top);
            expect_length_px(css, style, css_computed_bottom, CSS_BOTTOM_SET, cases[i].bottom);
            destroy_style_case(&ctx);
        }
    }
    const char *const children[] = {"inset-block:inherit", "inset-block-start:inherit;inset-block-end:inherit",
        "inset-block-start:var(--missing,inherit);inset-block-end:inherit"};
    for (size_t i = 0; i < sizeof(children) / sizeof(children[0]); i++) {
        style_case p, c;
        const css_computed_style *parent = select_style_from_css("div {position:absolute;top:11px;bottom:17px}", &p);
        char css[256];
        snprintf(css, sizeof(css), "div {position:absolute;%s}", children[i]);
        setup_style_case(&c, css);
        c.node.parent = &p.node;
        const css_computed_style *child = select_style_for_node(&c, &c.node, NULL, &c.results);
        css_computed_style *composed;
        assert(css_computed_style_compose(parent, child, &unit_ctx, &composed) == CSS_OK);
        destroy_style_case(&c);
        destroy_style_case(&p);
        expect_length_px(children[i], composed, css_computed_top, CSS_TOP_SET, 11);
        expect_length_px(children[i], composed, css_computed_bottom, CSS_BOTTOM_SET, 17);
        css_computed_style_destroy(composed);
    }
    for (unsigned deferred = 0; deferred < 2; deferred++) {
        style_case ctx;
        setup_style_case(&ctx, deferred ?
            "div {position:absolute;inset-block:11px 17px;inset-block-start:var(--missing,revert)}" :
            "div {position:absolute;inset-block:11px 17px;inset-block-start:revert}");
        css_stylesheet *user = create_stylesheet_from_css("div {top:3px;bottom:5px}", false, false);
        assert(css_select_ctx_insert_sheet(ctx.select_ctx, user, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
        const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
        expect_length_px("logical revert top", style, css_computed_top, CSS_TOP_SET, 3);
        expect_length_px("logical revert bottom", style, css_computed_bottom, CSS_BOTTOM_SET, 17);
        destroy_style_case(&ctx);
        css_stylesheet_destroy(user);
    }
}

/* CSS Text 4: collapse and wrapping have independent inherited cascades. */
static void expect_white_space(const css_computed_style *style, unsigned collapse, unsigned wrap, const char *css)
{
    if (css_computed_white_space_collapse(style) != collapse || css_computed_text_wrap_mode(style) != wrap) {
        fprintf(stderr, "wrong white-space components for %s: %u/%u, expected %u/%u\n", css,
            css_computed_white_space_collapse(style), css_computed_text_wrap_mode(style), collapse, wrap);
        assert(false);
    }
}

static void test_white_space_longhands(void)
{
    static const struct { const char *css; unsigned collapse, wrap; } cases[] = {
        {"white-space:normal", CSS_WHITE_SPACE_COLLAPSE_COLLAPSE, CSS_TEXT_WRAP_MODE_WRAP},
        {"white-space:pre", CSS_WHITE_SPACE_COLLAPSE_PRESERVE, CSS_TEXT_WRAP_MODE_NOWRAP},
        {"white-space:pre-wrap", CSS_WHITE_SPACE_COLLAPSE_PRESERVE, CSS_TEXT_WRAP_MODE_WRAP},
        {"white-space:pre-line", CSS_WHITE_SPACE_COLLAPSE_PRESERVE_BREAKS, CSS_TEXT_WRAP_MODE_WRAP},
        {"white-space:nowrap", CSS_WHITE_SPACE_COLLAPSE_COLLAPSE, CSS_TEXT_WRAP_MODE_NOWRAP},
        {"white-space:pre-line;text-wrap-mode:nowrap", CSS_WHITE_SPACE_COLLAPSE_PRESERVE_BREAKS, CSS_TEXT_WRAP_MODE_NOWRAP},
        {"white-space:pre;white-space-collapse:collapse", CSS_WHITE_SPACE_COLLAPSE_COLLAPSE, CSS_TEXT_WRAP_MODE_NOWRAP},
        {"white-space:pre;text-wrap-mode:initial", CSS_WHITE_SPACE_COLLAPSE_PRESERVE, CSS_TEXT_WRAP_MODE_WRAP},
        {"white-space:pre!important;text-wrap-mode:wrap", CSS_WHITE_SPACE_COLLAPSE_PRESERVE, CSS_TEXT_WRAP_MODE_NOWRAP},
        {"text-wrap-mode:nowrap!important;white-space:preserve-breaks", CSS_WHITE_SPACE_COLLAPSE_PRESERVE_BREAKS, CSS_TEXT_WRAP_MODE_NOWRAP},
        {"--w:pre-line;white-space:var(--w);text-wrap-mode:nowrap", CSS_WHITE_SPACE_COLLAPSE_PRESERVE_BREAKS, CSS_TEXT_WRAP_MODE_NOWRAP},
        {"--w:nowrap;white-space:pre-wrap;text-wrap-mode:var(--w)", CSS_WHITE_SPACE_COLLAPSE_PRESERVE, CSS_TEXT_WRAP_MODE_NOWRAP},
        {"--w:preserve;white-space-collapse:var(--w);white-space:normal", CSS_WHITE_SPACE_COLLAPSE_COLLAPSE, CSS_TEXT_WRAP_MODE_WRAP},
        {"white-space:pre;white-space-collapse:var(--missing)", CSS_WHITE_SPACE_COLLAPSE_COLLAPSE, CSS_TEXT_WRAP_MODE_NOWRAP},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (unsigned inlined = 0; inlined < 2; inlined++) {
            char css[512];
            snprintf(css, sizeof(css), "div {%s}", cases[i].css);
            style_case ctx;
            setup_style_case(&ctx, inlined ? "" : css);
            if (inlined)
                ctx.inline_style = create_stylesheet_from_css(cases[i].css, true, false);
            const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, ctx.inline_style, &ctx.results);
            expect_white_space(style, cases[i].collapse, cases[i].wrap, cases[i].css);
            destroy_style_case(&ctx);
        }
    }
    const char *const children[] = {"text-wrap-mode:wrap", "white-space-collapse:collapse", "white-space:inherit",
        "white-space:initial", "white-space:unset", "white-space:var(--missing)",
        "white-space:normal;white-space-collapse:var(--missing)", "white-space:normal;text-wrap-mode:var(--missing)"};
    const unsigned expected[][2] = {{2,1}, {1,2}, {2,2}, {1,1}, {2,2}, {2,2}, {2,1}, {1,2}};
    for (size_t i = 0; i < sizeof(children) / sizeof(children[0]); i++) {
        style_case p, c;
        const css_computed_style *parent = select_style_from_css("div {white-space:pre}", &p);
        char css[256];
        snprintf(css, sizeof(css), "div {%s}", children[i]);
        setup_style_case(&c, css);
        c.node.parent = &p.node;
        const css_computed_style *child = select_style_for_node(&c, &c.node, NULL, &c.results);
        css_computed_style *composed;
        assert(css_computed_style_compose(parent, child, &unit_ctx, &composed) == CSS_OK);
        destroy_style_case(&p);
        destroy_style_case(&c);
        expect_white_space(composed, expected[i][0], expected[i][1], children[i]);
        css_computed_style_destroy(composed);
    }
    for (unsigned deferred = 0; deferred < 2; deferred++) {
        style_case ctx;
        setup_style_case(&ctx, deferred ? "div {white-space:normal;white-space-collapse:var(--missing,revert)}" :
            "div {white-space:normal;white-space-collapse:revert}");
        css_stylesheet *user = create_stylesheet_from_css("div {white-space:pre}", false, false);
        assert(css_select_ctx_insert_sheet(ctx.select_ctx, user, 0, CSS_ORIGIN_USER, NULL) == CSS_OK);
        const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
        expect_white_space(style, CSS_WHITE_SPACE_COLLAPSE_PRESERVE, CSS_TEXT_WRAP_MODE_WRAP, "independent revert");
        destroy_style_case(&ctx);
        css_stylesheet_destroy(user);
    }
}

/* CSS Fonts 4 / Text Decoration 3 longhand identities share the supported
 * legacy rendering values, including substitution and cascade precedence. */
static void test_aggregate_longhand_values(void)
{
    static const struct {
        const char *declarations;
        uint8_t caps, lines;
    } cases[] = {
        {"font-variant-caps:small-caps;text-decoration-line:underline overline",
            CSS_FONT_VARIANT_SMALL_CAPS, CSS_TEXT_DECORATION_UNDERLINE | CSS_TEXT_DECORATION_OVERLINE},
        {"font-variant:small-caps;font-variant-caps:normal;text-decoration:underline;text-decoration-line:none",
            CSS_FONT_VARIANT_NORMAL, CSS_TEXT_DECORATION_NONE},
        {"font-variant-caps:small-caps!important;font-variant:normal;"
         "text-decoration-line:overline!important;text-decoration:underline",
            CSS_FONT_VARIANT_SMALL_CAPS, CSS_TEXT_DECORATION_OVERLINE},
        {"--caps:small-caps;--lines:overline;"
         "font-variant-caps:var(--caps);text-decoration-line:var(--lines)",
            CSS_FONT_VARIANT_SMALL_CAPS, CSS_TEXT_DECORATION_OVERLINE},
        {"--caps:small-caps;--lines:overline;"
         "font-variant-caps:var(--caps);font-variant:normal;"
         "text-decoration-line:var(--lines);text-decoration:underline",
            CSS_FONT_VARIANT_NORMAL, CSS_TEXT_DECORATION_UNDERLINE},
        {"--caps:small-caps;--lines:overline;"
         "font-variant:var(--caps);font-variant-caps:normal;"
         "text-decoration:var(--lines);text-decoration-line:underline",
            CSS_FONT_VARIANT_NORMAL, CSS_TEXT_DECORATION_UNDERLINE},
        {"--caps:small-caps;--lines:overline;"
         "font-variant-caps:var(--caps)!important;font-variant:normal;"
         "text-decoration-line:var(--lines)!important;text-decoration:underline",
            CSS_FONT_VARIANT_SMALL_CAPS, CSS_TEXT_DECORATION_OVERLINE},
        {"font-variant:small-caps;font-variant-caps:var(--missing);"
         "text-decoration:underline;text-decoration-line:var(--missing)",
            CSS_FONT_VARIANT_NORMAL, CSS_TEXT_DECORATION_NONE},
        {"--caps:bad;--lines:bad;font-variant:small-caps;font-variant-caps:var(--caps);"
         "text-decoration:underline;text-decoration-line:var(--lines)",
            CSS_FONT_VARIANT_NORMAL, CSS_TEXT_DECORATION_NONE},
        {"font-variant-caps:var(--missing,small-caps);text-decoration-line:var(--missing,overline)",
            CSS_FONT_VARIANT_SMALL_CAPS, CSS_TEXT_DECORATION_OVERLINE},
        {"--font:small-caps 16px serif;font:var(--font);font-variant-caps:normal;"
         "text-decoration-line:underline", CSS_FONT_VARIANT_NORMAL, CSS_TEXT_DECORATION_UNDERLINE},
        {"font-variant-caps:small-caps;font:16px serif;text-decoration-line:underline;"
         "text-decoration:var(--missing,initial)", CSS_FONT_VARIANT_NORMAL, CSS_TEXT_DECORATION_NONE},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (unsigned inlined = 0; inlined < 2; inlined++) {
            char css[512];
            snprintf(css, sizeof(css), "div {%s}", cases[i].declarations);
            style_case ctx;
            setup_style_case(&ctx, inlined ? "" : css);
            if (inlined)
                ctx.inline_style = create_stylesheet_from_css(cases[i].declarations, true, false);
            const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, ctx.inline_style, &ctx.results);
            uint8_t caps = css_computed_font_variant(style), lines = css_computed_text_decoration(style);
            if (caps != cases[i].caps || lines != cases[i].lines) {
                fprintf(stderr, "aggregate case %zu inline %u: caps %u/%u, lines %u/%u\n%s\n",
                    i, inlined, caps, cases[i].caps, lines, cases[i].lines, css);
                assert(false);
            }
            destroy_style_case(&ctx);
        }
    }
}

static void test_position_payloads(void)
{
    static const struct {
        const char *name;
        uint8_t (*get)(const css_computed_style *, css_fixed_or_calc *, css_unit *, css_fixed_or_calc *, css_unit *);
    } properties[] = {{"object-position", css_computed_object_position},
        {"background-position", css_computed_background_position}};
    for (size_t p = 0; p < sizeof(properties) / sizeof(properties[0]); p++) {
        for (unsigned important = 0; important < 2; important++) {
            style_case ctx;
            char css[256];
            /* 32768px resembles a variable-length opcode if the payload is
             * incorrectly decoded; !important must preserve both units too. */
            snprintf(css, sizeof(css), "div { %s:32768px 2%%%s; --size:37px; width:var(--size); }",
                properties[p].name, important ? "!important" : "");
            const css_computed_style *style = select_style_from_css(css, &ctx);
            expect_length_px(css, style, css_computed_width, CSS_WIDTH_SET, 37);
            css_fixed_or_calc x, y;
            css_unit xu, yu;
            properties[p].get(style, &x, &xu, &y, &yu);
            assert(x.value == INTTOFIX(32768) && xu == CSS_UNIT_PX);
            assert(y.value == INTTOFIX(2) && yu == CSS_UNIT_PCT);
            destroy_style_case(&ctx);
        }
    }
}

static void test_environment_outlives_stylesheet(void)
{
    style_case ctx;
    setup_style_case(&ctx, "html { --literal:38px; --alias:var(--literal); --label:\"owned tokens\"; }");
    attach_parent(&ctx);
    /* Inheritance must own its values independently of the original parser,
     * stylesheet declaration table, and stylesheet string table. */
    assert(css_select_ctx_remove_sheet(ctx.select_ctx, ctx.sheet) == CSS_OK);
    assert(css_stylesheet_destroy(ctx.sheet) == CSS_OK);
    ctx.sheet = create_stylesheet_from_css(
        "div { width:var(--literal); height:var(--alias); }"
        "div::before { content:var(--label); }", false, false);
    assert(css_select_ctx_append_sheet(ctx.select_ctx, ctx.sheet, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);
    const css_computed_style *style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_px("literal survives sheet", style, css_computed_width, CSS_WIDTH_SET, 38);
    expect_length_px("computed alias survives sheet", style, css_computed_height, CSS_HEIGHT_SET, 38);
    const css_computed_content_item *content;
    assert(css_computed_content(ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE], &content) == CSS_CONTENT_SET);
    assert(content[0].type == CSS_COMPUTED_CONTENT_STRING);
    assert(strcmp(lwc_string_data(content[0].data.string), "owned tokens") == 0);
    destroy_style_case(&ctx);
}

/* Repeatable selection-only workload: retain all nodes/results until measured.
 * This deliberately exercises the sibling-sharing path as well as inheritance.
 * Run in an optimized build with assertions enabled and diagnostic logs off. */
static int benchmark(unsigned elements, unsigned variables)
{
    style_case ctx;
    size_t size = (size_t)variables * 40 + 160, used = 0;
    char *css = malloc(size);
    test_node *nodes = calloc(elements, sizeof(*nodes));
    css_select_results **results = calloc(elements, sizeof(*results));
    uint64_t begin, parsed, selected;
    size_t set_size = 8, environments = 0, bindings = 0;
    css_var_context **seen;
    assert(css != NULL && nodes != NULL && results != NULL);
    used += snprintf(css + used, size - used, "html {");
    for (unsigned i = 0; i < variables; i++)
        used += snprintf(css + used, size - used, "--v%u:20px;", i);
    snprintf(css + used, size - used, variables != 0 ?
        "} div { width:var(--v0); height:var(--v0); }" :
        "} div { width:20px; height:20px; }");
    nsu_getmonotonic_ms(&begin);
    setup_style_case(&ctx, css);
    nsu_getmonotonic_ms(&parsed);
    attach_parent(&ctx);
    for (unsigned i = 0; i < elements; i++) {
        nodes[i].name = ctx.node_name;
        nodes[i].parent = &ctx.parent_node;
        nodes[i].previous = i != 0 ? &nodes[i - 1] : NULL;
        select_style_for_node(&ctx, &nodes[i], NULL, &results[i]);
    }
    nsu_getmonotonic_ms(&selected);
    expect_length_px("benchmark first", results[0]->styles[0], css_computed_width, CSS_WIDTH_SET, 20);
    expect_length_px("benchmark last", results[elements - 1]->styles[0], css_computed_width, CSS_WIDTH_SET, 20);
    while (set_size < (size_t)(elements + 1) * 2)
        set_size *= 2;
    seen = calloc(set_size, sizeof(*seen));
    assert(seen != NULL);
    for (unsigned i = 0; i <= elements; i++) {
        struct css_node_data *data = i == elements ? ctx.parent_node.libcss_node_data : nodes[i].libcss_node_data;
        css_var_context *env = data->var_ctx;
        size_t slot = ((uintptr_t)env >> 4) & (set_size - 1);
        while (seen[slot] != NULL && seen[slot] != env)
            slot = (slot + 1) & (set_size - 1);
        if (seen[slot] == NULL) {
            seen[slot] = env;
            environments++;
            bindings += env->count;
        }
    }
    printf("elements=%u variables=%u parse_ms=%llu select_ms=%llu environments=%zu bindings=%zu\n",
        elements, variables, (unsigned long long)(parsed - begin),
        (unsigned long long)(selected - parsed), environments, bindings);
    for (unsigned i = 0; i < elements; i++) {
        destroy_node_data(&nodes[i]);
        css_select_results_destroy(results[i]);
    }
    destroy_style_case(&ctx);
    free(seen);
    free(results);
    free(nodes);
    free(css);
    return 0;
}

static void assert_no_interned_strings(lwc_string *string, void *pw)
{
    UNUSED(pw);
    fprintf(stderr, "Leaked interned string: %.*s\n", (int)lwc_string_length(string), lwc_string_data(string));
    assert(false);
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--benchmark") == 0) {
        unsigned elements = (unsigned)strtoul(argv[2], NULL, 10);
        unsigned variables = (unsigned)strtoul(argv[3], NULL, 10);
        if (elements == 0 || elements > 50000 || variables > 1024)
            return EXIT_FAILURE;
        return benchmark(elements, variables);
    }
    assert(argc == 1);
    test_same_rule_custom_property_after_use();
    test_identifier_custom_property_names();
    test_later_rule_custom_property_after_use();
    test_nested_fallback_uses_final_context();
    test_var_shorthand_keeps_declaration_order();
    test_reused_custom_property_expansion();
    test_important_custom_property_wins();
    test_invalid_var_overrides_earlier_width();
    test_custom_property_inherits_from_parent_node();
    test_inherited_custom_property_keeps_parent_value();
    test_inline_style_var_resolution();
    test_imported_sheet_custom_property();
    test_variables_imported_by_literal_sheet();
    test_variable_import_prefix();
    test_unmatched_sheet_import();
    test_many_class_selectors();
    test_collection_skips_ordinary_rules();
    test_pseudo_element_uses_element_vars();
    test_literal_fallback_value();
    test_shorthand_fallback_value();
    test_variable_cycle_is_invalid();
    test_cycle_uses_property_fallback();
    test_cyclic_var_fallback_does_not_poison_later_use();
    test_custom_property_fallback_cycle_is_invalid();
    test_noncyclic_var_can_fallback_from_cycle();
    test_self_reference_in_fallback_is_cycle();
    test_inherited_invalid_value_stays_invalid();

    test_cross_generation_dependencies();
    test_pseudo_local_scopes();
    test_deferred_pseudo_variable_cascade();
    test_deferred_pseudo_variable_cycles();
    test_empty_and_invalid_values();
    test_css_wide_custom_values();
    test_custom_property_origins();
    test_token_preservation();
    test_full_cyclic_component();
    test_long_dependency_chain();
    test_expansion_limit();
    test_sharing_and_parent_reselection();
    test_nested_important_and_case_sensitive_names();
    test_deferred_important();
    test_property_handler_integration();
    test_grid_placement_payloads();
    test_counted_payloads();
    test_svg_paint_payloads();
    test_position_payloads();
    test_repeat_and_decoration_values();
    test_background_repeat_axes();
    test_background_position_axes();
    test_white_space_longhands();
    test_block_inset_longhands();
    test_inline_edges_cascade();
    test_inline_edges_compose();
    test_inline_edges_revert();
    test_inline_edges_interning();
    test_inline_edges_pseudo();
    test_aggregate_longhand_values();
    test_gap_aliases();
    test_deferred_revert();
    test_environment_outlives_stylesheet();

    lwc_iterate_strings(assert_no_interned_strings, NULL);
    printf("PASS\n");
    return 0;
}
