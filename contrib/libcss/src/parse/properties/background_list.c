/* Background list bytecode. Licensed under the MIT License. */
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/background.h"

css_error css__background_list_append(css_style *result, css_style *items, opcode_t op, uint32_t count)
{
    css_error error;
    if (count > 1) {
        error = css__stylesheet_style_appendOPV(result, op, 0, BACKGROUND_LAYER_LIST);
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
    opcode_t op, css_prop_handler parse_item)
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
            error = css__background_list_append(result, items, op, count);
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
