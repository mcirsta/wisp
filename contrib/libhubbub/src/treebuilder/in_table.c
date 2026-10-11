/*
 * This file is part of Hubbub.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 Andrew Sidwell <takkaria@netsurf-browser.org>
 */

#include <assert.h>
#include <string.h>

#include "utils/string.h"
#include "utils/utils.h"
#include "treebuilder/internal.h"
#include "treebuilder/modes.h"
#include "treebuilder/treebuilder.h"


/**
 * Clear the stack back to a table context: "the UA must, while the current
 * node is not a table, template or html element, pop elements from the stack
 * of open elements."
 */
static inline void clear_stack_table_context(hubbub_treebuilder *treebuilder)
{
    hubbub_ns ns;
    element_type type = current_node(treebuilder);
    void *node;

    while (type != TABLE && type != TEMPLATE && type != HTML) {
        element_stack_pop(treebuilder, &ns, &type, &node);

        treebuilder->tree_handler->unref_node(treebuilder->tree_handler->ctx, node);

        type = current_node(treebuilder);
    }
}


/**
 * Process an input start tag in the "in table" insertion mode.
 */
static inline hubbub_error process_input_in_table(hubbub_treebuilder *treebuilder, const hubbub_token *token)
{
    for (size_t i = 0; i < token->data.tag.n_attributes; i++) {
        const hubbub_attribute *attr = &token->data.tag.attributes[i];
        if (!hubbub_string_match(attr->name.ptr, attr->name.len, (const uint8_t *)"type", SLEN("type")))
            continue;
        if (!hubbub_string_match_ci(attr->value.ptr, attr->value.len, (const uint8_t *)"hidden", SLEN("hidden")))
            return HUBBUB_REPROCESS;

        /* "In table" handles hidden inputs even with an open select. They are
         * void elements, and must neither remain on the stack nor be inserted
         * again because another attribute happens to have the value hidden. */
        return insert_element(treebuilder, &token->data.tag, false);
    }
    return HUBBUB_REPROCESS;
}


/**
 * Handle token in "in table" insertion mode
 *
 * \param treebuilder  The treebuilder instance
 * \param token        The token to handle
 * \return True to reprocess token, false otherwise
 */
hubbub_error handle_in_table(hubbub_treebuilder *treebuilder, const hubbub_token *token)
{
    hubbub_error err = HUBBUB_OK;
    bool handled = true;

    switch (token->type) {
    case HUBBUB_TOKEN_CHARACTER: {
        element_context *current = &treebuilder->context.element_stack[treebuilder->context.current_node];
        if (current->ns == HUBBUB_NS_HTML && (current->type == TABLE || current->type == TBODY ||
            current->type == TFOOT || current->type == THEAD || current->type == TR)) {
            treebuilder->context.table_text_mode = treebuilder->context.mode;
            treebuilder->context.mode = IN_TABLE_TEXT;
            return handle_in_table_text(treebuilder, token);
        }
        handled = false;
        break;
    }
    case HUBBUB_TOKEN_COMMENT:
        err = process_comment_append(
            treebuilder, token, treebuilder->context.element_stack[treebuilder->context.current_node].node);
        break;
    case HUBBUB_TOKEN_DOCTYPE:
        /** \todo parse error */
        break;
    case HUBBUB_TOKEN_START_TAG: {
        element_type type = element_type_from_name(treebuilder, &token->data.tag.name);

        if (type == CAPTION) {
            clear_stack_table_context(treebuilder);

            treebuilder->tree_handler->ref_node(treebuilder->tree_handler->ctx,
                treebuilder->context.element_stack[treebuilder->context.current_node].node);

            err = formatting_list_append(treebuilder, token->data.tag.ns, type,
                treebuilder->context.element_stack[treebuilder->context.current_node].node,
                treebuilder->context.current_node);
            if (err != HUBBUB_OK) {
                treebuilder->tree_handler->unref_node(treebuilder->tree_handler->ctx,
                    treebuilder->context.element_stack[treebuilder->context.current_node].node);

                return err;
            }

            err = insert_element(treebuilder, &token->data.tag, true);
            if (err != HUBBUB_OK) {
                hubbub_ns ns;
                element_type type;
                void *node;
                uint32_t index;

                formatting_list_remove(
                    treebuilder, treebuilder->context.formatting_list_end, &ns, &type, &node, &index);

                treebuilder->tree_handler->unref_node(treebuilder->tree_handler->ctx, node);

                return err;
            }

            treebuilder->context.mode = IN_CAPTION;
        } else if (type == COLGROUP || type == COL) {
            hubbub_error e;
            hubbub_tag tag = token->data.tag;

            if (type == COL) {
                /* Insert colgroup and reprocess */
                tag.name.ptr = (const uint8_t *)"colgroup";
                tag.name.len = SLEN("colgroup");
                tag.n_attributes = 0;
                tag.attributes = NULL;

                err = HUBBUB_REPROCESS;
            }

            clear_stack_table_context(treebuilder);

            e = insert_element(treebuilder, &tag, true);
            if (e != HUBBUB_OK)
                return e;

            treebuilder->context.mode = IN_COLUMN_GROUP;
        } else if (type == TBODY || type == TFOOT || type == THEAD || type == TD || type == TH || type == TR) {
            hubbub_error e;
            hubbub_tag tag = token->data.tag;

            if (type == TD || type == TH || type == TR) {
                /* Insert tbody and reprocess */
                tag.name.ptr = (const uint8_t *)"tbody";
                tag.name.len = SLEN("tbody");
                tag.n_attributes = 0;
                tag.attributes = NULL;

                err = HUBBUB_REPROCESS;
            }

            clear_stack_table_context(treebuilder);

            e = insert_element(treebuilder, &tag, true);
            if (e != HUBBUB_OK)
                return e;

            treebuilder->context.mode = IN_TABLE_BODY;
        } else if (type == TABLE) {
            /** \todo parse error */

            if (!element_in_scope(treebuilder, TABLE, SCOPE_TABLE))
                break;

            /* This should match "</table>" handling */
            element_stack_pop_until(treebuilder, TABLE);

            reset_insertion_mode(treebuilder);

            err = HUBBUB_REPROCESS;
        } else if (type == STYLE || type == SCRIPT || type == TEMPLATE) {
            err = handle_in_head(treebuilder, token);
        } else if (type == FORM) {
            if (treebuilder->context.template_count != 0 || treebuilder->context.form_element != NULL)
                return HUBBUB_OK;
            err = insert_element(treebuilder, &token->data.tag, true);
            if (err == HUBBUB_OK) {
                hubbub_ns ns;
                element_type popped_type;
                void *node;
                element_stack_pop(treebuilder, &ns, &popped_type, &node);
                treebuilder->context.form_element = node; /* Transfer stack reference. */
            }
        } else if (type == INPUT) {
            err = process_input_in_table(treebuilder, token);
            handled = (err != HUBBUB_REPROCESS);
        } else {
            handled = false;
        }
    } break;
    case HUBBUB_TOKEN_END_TAG: {
        element_type type = element_type_from_name(treebuilder, &token->data.tag.name);

        if (type == TEMPLATE) {
            return end_template(treebuilder);
        } else if (type == TABLE) {
            if (!element_in_scope(treebuilder, TABLE, SCOPE_TABLE))
                break;

            element_stack_pop_until(treebuilder, TABLE);

            reset_insertion_mode(treebuilder);
        } else if (type == BODY || type == CAPTION || type == COL || type == COLGROUP || type == HTML ||
            type == TBODY || type == TD || type == TFOOT || type == TH || type == THEAD || type == TR) {
            /** \todo parse error */
        } else {
            handled = false;
        }
    } break;
    case HUBBUB_TOKEN_EOF:
        return handle_in_body(treebuilder, token);
    }

    if (!handled) {
        treebuilder->context.in_table_foster = true;

        /** \todo parse error */
        err = handle_in_body(treebuilder, token);

        treebuilder->context.in_table_foster = false;
    }


    return err;
}

