#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcss/computed.h>
#include <libcss/libcss.h>

#include <wisp/utils/log.h>

/* Mock resolve functions */
static css_error resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    *abs = lwc_string_ref(rel);
    return CSS_OK;
}
static css_error resolve_font(void *pw, lwc_string *name, css_system_font *system_font)
{
    return CSS_OK;
}

int main(int argc, char **argv)
{
    css_stylesheet *sheet;
    css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT,
        .charset = "UTF-8",
        .url = "foo",
        .title = "foo",
        .allow_quirks = false,
        .inline_style = false,
        .resolve = resolve_url,
        .resolve_font = resolve_font,
        .import = NULL,
        .color = NULL,
        .font = NULL,
        .pw = NULL};

    printf("Initializing libcss...\n");
    if (css_stylesheet_create(&params, &sheet) != CSS_OK) {
        fprintf(stderr, "Failed to create stylesheet\n");
        return 1;
    }

    const char *css = "div { grid-template-columns: 1fr 1fr 1fr; }";
    printf("Parsing CSS: %s\n", css);
    if (css_stylesheet_append_data(sheet, (const uint8_t *)css, strlen(css)) != CSS_OK) {
        fprintf(stderr, "Failed to append data\n");
        return 1;
    }
    css_stylesheet_data_done(sheet);

    printf("Creating selection context...\n");
    css_select_ctx *select_ctx;
    if (css_select_ctx_create(&select_ctx) != CSS_OK) {
        fprintf(stderr, "Failed to create select ctx\n");
        return 1;
    }

    if (css_select_ctx_append_sheet(select_ctx, sheet, CSS_ORIGIN_AUTHOR, NULL) != CSS_OK) {
        fprintf(stderr, "Failed to append sheet\n");
        return 1;
    }

    printf("Selecting style...\n");
    css_select_results *results;
    css_select_handler handler = {
        .node_name = NULL,
        .node_classes = NULL,
        .node_id = NULL,
        .named_ancestor_node = NULL,
        .parent_node = NULL,
        .sibling_node = NULL,
        .node_has_name = NULL,
        .node_has_class = NULL,
        .node_has_id = NULL,
        .node_count_siblings = NULL,
        .node_is_empty = NULL,
        .node_is_link = NULL,
        .node_is_visited = NULL,
        .node_is_hover = NULL,
        .node_is_active = NULL,
        .node_is_focus = NULL,
        .node_is_enabled = NULL,
        .node_is_disabled = NULL,
        .node_is_checked = NULL,
        .node_is_target = NULL,
        .node_is_lang = NULL,
        .node_presentational_hint = NULL,
        .ua_default_for_property = NULL,
        .compute_font_size = NULL,
    };
    /* Since we have simple element selector 'div', we need minimal handler?
       Actually libcss selection requires callbacks to match selectors.
       If we use universal selector or just trust the cascade?
       But 'div' requires knowing the node name. */

    /* Simpler approach: inspect the stylesheet bytecode directly if
       possible? No, opcodes are internal.

       We need a valid select handler.
       Instead of fully implementing it, let's look at what tests do.
       Tests use css_select_style directly with a dummy handler. */

    /* For this test, verifying parsing via bytecode inspection might be
       hard without selection. However, if we use a universal selector `* {
       ... }` it matches everything. */

    css_stylesheet_destroy(sheet);

    /* Recreate with universal selector */
    css = "* { grid-template-columns: 1fr 1fr 1fr; }";
    css_stylesheet_create(&params, &sheet);
    css_stylesheet_append_data(sheet, (const uint8_t *)css, strlen(css));
    css_stylesheet_data_done(sheet);

    css_select_ctx_destroy(select_ctx);
    css_select_ctx_create(&select_ctx);
    css_select_ctx_append_sheet(select_ctx, sheet, CSS_ORIGIN_AUTHOR, NULL);

    /* We need a handler that returns true for match.
       Universal selector matches any node. */

    /* Actually, passing NULL handler might crash.
       We need dummy functions. */

    printf("Can't easily select without full DOM/Handler. Aborting selection test.\n");
    printf("Instead, I will check if logs appeared during parsing (append_data).\n");

    /* Parsing happens during append_data.
       My logs in grid_template_columns.c (Phase 3) trigger during
       CASCADE/SELECTION? Wait.
       src/select/properties/grid_template_columns.c is 'css__cascade_...'.
       This happens during SELECTION, not parsing.
       Parsing happens in src/parse/properties/grid_template_columns.c.

       I added logs to src/select/... (Phase 3) in Step 2250.
       So I WON'T see them unless I perform selection.

       So I MUST perform selection. */

    return 0;
}
