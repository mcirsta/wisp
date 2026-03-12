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
 * Parse padding shorthand
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
css_error css__parse_padding(css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum flag_value flag_value;
	css_error error;

	/* Firstly, handle inherit */
	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);

	if (flag_value != FLAG_VALUE__NONE) {
		error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_PADDING_TOP);
		if (error != CSS_OK)
			return error;

		error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_PADDING_RIGHT);
		if (error != CSS_OK)
			return error;

		error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_PADDING_BOTTOM);
		if (error != CSS_OK)
			return error;

		error = css_stylesheet_style_flag_value(result, flag_value, CSS_PROP_PADDING_LEFT);
		if (error == CSS_OK)
			parserutils_vector_iterate(vector, ctx);

		return error;
	}

	/* Parse up to 4 values using auto-generated value helpers.
	 * The helpers handle calc(), plain length/percentage, and emit
	 * bytecode directly. We save ctx positions for replay when
	 * fewer than 4 values map to 4 sides. */
	int32_t side_ctx[4];
	uint32_t side_count = 0;
	static const enum css_properties_e side_ops[4] = {
		CSS_PROP_PADDING_TOP, CSS_PROP_PADDING_RIGHT,
		CSS_PROP_PADDING_BOTTOM, CSS_PROP_PADDING_LEFT
	};

	/* Parse values, saving their start positions */
	while (side_count < 4) {
		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);
		if (token == NULL || token->type == CSS_TOKEN_EOF)
			break;
		if (side_count > 0 && is_css_inherit(c, token))
			break;

		side_ctx[side_count] = *ctx;
		int32_t temp_ctx = *ctx;
		/* Use a temporary style to test if parsing succeeds
		 * without emitting bytecode yet */
		error = css__parse_padding_side_value(c, vector, &temp_ctx, result, side_ops[side_count]);
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

	/* Now replay the missing sides according to CSS box model rules:
	 * 1 value:  T=R=B=L=v0
	 * 2 values: T=B=v0, R=L=v1
	 * 3 values: T=v0, R=L=v1, B=v2
	 * 4 values: T=v0, R=v1, B=v2, L=v3
	 *
	 * Since we already emitted side_count values for sides 0..side_count-1,
	 * we need to replay to fill the remaining sides. But we emitted bytecode
	 * for sides in order (top, right, bottom, left) and we need specific
	 * mapping. This means we need to redo the approach: parse without
	 * emitting first, then emit in the right order.
	 *
	 * Actually, we already emitted in order. Let's check what we emitted:
	 * side_count=1: emitted top. Need to re-emit v0 for right, bottom, left.
	 * side_count=2: emitted top, right. Need to re-emit v0 for bottom, v1 for left.
	 * side_count=3: emitted top, right, bottom. Need to re-emit v1 for left.
	 * side_count=4: all done.
	 */
	static const int replay_map[4][4] = {
		/* 1 value:  T=0, R=0, B=0, L=0 */ {-1, 0, 0, 0},
		/* 2 values: T=0, R=1, B=0, L=1 */ {-1, -1, 0, 1},
		/* 3 values: T=0, R=1, B=2, L=1 */ {-1, -1, -1, 1},
		/* 4 values: all done */             {-1, -1, -1, -1},
	};

	for (uint32_t i = side_count; i < 4; i++) {
		int src = replay_map[side_count - 1][i];
		if (src >= 0) {
			int32_t replay = side_ctx[src];
			error = css__parse_padding_side_value(c, vector, &replay, result, side_ops[i]);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}
		}
	}

	return CSS_OK;
}
