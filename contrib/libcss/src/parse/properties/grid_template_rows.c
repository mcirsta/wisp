/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2025 Neosurf Grid Support
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

#define MAX_GRID_TRACKS 32

/* Track types for internal storage during parsing */
typedef enum {
	TRACK_SIMPLE, /* Single value + unit (e.g., 100px, 1fr) */
	TRACK_MINMAX /* minmax(min, max) - needs two values */
} track_type_t;

typedef struct {
	track_type_t type;
	css_fixed value; /* For TRACK_SIMPLE: the value. For TRACK_MINMAX: min
			    value */
	uint32_t unit; /* For TRACK_SIMPLE: the unit. For TRACK_MINMAX: min unit
			*/
	css_fixed max_value; /* For TRACK_MINMAX only: max value */
	uint32_t max_unit; /* For TRACK_MINMAX only: max unit */
} grid_track_t;

/**
 * Try to parse a track sizing value (dimension, keyword, or function)
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens
 * \param ctx     Pointer to current position (will be updated on success)
 * \param track   Output track structure
 * \return CSS_OK on success, CSS_INVALID if not a valid track value
 */
static css_error parse_track_size(css_language *c,
				  const parserutils_vector *vector,
				  int32_t *ctx,
				  grid_track_t *track)
{
	int32_t orig_ctx = *ctx;
	css_error error;
	const css_token *token;
	bool match;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL) {
		return CSS_INVALID;
	}

	/* Check for keywords: auto, min-content, max-content */
	if (token->type == CSS_TOKEN_IDENT) {
		if ((lwc_string_caseless_isequal(token->idata,
						 c->strings[AUTO],
						 &match) == lwc_error_ok &&
		     match)) {
			/* 'auto' - treat as 1fr for layout purposes */
			parserutils_vector_iterate(vector, ctx);
			track->type = TRACK_SIMPLE;
			track->value = INTTOFIX(1);
			track->unit = CSS_UNIT_FR;
			return CSS_OK;
		} else if ((lwc_string_caseless_isequal(token->idata,
							c->strings[MIN_CONTENT],
							&match) ==
				    lwc_error_ok &&
			    match)) {
			parserutils_vector_iterate(vector, ctx);
			track->type = TRACK_SIMPLE;
			track->value = 0;
			track->unit = CSS_UNIT_MIN_CONTENT;
			return CSS_OK;
		} else if ((lwc_string_caseless_isequal(token->idata,
							c->strings[MAX_CONTENT],
							&match) ==
				    lwc_error_ok &&
			    match)) {
			parserutils_vector_iterate(vector, ctx);
			track->type = TRACK_SIMPLE;
			track->value = 0;
			track->unit = CSS_UNIT_MAX_CONTENT;
			return CSS_OK;
		}
	}

	/* Check for minmax() function */
	if (token->type == CSS_TOKEN_FUNCTION) {
		if ((lwc_string_caseless_isequal(token->idata,
						 c->strings[MINMAX],
						 &match) == lwc_error_ok &&
		     match)) {
			/* Parse minmax(min, max) */
			css_fixed min_value = 0, max_value = 0;
			uint32_t min_unit = CSS_UNIT_PX, max_unit = CSS_UNIT_PX;

			/* Consume 'minmax(' */
			parserutils_vector_iterate(vector, ctx);

			/* Skip whitespace */
			consumeWhitespace(vector, ctx);

			/* Parse min value */
			token = parserutils_vector_peek(vector, *ctx);
			if (token == NULL) {
				*ctx = orig_ctx;
				return CSS_INVALID;
			}

			/* Check for min-content/max-content/auto as min */
			if (token->type == CSS_TOKEN_IDENT) {
				if ((lwc_string_caseless_isequal(
					     token->idata,
					     c->strings[MIN_CONTENT],
					     &match) == lwc_error_ok &&
				     match)) {
					parserutils_vector_iterate(vector, ctx);
					min_unit = CSS_UNIT_MIN_CONTENT;
				} else if ((lwc_string_caseless_isequal(
						    token->idata,
						    c->strings[MAX_CONTENT],
						    &match) == lwc_error_ok &&
					    match)) {
					parserutils_vector_iterate(vector, ctx);
					min_unit = CSS_UNIT_MAX_CONTENT;
				} else if ((lwc_string_caseless_isequal(
						    token->idata,
						    c->strings[AUTO],
						    &match) == lwc_error_ok &&
					    match)) {
					parserutils_vector_iterate(vector, ctx);
					min_unit = CSS_UNIT_FR;
					min_value = INTTOFIX(1);
				} else {
					*ctx = orig_ctx;
					return CSS_INVALID;
				}
			} else {
				/* Try to parse as dimension */
				error = css__parse_unit_specifier(c,
								  vector,
								  ctx,
								  CSS_UNIT_PX,
								  &min_value,
								  &min_unit);
				if (error != CSS_OK) {
					*ctx = orig_ctx;
					return CSS_INVALID;
				}
			}

			/* Skip whitespace and comma */
			consumeWhitespace(vector, ctx);
			token = parserutils_vector_peek(vector, *ctx);
			if (token != NULL && tokenIsChar(token, ',')) {
				parserutils_vector_iterate(vector, ctx);
			}
			consumeWhitespace(vector, ctx);

			/* Parse max value */
			token = parserutils_vector_peek(vector, *ctx);
			if (token == NULL) {
				*ctx = orig_ctx;
				return CSS_INVALID;
			}

			/* Check for min-content/max-content/auto/fr as max */
			if (token->type == CSS_TOKEN_IDENT) {
				if ((lwc_string_caseless_isequal(
					     token->idata,
					     c->strings[MIN_CONTENT],
					     &match) == lwc_error_ok &&
				     match)) {
					parserutils_vector_iterate(vector, ctx);
					max_unit = CSS_UNIT_MIN_CONTENT;
				} else if ((lwc_string_caseless_isequal(
						    token->idata,
						    c->strings[MAX_CONTENT],
						    &match) == lwc_error_ok &&
					    match)) {
					parserutils_vector_iterate(vector, ctx);
					max_unit = CSS_UNIT_MAX_CONTENT;
				} else if ((lwc_string_caseless_isequal(
						    token->idata,
						    c->strings[AUTO],
						    &match) == lwc_error_ok &&
					    match)) {
					parserutils_vector_iterate(vector, ctx);
					max_unit = CSS_UNIT_FR;
					max_value = INTTOFIX(1);
				} else {
					*ctx = orig_ctx;
					return CSS_INVALID;
				}
			} else {
				/* Try to parse as dimension */
				error = css__parse_unit_specifier(c,
								  vector,
								  ctx,
								  CSS_UNIT_PX,
								  &max_value,
								  &max_unit);
				if (error != CSS_OK) {
					*ctx = orig_ctx;
					return CSS_INVALID;
				}
			}

			/* Skip whitespace and consume closing paren */
			consumeWhitespace(vector, ctx);
			token = parserutils_vector_iterate(vector, ctx);
			if (token == NULL || !tokenIsChar(token, ')')) {
				*ctx = orig_ctx;
				return CSS_INVALID;
			}

			/* Store minmax track */
			track->type = TRACK_MINMAX;
			track->value = min_value;
			track->unit = min_unit;
			track->max_value = max_value;
			track->max_unit = max_unit;
			return CSS_OK;
		}
	}

	/* Try to parse a simple dimension/percentage/number */
	error = css__parse_unit_specifier(c,
					  vector,
					  ctx,
					  CSS_UNIT_PX, /* default unit */
					  &track->value,
					  &track->unit);
	if (error == CSS_OK) {
		track->type = TRACK_SIMPLE;
		return CSS_OK;
	}

	return CSS_INVALID;
}

