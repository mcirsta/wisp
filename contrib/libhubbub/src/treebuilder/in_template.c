/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdlib.h>
#include "treebuilder/internal.h"

/* HTML's stack of template insertion modes is independent of open elements:
 * a template fragment context contributes a mode but no open template node. */
hubbub_error push_template_mode(hubbub_treebuilder *tb)
{
    if (tb->context.template_count == tb->context.template_alloc) {
        size_t capacity = tb->context.template_alloc ? tb->context.template_alloc * 2 : 8;
        if (capacity < tb->context.template_alloc || capacity > SIZE_MAX / sizeof(insertion_mode))
            return HUBBUB_NOMEM;
        insertion_mode *modes = realloc(tb->context.template_modes, capacity * sizeof(*modes));
        if (modes == NULL)
            return HUBBUB_NOMEM;
        tb->context.template_modes = modes;
        tb->context.template_alloc = capacity;
    }
    tb->context.template_modes[tb->context.template_count++] = IN_TEMPLATE;
    return HUBBUB_OK;
}

static uint32_t last_template(hubbub_treebuilder *tb)
{
    for (uint32_t i = tb->context.current_node; i != 0; i--) {
        if (tb->context.element_stack[i].ns == HUBBUB_NS_HTML && tb->context.element_stack[i].type == TEMPLATE)
            return i;
    }
    return 0;
}

hubbub_error start_template(hubbub_treebuilder *tb, const hubbub_token *token)
{
    hubbub_error err = push_template_mode(tb);
    if (err != HUBBUB_OK)
        return err;
    err = insert_element(tb, &token->data.tag, true);
    if (err != HUBBUB_OK) {
        tb->context.template_count--;
        return err;
    }
    void *node = tb->context.element_stack[tb->context.current_node].node;
    /* Existing formatting markers retain their associated scoping element. */
    tb->tree_handler->ref_node(tb->tree_handler->ctx, node);
    err = formatting_list_append(tb, HUBBUB_NS_HTML, TEMPLATE, node, tb->context.current_node);
    if (err != HUBBUB_OK) {
        tb->tree_handler->unref_node(tb->tree_handler->ctx, node);
        return err;
    }
    tb->context.frameset_ok = false;
    tb->context.mode = IN_TEMPLATE;
    return HUBBUB_OK;
}

hubbub_error end_template(hubbub_treebuilder *tb)
{
    uint32_t index = last_template(tb);
    if (index == 0)
        return HUBBUB_OK;
    /* Thorough implied-end-tag generation and then popping to template have
     * the same final stack here; no per-pop DOM actions depend on that split. */
    while (tb->context.current_node >= index) {
        hubbub_ns ns;
        element_type type;
        void *node;
        element_stack_pop(tb, &ns, &type, &node);
        tb->tree_handler->unref_node(tb->tree_handler->ctx, node);
    }
    clear_active_formatting_list_to_marker(tb);
    assert(tb->context.template_count != 0);
    tb->context.template_count--;
    reset_insertion_mode(tb);
    return HUBBUB_OK;
}

/* https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-intemplate */
hubbub_error handle_in_template(hubbub_treebuilder *tb, const hubbub_token *token)
{
    switch (token->type) {
    case HUBBUB_TOKEN_CHARACTER:
    case HUBBUB_TOKEN_COMMENT:
    case HUBBUB_TOKEN_DOCTYPE:
        return handle_in_body(tb, token);
    case HUBBUB_TOKEN_END_TAG:
        if (element_type_from_name(tb, &token->data.tag.name) == TEMPLATE)
            return end_template(tb);
        return HUBBUB_OK;
    case HUBBUB_TOKEN_EOF:
        if (last_template(tb) == 0)
            return HUBBUB_OK; /* Fragment's context is not an open element. */
        end_template(tb);
        return HUBBUB_REPROCESS;
    case HUBBUB_TOKEN_START_TAG:
        break;
    }
    insertion_mode mode;
    switch (element_type_from_name(tb, &token->data.tag.name)) {
    case BASE:
    case BASEFONT:
    case BGSOUND:
    case LINK:
    case META:
    case NOFRAMES:
    case SCRIPT:
    case STYLE:
    case TEMPLATE:
    case TITLE:
        return handle_in_head(tb, token);
    case CAPTION:
    case COLGROUP:
    case TBODY:
    case TFOOT:
    case THEAD:
        mode = IN_TABLE;
        break;
    case COL:
        mode = IN_COLUMN_GROUP;
        break;
    case TR:
        mode = IN_TABLE_BODY;
        break;
    case TD:
    case TH:
        mode = IN_ROW;
        break;
    default:
        mode = IN_BODY;
        break;
    }
    assert(tb->context.template_count != 0);
    tb->context.template_modes[tb->context.template_count - 1] = mode;
    tb->context.mode = mode;
    return HUBBUB_REPROCESS;
}
