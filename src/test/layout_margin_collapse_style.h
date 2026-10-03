/*
 * Copyright 2025 Wisp Contributors
 *
 * This file is part of Wisp.
 *
 * Shared header for margin collapse test style creation.
 * Separates libcss-internal style construction from Wisp-header-using tests,
 * avoiding enum name collisions between the two header sets.
 */

#ifndef LAYOUT_MARGIN_COLLAPSE_STYLE_H
#define LAYOUT_MARGIN_COLLAPSE_STYLE_H

#include <libcss/computed.h>

/**
 * Create a css_computed_style configured as a normal block element:
 *   display:block, position:static, overflow:visible,
 *   height:auto, font-size:16px, margins:0
 */
css_computed_style *create_block_style(void);

/** Use a block-level flex or grid formatting context. */
void style_set_flex_or_grid(css_computed_style *s, bool grid);

/**
 * Set CSS margins on a style (in px).
 * Pass INT_MIN for any edge to leave it unchanged.
 */
void style_set_margins(css_computed_style *s, int top, int right, int bottom, int left);

/**
 * Set CSS top border width on a style (in px). 0 = no border.
 */
void style_set_border_top(css_computed_style *s, int width);

/**
 * Set CSS top padding on a style (in px).
 */
void style_set_padding_top(css_computed_style *s, int px);

/**
 * Set CSS height on a style (in px). Pass -1 for auto.
 */
void style_set_height_px(css_computed_style *s, int px);

/** Set float clearance. */
void style_set_clear(css_computed_style *s, uint8_t clear);

/** Set width in px or percent; a negative value means auto. */
void style_set_width(css_computed_style *s, int value, bool percent);

/** Set min-width in px. */
void style_set_min_width(css_computed_style *s, int px);

/** Set left/right padding and borders to the same widths, in px. */
void style_set_horizontal_edges(css_computed_style *s, int padding, int border);

/** Set overflow-x and overflow-y to hidden. */
void style_set_overflow_hidden(css_computed_style *s);

/** Set position: absolute. */
void style_set_position_absolute(css_computed_style *s);

/** Set CSS bottom border width (in px). 0 = no border. */
void style_set_border_bottom(css_computed_style *s, int width);

/** Set CSS bottom padding (in px). */
void style_set_padding_bottom(css_computed_style *s, int px);

/** Set CSS min-height on a style (in px). 0 = auto. */
void style_set_min_height(css_computed_style *s, int px);

/** Set explicit font size, line height and vertical alignment for inline tests. */
void style_set_inline_metrics(css_computed_style *s, int font_px, int line_px, uint8_t align);

/** Free a mock style created by create_block_style(). */
void destroy_mock_style(css_computed_style *s);

#endif
