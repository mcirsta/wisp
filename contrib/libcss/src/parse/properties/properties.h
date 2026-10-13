/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_css__parse_properties_properties_h_
#define css_css__parse_properties_properties_h_

#include "lex/lex.h"
#include "parse/language.h"
#include "parse/propstrings.h"
#include "stylesheet.h"

/**
 * Type of property handler function
 */
typedef css_error (*css_prop_handler)(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result);

/* Note: Property handlers are now accessed via prop_hash_table.inc in language.c */

/* Auto-generated parse function declarations for all properties */
#include "parse_declarations.inc"

/* Internal helpers not in properties.gen (non-standard signatures) */
css_error css__parse_grid_template_internal(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e id);
css_error css__parse_grid_line_internal(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e id);

/** Mapping from property bytecode index to bytecode unit class mask. */
extern const uint32_t property_unit_mask[CSS_N_PROPERTIES];

/* Auto-generated unit mask definitions */
#include "unit_masks.inc"

/* Backward-compatible shared aliases used by manual parsers */
#define UNIT_MASK_BORDER_SIDE_COLOR (0)
#define UNIT_MASK_BORDER_SIDE_STYLE (0)
#define UNIT_MASK_BORDER_SIDE_WIDTH (UNIT_LENGTH)
#define UNIT_MASK_MARGIN_SIDE (UNIT_LENGTH | UNIT_PCT)
#define UNIT_MASK_PADDING_SIDE (UNIT_LENGTH | UNIT_PCT)

#endif
