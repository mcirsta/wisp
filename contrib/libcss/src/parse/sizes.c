/* HTML source sizes. Licensed under the MIT License. */
#include <stdlib.h>
#include <libcss/sizes.h>
#include <libcss/hint.h>
#include "parse/calc.h"
#include "parse/language.h"
#include "parse/mq.h"
#include "parse/properties/utils.h"
#include "select/mq.h"
#include "utils/parserutilserror.h"

static void tokens_trim(parserutils_vector *tokens, int32_t *count, int32_t keep)
{
    while (*count > keep) {
        const css_token *token = parserutils_vector_peek(tokens, --*count);
        if (token->idata != NULL)
            lwc_string_unref(token->idata);
        parserutils_vector_remove_last(tokens);
    }
}

static css_error source_size_entry(parserutils_vector *tokens, int32_t *count, int32_t last, const css_unit_ctx *units,
    const css_media *media, double auto_width, double *pixels)
{
    if (last < 0)
        return CSS_INVALID;
    int32_t end = *count;
    while (end > last && ((const css_token *)parserutils_vector_peek(tokens, end - 1))->type == CSS_TOKEN_S)
        end--;
    const css_token *token = parserutils_vector_peek(tokens, last);
    double size;
    css_error error;
    if (token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->idata, "auto")) {
        if (auto_width < 0)
            return CSS_INVALID;
        size = auto_width;
    } else {
        error = css__calc_length(tokens, last, end, units, &size);
        if (error != CSS_OK)
            return error;
    }
    tokens_trim(tokens, count, last);
    int32_t ctx = 0;
    consumeWhitespace(tokens, &ctx);
    if (ctx < *count) {
        /* sizes accepts a condition, never a media type such as 'screen'. */
        token = parserutils_vector_peek(tokens, ctx);
        if (!tokenIsChar(token, '(') && !(token->type == CSS_TOKEN_IDENT && css__keyword_equal(token->idata, "not")))
            return CSS_INVALID;
        lwc_string **strings;
        error = css__propstrings_get(&strings);
        if (error != CSS_OK)
            return error;
        css_mq_query *query = NULL;
        error = css__mq_parse_media_list(strings, tokens, &ctx, &query);
        if (error == CSS_OK) {
            css_select_strings names = {.width = strings[WIDTH], .height = strings[HEIGHT]};
            error = css_error_from_lwc_error(
                lwc_intern_string("prefers-color-scheme", 20, &names.prefers_color_scheme));
            if (error == CSS_OK &&
                (ctx != *count || query == NULL || query->next != NULL || query->cond == NULL || query->negate_type ||
                    !mq_match_condition(query->cond, units, media, &names)))
                error = CSS_INVALID;
            if (names.prefers_color_scheme != NULL)
                lwc_string_unref(names.prefers_color_scheme);
        }
        css__mq_query_destroy(query);
        css__propstrings_unref();
        if (error != CSS_OK)
            return error;
    }
    *pixels = size;
    return CSS_OK;
}

css_error css_source_size(const uint8_t *text, size_t length, const css_unit_ctx *units, const css_media *media,
    double auto_width, double *pixels)
{
    if (units == NULL || media == NULL || pixels == NULL || (text == NULL && length != 0))
        return CSS_BADPARM;
    *pixels = (double)units->viewport_width / F_1;
    if (length == 0)
        return CSS_OK;
    /* HTML sizes uses initial font metrics, independently of author styles. */
    css_unit_ctx initial = *units;
    initial.root_style = NULL;
    initial.font_size_minimum = 0;
    initial.container_width = initial.container_height = CSS_UNIT_CTX_UNSET;
    parserutils_inputstream *stream;
    css_error error = css_error_from_parserutils_error(
        parserutils_inputstream_create("UTF-8", CSS_CHARSET_DICTATED, NULL, &stream));
    if (error != CSS_OK)
        return error;
    css_lexer *lexer = NULL;
    parserutils_vector *tokens = NULL;
    int32_t count = 0, last = -1;
    error = css_error_from_parserutils_error(parserutils_inputstream_append(stream, text, length));
    if (error == CSS_OK)
        error = css_error_from_parserutils_error(parserutils_inputstream_append(stream, NULL, 0));
    if (error == CSS_OK)
        error = css__lexer_create(stream, &lexer);
    if (error == CSS_OK)
        error = css_error_from_parserutils_error(parserutils_vector_create(sizeof(css_token), 16, &tokens));
    /* Match the calc parser's resource bound. Overdeep input simply uses the
     * default size; it must not recurse through unbounded media conditions. */
    char closing[128];
    size_t depth = 0;
    while (error == CSS_OK) {
        css_token *read;
        error = css__lexer_get_token(lexer, &read);
        if (error != CSS_OK)
            break;
        css_token token = *read;
        bool eof = token.type == CSS_TOKEN_EOF;
        token.idata = NULL;
        if (token.type < CSS_TOKEN_LAST_INTERN && token.data.data != NULL)
            error = css_error_from_lwc_error(
                lwc_intern_string((const char *)token.data.data, token.data.len, &token.idata));
        if (error != CSS_OK)
            break;
        /* CSS Syntax closes open components at EOF. */
        if (eof && depth > 0) {
            token.type = CSS_TOKEN_CHAR;
            error = css_error_from_lwc_error(lwc_intern_string(&closing[depth - 1], 1, &token.idata));
            eof = false;
        }
        if (error != CSS_OK)
            break;
        if (eof || (depth == 0 && tokenIsChar(&token, ','))) {
            if (token.idata != NULL)
                lwc_string_unref(token.idata);
            error = source_size_entry(tokens, &count, last, &initial, media, auto_width, pixels);
            if (error == CSS_OK || error == CSS_NOMEM || eof)
                break;
            error = CSS_OK;
            tokens_trim(tokens, &count, 0);
            last = -1;
            continue;
        }
        if (depth == 0 && token.type != CSS_TOKEN_S)
            last = count;
        char close = token.type == CSS_TOKEN_FUNCTION || tokenIsChar(&token, '(') ? ')'
            : tokenIsChar(&token, '[')                                            ? ']'
            : tokenIsChar(&token, '{')                                            ? '}'
                                                                                  : 0;
        if (close != 0) {
            if (depth == sizeof(closing)) {
                if (token.idata != NULL)
                    lwc_string_unref(token.idata);
                break;
            }
            closing[depth++] = close;
        } else if (depth > 0 && tokenIsChar(&token, closing[depth - 1])) {
            depth--;
        }
        error = css_error_from_parserutils_error(parserutils_vector_append(tokens, &token));
        if (error != CSS_OK) {
            if (token.idata != NULL)
                lwc_string_unref(token.idata);
            break;
        }
        count++;
    }
    if (tokens != NULL) {
        tokens_trim(tokens, &count, 0);
        parserutils_vector_destroy(tokens);
    }
    if (lexer != NULL)
        css__lexer_destroy(lexer);
    parserutils_inputstream_destroy(stream);
    return error == CSS_INVALID ? CSS_OK : error;
}
