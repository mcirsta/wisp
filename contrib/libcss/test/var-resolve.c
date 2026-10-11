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
    test_environment_outlives_stylesheet();

    lwc_iterate_strings(assert_no_interned_strings, NULL);
    printf("PASS\n");
    return 0;
}
