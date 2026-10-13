/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_css__parse_properties_utils_h_
#define css_css__parse_properties_utils_h_

#include <math.h>

#include "parse/language.h"
#include "parse/properties/properties.h"
#include "stylesheet.h"

static inline bool is_css_inherit(css_language *c, const css_token *token)
{
    bool match;
    return ((token->type == CSS_TOKEN_IDENT) &&
        (lwc_string_caseless_isequal(token->idata, c->strings[INHERIT], &match) == lwc_error_ok && match));
}

static inline enum flag_value get_css_flag_value(css_language *c, const css_token *token)
{
    if (token->type == CSS_TOKEN_IDENT) {
        bool match;

        if (lwc_string_caseless_isequal(token->idata, c->strings[INHERIT], &match) == lwc_error_ok && match) {
            return FLAG_VALUE_INHERIT;
        } else if (lwc_string_caseless_isequal(token->idata, c->strings[INITIAL], &match) == lwc_error_ok && match) {
            return FLAG_VALUE_INITIAL;
        } else if (lwc_string_caseless_isequal(token->idata, c->strings[REVERT], &match) == lwc_error_ok && match) {
            return FLAG_VALUE_REVERT;
        } else if (lwc_string_caseless_isequal(token->idata, c->strings[UNSET], &match) == lwc_error_ok && match) {
            return FLAG_VALUE_UNSET;
        }
    }

    return FLAG_VALUE__NONE;
}

/* Parse a logical inline edge using its physical grammar, retaining mapping
 * identity in the OPV until the inherited direction is known. */
css_error css__parse_inline_side(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    css_style *result, enum css_properties_e op);

/* CSS Lists <counter-name>: a custom identifier other than none. */
bool css__is_counter_name(const css_token *token);

/* Shared unordered colour/style/width grammar. Entries use bytecode order,
 * which need not match the CSSOM shorthand's canonical serialization order. */
struct css_border_shorthand {
    enum css_properties_e properties[3];
    css_prop_handler handlers[3];
    uint16_t defaults[3];
};
css_error css__parse_border_shorthand(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    css_style *result, const struct css_border_shorthand *syntax);

enum border_side_e { BORDER_SIDE_TOP = 0, BORDER_SIDE_RIGHT = 1, BORDER_SIDE_BOTTOM = 2, BORDER_SIDE_LEFT = 3 };

/**
 * Parse border-{top,right,bottom,left} shorthand
 *
 * \param c	  Parsing context.
 * \param vector  Vector of tokens to process.
 * \param ctx	  Pointer to vector iteration context.
 * \param result  Result style.
 * \param side	  The side we're parsing for.
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_border_side(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum border_side_e side);

/**
 * Parse border-{top,right,bottom,left}-color
 *
 * \param c	  Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx	  Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \param op	  Opcode to parse for (encodes side)
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_border_side_color(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e op);

/**
 * Parse border-{top,right,bottom,left}-style
 *
 * \param c	  Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx	  Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \param op	  Opcode to parse for (encodes side)
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_border_side_style(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e op);


/**
 * Parse border-{top,right,bottom,left}-width
 *
 * \param c	  Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx	  Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \param op	  Opcode to parse for (encodes side)
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_border_side_width(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e op);


/**
 * Parse {top,right,bottom,left}
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param op      Opcode to parse for
 * \param result  Pointer to location to receive resulting style
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion,
 *         CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *                 If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_side(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e op);


/**
 * Parse margin-{top,right,bottom,left}
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
css_error css__parse_margin_side(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e op);

/**
 * Parse padding-{top,right,bottom,left}
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
css_error css__parse_padding_side(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e op);

css_error css__parse_list_style_type_value(css_language *c, const css_token *token, uint16_t *value);

css_error css__parse_colour_specifier(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, uint16_t *value, uint32_t *result);

/* Modern function grammars do not inherit the legacy hashless-colour quirk. */
css_error css__parse_colour_specifier_strict(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, uint16_t *value, uint32_t *result);

css_error css__parse_named_colour(css_language *c, lwc_string *data, uint32_t *result);

css_error css__parse_hash_colour(lwc_string *data, uint32_t *result);

/* Parse a CSS length, excluding percentages, resolutions and grid fractions.
 * Range checks use the numeric spelling before rounding into rendering storage. */
css_error css__parse_length(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    bool non_negative, css_fixed *length, uint32_t *unit);

css_error css__parse_unit_specifier(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    uint32_t default_unit, css_fixed *length, uint32_t *unit);

css_error css__parse_unit_keyword(const char *ptr, size_t len, uint32_t *unit);

css_error css__ident_list_or_string_to_string(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    bool (*reserved)(css_language *c, const css_token *ident), lwc_string **result);

css_error css__ident_list_to_string(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    bool (*reserved)(css_language *c, const css_token *ident), lwc_string **result);

css_error css__comma_list_to_style(css_language *c, const parserutils_vector *vector, int32_t *ctx,
    bool (*reserved)(css_language *c, const css_token *ident),
    css_code_t (*get_value)(css_language *c, const css_token *token, bool first), css_style *result);

/* Grammar and outer result limits supplied by the property generator.
 * Limits never apply inside a nested calc. */
struct css_calc_parse_options {
    uint32_t unit, unit_mask;
    double minimum, maximum;
};

/**
 * Parse a CSS calc() invocation
 *
 * Calc can generate a number of kinds of units, so we have to tell the
 * parser the kind of unit we're aiming for (e.g. UNIT_PX, UNIT_ANGLE, etc.)
 *
 * \param[in] c        Parsing context
 * \param[in] vector   Vector of tokens to process
 * \param[in] ctx      Pointer to vector iteration context
 * \param[in] result   Pointer to location to receive resulting style
 * \param[in] OPV      The CSS property we are calculating for
 * \param[in] options  Result kind, accepted units and outer property range
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion,
 *         CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *                 If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_calc(
    css_language *c, const parserutils_vector *vector, int *ctx, css_style *result, css_code_t OPV,
    const struct css_calc_parse_options *options);
#endif