/* Character-token boundaries depend on input chunking. Buffer the whole run
 * before deciding whether all of it is whitespace or all of it is fostered.
 * https://html.spec.whatwg.org/#parsing-main-intabletext */
hubbub_error handle_in_table_text(hubbub_treebuilder *tb, const hubbub_token *token)
{
    hubbub_treebuilder_context *ctx = &tb->context;
    if (token->type == HUBBUB_TOKEN_CHARACTER) {
        const hubbub_string *text = &token->data.character;
        if (text->len > SIZE_MAX - ctx->table_text_length)
            return HUBBUB_NOMEM;
        size_t need = ctx->table_text_length + text->len;
        if (need > ctx->table_text_alloc) {
            size_t capacity = ctx->table_text_alloc ? ctx->table_text_alloc : 128;
            while (capacity < need) {
                if (capacity > SIZE_MAX / 2) {
                    capacity = need;
                    break;
                }
                capacity *= 2;
            }
            uint8_t *buffer = realloc(ctx->table_text, capacity);
            if (buffer == NULL)
                return HUBBUB_NOMEM;
            ctx->table_text = buffer;
            ctx->table_text_alloc = capacity;
        }
        for (size_t i = 0; i < text->len; i++) {
            uint8_t ch = text->ptr[i];
            if (ch == 0)
                continue; /* Null character: parse error, ignore it. */
            ctx->table_text[ctx->table_text_length++] = ch;
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f')
                ctx->table_text_nonspace = true;
        }
        return HUBBUB_OK;
    }
    hubbub_error err = HUBBUB_OK;
    if (ctx->table_text_length != 0) {
        hubbub_token pending = {.type = HUBBUB_TOKEN_CHARACTER,
            .data.character = {ctx->table_text, ctx->table_text_length}};
        if (ctx->table_text_nonspace) {
            bool saved_foster = ctx->in_table_foster;
            ctx->in_table_foster = true;
            err = handle_in_body(tb, &pending);
            ctx->in_table_foster = saved_foster;
        } else {
            err = append_text(tb, &pending.data.character);
        }
    }
    ctx->table_text_length = 0;
    ctx->table_text_nonspace = false;
    ctx->mode = ctx->table_text_mode;
    return err == HUBBUB_OK ? HUBBUB_REPROCESS : err;
}
