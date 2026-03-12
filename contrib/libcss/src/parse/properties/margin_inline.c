/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse margin-inline shorthand
 *
 * \param c	  Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx	  Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_margin_inline(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	css_error error;
	enum flag_value flag_value;

	/* Firstly, handle inherit */
	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);

	if (flag_value != FLAG_VALUE__NONE) {
		error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_MARGIN_LEFT);
		if (error != CSS_OK)
			return error;

		error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_MARGIN_RIGHT);
		if (error == CSS_OK)
			parserutils_vector_iterate(vector, ctx);

		return error;
	}

	/* Parse first value (margin-left) via auto-generated value helper.
	 * Handles: auto keyword, calc(), plain length/percentage. */
	int32_t first_ctx = *ctx;
	error = css__parse_margin_side_value(c, vector, ctx, result, CSS_PROP_MARGIN_LEFT);
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		return error;
	}

	int32_t after_first = *ctx;
	consumeWhitespace(vector, ctx);

	/* Try to parse second value (margin-right) */
	token = parserutils_vector_peek(vector, *ctx);
	if (token != NULL && token->type != CSS_TOKEN_EOF) {
		int32_t temp_ctx = *ctx;
		error = css__parse_margin_side_value(c, vector, &temp_ctx, result, CSS_PROP_MARGIN_RIGHT);
		if (error == CSS_OK) {
			*ctx = temp_ctx;
			return CSS_OK;
		}
	}

	/* Only one value: replay for margin-right */
	int32_t replay = first_ctx;
	error = css__parse_margin_side_value(c, vector, &replay, result, CSS_PROP_MARGIN_RIGHT);
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		return error;
	}
	*ctx = after_first;
	return CSS_OK;
}