/**
 * Parse grid-template-rows
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion,
 *         CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *                 If the input is invalid, then \a *ctx remains unchanged.
 */
css_error
css__parse_grid_template_rows_internal(css_language *c,
				       const parserutils_vector *vector,
				       int32_t *ctx,
				       css_style *result,
				       enum css_properties_e id)
{
	(void)id;
	int32_t orig_ctx = *ctx;
	css_error error = CSS_OK;
	const css_token *token;
	bool match;
	grid_track_t tracks[MAX_GRID_TRACKS];
	int num_tracks = 0;

	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	/* Check for keywords first */
	if (token->type == CSS_TOKEN_IDENT) {
		if ((lwc_string_caseless_isequal(token->idata,
						 c->strings[INHERIT],
						 &match) == lwc_error_ok &&
		     match)) {
			return css_stylesheet_style_inherit(
				result, CSS_PROP_GRID_TEMPLATE_ROWS);
		} else if ((lwc_string_caseless_isequal(token->idata,
							c->strings[INITIAL],
							&match) ==
				    lwc_error_ok &&
			    match)) {
			return css__stylesheet_style_appendOPV(
				result,
				CSS_PROP_GRID_TEMPLATE_ROWS,
				0,
				GRID_TEMPLATE_NONE);
		} else if ((lwc_string_caseless_isequal(
				    token->idata, c->strings[REVERT], &match) ==
				    lwc_error_ok &&
			    match)) {
			return css_stylesheet_style_revert(
				result, CSS_PROP_GRID_TEMPLATE_ROWS);
		} else if ((lwc_string_caseless_isequal(
				    token->idata, c->strings[UNSET], &match) ==
				    lwc_error_ok &&
			    match)) {
			return css__stylesheet_style_appendOPV(
				result,
				CSS_PROP_GRID_TEMPLATE_ROWS,
				FLAG_VALUE_UNSET,
				0);
		} else if ((lwc_string_caseless_isequal(
				    token->idata, c->strings[NONE], &match) ==
				    lwc_error_ok &&
			    match)) {
			return css__stylesheet_style_appendOPV(
				result,
				CSS_PROP_GRID_TEMPLATE_ROWS,
				0,
				GRID_TEMPLATE_NONE);
		}
	}

