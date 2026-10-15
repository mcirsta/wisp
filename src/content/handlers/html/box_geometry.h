/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_HTML_BOX_GEOMETRY_H
#define WISP_HTML_BOX_GEOMETRY_H
#include <stdbool.h>
#include <libcss/libcss.h>
struct box;
struct html_content;
struct box_rect {
    double x0, y0, x1, y1;
};

/* Transform in target coordinates, whose padding-edge origin is x,y.
 * scale converts layout pixels to target pixels. False means identity. */
bool box_transform_matrix(
    const struct box *box, const css_unit_ctx *units, double x, double y, double scale, double matrix[6]);
bool box_has_transform(const struct box *box);

/* Border-fragment bounds in layout pixels. Visual includes transforms/scroll
 * and is viewport-relative; offset bounds ignore both. Empty elements return
 * their first fragment, while an absent box has a zero rectangle. */
void box_geometry_bounds(struct html_content *html, struct box *box, bool visual, struct box_rect *bounds);
#endif
