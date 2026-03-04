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

/** Free a mock style created by create_block_style(). */
void destroy_mock_style(css_computed_style *s);

#endif
