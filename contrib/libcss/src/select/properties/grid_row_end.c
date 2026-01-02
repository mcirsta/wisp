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

css_error css__cascade_grid_row_end(uint32_t opv,
				    css_style *style,
				    css_select_state *state)
{
	uint16_t value = CSS_GRID_LINE_INHERIT;
	css_fixed integer = 0;

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case CSS_GRID_LINE_AUTO:
			value = CSS_GRID_LINE_AUTO;
			break;
		case CSS_GRID_LINE_SET:
			value = CSS_GRID_LINE_SET;
			integer = *((css_fixed *)style->bytecode);
			advance_bytecode(style, sizeof(integer));
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv),
				   isImportant(opv),
				   state,
				   getFlagValue(opv))) {
		return set_grid_row_end(state->computed, value, integer);
	}

	return CSS_OK;
}

css_error
css__set_grid_row_end_from_hint(const css_hint *hint, css_computed_style *style)
{
	return set_grid_row_end(style, hint->status, hint->data.integer);
}

css_error css__initial_grid_row_end(css_select_state *state)
{
	return set_grid_row_end(state->computed, CSS_GRID_LINE_AUTO, 0);
}

css_error
css__copy_grid_row_end(const css_computed_style *from, css_computed_style *to)
{
	css_fixed integer;
	uint8_t type;

	if (from == to) {
		return CSS_OK;
	}

	type = get_grid_row_end(from, &integer);
	return set_grid_row_end(to, type, integer);
}

css_error css__compose_grid_row_end(const css_computed_style *parent,
				    const css_computed_style *child,
				    css_computed_style *result)
{
	css_fixed integer;
	uint8_t type = get_grid_row_end(child, &integer);

	return css__copy_grid_row_end(
		type == CSS_GRID_LINE_INHERIT ? parent : child, result);
}
