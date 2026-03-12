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
 * Parse border-width shorthand
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
css_error css__parse_border_width(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
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
		error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_BORDER_TOP_WIDTH);
		if (error != CSS_OK)
			return error;

		error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_BORDER_RIGHT_WIDTH);
		if (error != CSS_OK)
			return error;

		error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_BORDER_BOTTOM_WIDTH);
		if (error != CSS_OK)
			return error;

		error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_BORDER_LEFT_WIDTH);
		if (error == CSS_OK)
			parserutils_vector_iterate(vector, ctx);

		return error;
	}

	/* Parse up to 4 values using auto-generated value helpers.
	 * css__parse_border_side_width_value handles: thin/medium/thick
	 * keywords, calc(), plain length. */
	int32_t side_ctx[4];
	uint32_t side_count = 0;
	static const enum css_properties_e side_ops[4] = {
		CSS_PROP_BORDER_TOP_WIDTH, CSS_PROP_BORDER_RIGHT_WIDTH,
		CSS_PROP_BORDER_BOTTOM_WIDTH, CSS_PROP_BORDER_LEFT_WIDTH
	};

	while (side_count < 4) {
		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);
		if (token == NULL || token->type == CSS_TOKEN_EOF)
			break;
		if (side_count > 0 && is_css_inherit(c, token))
			break;

		side_ctx[side_count] = *ctx;
		int32_t temp_ctx = *ctx;
		error = css__parse_border_side_width_value(c, vector, &temp_ctx, result, side_ops[side_count]);
		if (error != CSS_OK) {
			if (side_count == 0) {
				*ctx = orig_ctx;
				return error;
			}
			break;
		}
		*ctx = temp_ctx;
		side_count++;
	}

	if (side_count == 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	/* Replay missing sides per CSS box model rules */
	static const int replay_map[4][4] = {
		{-1, 0, 0, 0},   /* 1 value:  T=0, R=0, B=0, L=0 */
		{-1, -1, 0, 1},  /* 2 values: T=0, R=1, B=0, L=1 */
		{-1, -1, -1, 1}, /* 3 values: T=0, R=1, B=2, L=1 */
		{-1, -1, -1, -1}, /* 4 values: all done */
	};

	for (uint32_t i = side_count; i < 4; i++) {
		int src = replay_map[side_count - 1][i];
		if (src >= 0) {
			int32_t replay = side_ctx[src];
			error = css__parse_border_side_width_value(c, vector, &replay, result, side_ops[i]);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}
		}
	}

	return CSS_OK;
}
