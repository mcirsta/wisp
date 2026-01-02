/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2024 CSS Grid Support
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"
#include "select/helpers.h"

css_error css__cascade_grid_template_rows(uint32_t opv,
					  css_style *style,
					  css_select_state *state)
{
	uint16_t value = CSS_GRID_TEMPLATE_INHERIT;
	css_computed_grid_track *tracks = NULL;
	int32_t n_tracks = 0;

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case GRID_TEMPLATE_NONE:
			value = CSS_GRID_TEMPLATE_NONE;
			break;
		case GRID_TEMPLATE_SET:
			/* Read track count from bytecode */
			n_tracks = *style->bytecode;
			advance_bytecode(style, sizeof(css_code_t));

			if (n_tracks > 0) {
				/* Allocate track array */
				tracks = malloc(
					sizeof(css_computed_grid_track) *
					(n_tracks + 1));
				if (tracks == NULL) {
					return CSS_NOMEM;
				}

				/* Read each track's data */
				for (int32_t i = 0; i < n_tracks; i++) {
					css_unit unit;
					/* Read unit */
					uint32_t raw_unit = *style->bytecode;
					advance_bytecode(style,
							 sizeof(css_code_t));

					if (raw_unit == CSS_UNIT_MINMAX) {
						tracks[i].unit =
							CSS_UNIT_MINMAX;

						/* Read min value */
						tracks[i].value =
							*style->bytecode;
						advance_bytecode(
							style,
							sizeof(css_code_t));

						/* Read min unit */
						tracks[i].min_unit =
							(css_unit)*style
								->bytecode;
						advance_bytecode(
							style,
							sizeof(css_code_t));

						/* Read max value */
						tracks[i].max_value =
							*style->bytecode;
						advance_bytecode(
							style,
							sizeof(css_code_t));

						/* Read max unit */
						tracks[i].max_unit =
							(css_unit)*style
								->bytecode;
						advance_bytecode(
							style,
							sizeof(css_code_t));
					} else {
						tracks[i].unit = (css_unit)
							raw_unit;
						/* min_unit, max_value, max_unit
						 * are unused */
						tracks[i].min_unit = 0;
						tracks[i].max_value = 0;
						tracks[i].max_unit = 0;
					}
				}

				/* Terminator */
				tracks[n_tracks].value = 0;
				tracks[n_tracks].unit = 0;
				tracks[n_tracks].min_unit = 0;
				tracks[n_tracks].max_value = 0;
				tracks[n_tracks].max_unit = 0;
			}
			value = CSS_GRID_TEMPLATE_SET;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv),
				   isImportant(opv),
				   state,
				   getFlagValue(opv))) {
		return set_grid_template_rows(state->computed, value, tracks);
	}

	return CSS_OK;
}

css_error css__set_grid_template_rows_from_hint(const css_hint *hint,
						css_computed_style *style)
{
	return set_grid_template_rows(style, hint->status, NULL);
}

css_error css__initial_grid_template_rows(css_select_state *state)
{
	return set_grid_template_rows(state->computed,
				      CSS_GRID_TEMPLATE_NONE,
				      NULL);
}

css_error css__copy_grid_template_rows(const css_computed_style *from,
				       css_computed_style *to)
{
	css_error error;
	css_computed_grid_track *orig = NULL;
	css_computed_grid_track *copy = NULL;
	uint8_t type;

	if (from == to) {
		return CSS_OK;
	}

	type = get_grid_template_rows(from, &orig);

	error = css__copy_grid_track_array(orig, &copy);
	if (error != CSS_OK) {
		return error;
	}

	return set_grid_template_rows(to, type, copy);
}

css_error css__compose_grid_template_rows(const css_computed_style *parent,
					  const css_computed_style *child,
					  css_computed_style *result)
{
	css_error error;
	css_computed_grid_track *orig = NULL;
	css_computed_grid_track *copy = NULL;
	uint8_t type = get_grid_template_rows(child, &orig);

	if (type == CSS_GRID_TEMPLATE_INHERIT) {
		type = get_grid_template_rows(parent, &orig);
	}

	error = css__copy_grid_track_array(orig, &copy);
	if (error != CSS_OK) {
		return error;
	}

	return set_grid_template_rows(result, type, copy);
}
