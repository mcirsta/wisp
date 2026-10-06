/*
 * Copyright 2014 Vincent Sanders <vince@netsurf-browser.org>
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

/**
 * \file
 * Internal font handling interfaces.
 *
 * These functions provide font related services. They all work on
 * UTF-8 strings with lengths given.
 */

#ifndef WISP_HTML_FONT_H
#define WISP_HTML_FONT_H

struct html_content;
struct plot_font_style;
struct gui_layout_table;

/** Baseline offset within a CSS line-height, including half-leading. */
int font_baseline(const struct gui_layout_table *font_func, const struct plot_font_style *fstyle, int line_height);

/**
 * Populate a font style using data from a computed CSS style
 *
 * \param html     Document supplying length units and web-font ownership
 * \param css      Computed style to consider
 * \param fstyle   Font style to populate
 */
void font_plot_style_from_css(
    const struct html_content *html, const css_computed_style *css, struct plot_font_style *fstyle);

#endif
