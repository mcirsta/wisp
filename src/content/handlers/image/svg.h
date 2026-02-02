/*
 * Copyright 2007-2008 James Bursa <bursa@users.sourceforge.net>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file
 * Content for image/svg (interface).
 */

#ifndef _WISP_IMAGE_SVG_H_
#define _WISP_IMAGE_SVG_H_

#include <stdbool.h>
#include <stdint.h>

#include <wisp/utils/errors.h>

nserror svg_init(void);

/* Forward declarations */
struct svgtiny_diagram;
struct rect;
struct redraw_context;
typedef uint32_t colour;

/**
 * Render a standalone SVG diagram.
 *
 * Used for inline SVG elements in HTML content.
 *
 * \param diagram  SVG diagram to render
 * \param x        Left coordinate
 * \param y        Top coordinate
 * \param width    Width to render at
 * \param height   Height to render at
 * \param clip     Clipping rectangle
 * \param ctx      Redraw context
 * \param scale    Scale factor
 * \param background_colour  Background colour
 * \param current_color  CSS 'color' property for currentColor substitution
 * \return true on success, false on error
 */
bool svg_redraw_diagram(struct svgtiny_diagram *diagram, int x, int y, int width, int height, const struct rect *clip,
    const struct redraw_context *ctx, colour background_colour, colour current_color);

#endif