	/* Parse track list - reset context to re-parse first token */
	*ctx = orig_ctx;

	while (num_tracks < MAX_GRID_TRACKS) {
		grid_track_t track = {0};

		/* Skip whitespace (implicit in token iteration) */
		token = parserutils_vector_peek(vector, *ctx);
		if (token == NULL) {
			break; /* End of input */
		}

		/* Try to parse a track size */
		error = parse_track_size(c, vector, ctx, &track);
		if (error == CSS_OK) {
			tracks[num_tracks++] = track;
		} else {
			/* Unknown token - stop parsing */
			break;
		}
	}

	if (num_tracks == 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	/* Emit the opcode with track count */
	error = css__stylesheet_style_appendOPV(
		result, CSS_PROP_GRID_TEMPLATE_ROWS, 0, GRID_TEMPLATE_SET);
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		return error;
	}

	/* Emit number of tracks */
	error = css__stylesheet_style_append(result, num_tracks);
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		return error;
	}

	/* Emit each track's data */
	for (int i = 0; i < num_tracks; i++) {
		grid_track_t *t = &tracks[i];

		if (t->type == TRACK_SIMPLE) {
			/* Simple track: value, unit */
			error = css__stylesheet_style_append(result, t->value);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}
			error = css__stylesheet_style_append(result, t->unit);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}
		} else {
			/* Minmax track: encode as CSS_UNIT_MINMAX marker,
			 * followed by min value, min unit, max value, max unit
			 */
			/* First emit a marker value (0) and the MINMAX unit */
			error = css__stylesheet_style_append(result, 0);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}
			error = css__stylesheet_style_append(result,
							     CSS_UNIT_MINMAX);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}
			/* Then emit min value and unit */
			error = css__stylesheet_style_append(result, t->value);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}
			error = css__stylesheet_style_append(result, t->unit);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}
			/* Then emit max value and unit */
			error = css__stylesheet_style_append(result,
							     t->max_value);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}
			error = css__stylesheet_style_append(result,
							     t->max_unit);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}
		}
	}

	return CSS_OK;
}
