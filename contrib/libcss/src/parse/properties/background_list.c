/* Background list bytecode. Licensed under the MIT License. */
#include <assert.h>
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/background.h"

css_error css__background_list_append(css_style *result, css_style *items, uint32_t count)
{
    css_error error;
    if (count > 1) {
        error = css__stylesheet_style_appendOPV(result, getOpcode(items->bytecode[0]), 0, BACKGROUND_LAYER_LIST);
        if (error != CSS_OK)
            return error;
        /* Length includes the count word, but not the length word itself. */
        error = css__stylesheet_style_append(result, items->used + 1);
        if (error != CSS_OK)
            return error;
        error = css__stylesheet_style_append(result, count);
        if (error != CSS_OK)
            return error;
    }
    return css__stylesheet_merge_style(result, items);
}

css_error css__parse_background_list(css_language *c, const parserutils_vector *v, int32_t *ctx, css_style *result,
    css_prop_handler parse_item, css_background_list_appender append)
{
    int32_t orig = *ctx;
    uint32_t count = 1, used = result->used;
    css_style *items = NULL;
    css_error error = parse_item(c, v, ctx, result);
    if (error != CSS_OK)
        goto cleanup;
    consumeWhitespace(v, ctx);
    const css_token *token = parserutils_vector_peek(v, *ctx);
    if (token == NULL || !tokenIsChar(token, ','))
        return CSS_OK;
    if (hasFlagValue(result->bytecode[used])) {
        error = CSS_INVALID;
        goto cleanup;
    }
    error = css__stylesheet_style_create(c->sheet, &items);
    if (error != CSS_OK)
        goto cleanup;
    css_style first = *result;
    first.bytecode += used;
    first.used -= used;
    error = css__stylesheet_merge_style(items, &first);
    result->used = used;
    if (error != CSS_OK)
        goto cleanup;
    parserutils_vector_iterate(v, ctx);
    consumeWhitespace(v, ctx);
    for (;;) {
        uint32_t start = items->used;
        error = parse_item(c, v, ctx, items);
        if (error != CSS_OK)
            break;
        count++;
        consumeWhitespace(v, ctx);
        const css_token *token = parserutils_vector_peek(v, *ctx);
        bool comma = token != NULL && tokenIsChar(token, ',');
        if (hasFlagValue(items->bytecode[start])) {
            error = CSS_INVALID;
            break;
        }
        if (!comma) {
            error = append(result, items, count);
            break;
        }
        parserutils_vector_iterate(v, ctx);
        consumeWhitespace(v, ctx);
    }
cleanup:
    if (items != NULL)
        css__stylesheet_style_destroy(items);
    if (error != CSS_OK) {
        *ctx = orig;
        result->used = used;
    }
    return error;
}

static uint32_t pair_item_words(css_code_t opv)
{
    opcode_t op = getOpcode(opv);
    return !hasFlagValue(opv) && (op == CSS_PROP_BACKGROUND_POSITION_X || op == CSS_PROP_BACKGROUND_POSITION_Y) &&
        getValue(opv) == BACKGROUND_POSITION_AXIS_SET ? 3 : 1;
}

/* Position/repeat parsers emit interleaved X/Y items. Separate them into
 * independent lists, without temporary axis buffers. Numeric position items
 * own two operand words; these must never be mistaken for opcodes. */
css_error css__background_pair_append(css_style *result, css_style *items, uint32_t count)
{
    if (count == 1)
        return css__stylesheet_merge_style(result, items);
    uint32_t words[2] = {0}, offset = 0;
    for (uint32_t i = 0; i < count; i++) {
        for (unsigned axis = 0; axis < 2; axis++) {
            assert(offset < items->used);
            uint32_t size = pair_item_words(items->bytecode[offset]);
            words[axis] += size;
            offset += size;
        }
    }
    assert(offset == items->used);
    for (unsigned axis = 0; axis < 2; axis++) {
        offset = axis == 0 ? 0 : pair_item_words(items->bytecode[0]);
        css_error error = css__stylesheet_style_appendOPV(result, getOpcode(items->bytecode[offset]), 0,
            BACKGROUND_LAYER_LIST);
        if (error == CSS_OK)
            error = css__stylesheet_style_append(result, words[axis] + 1);
        if (error == CSS_OK)
            error = css__stylesheet_style_append(result, count);
        if (error != CSS_OK)
            return error;
        offset = 0;
        for (uint32_t i = 0; i < count; i++) {
            for (unsigned part = 0; part < 2; part++) {
                uint32_t size = pair_item_words(items->bytecode[offset]);
                if (part == axis) {
                    css_style item = *items;
                    item.bytecode += offset;
                    item.used = size;
                    error = css__stylesheet_merge_style(result, &item);
                    if (error != CSS_OK)
                        return error;
                }
                offset += size;
            }
        }
    }
    return CSS_OK;
}
