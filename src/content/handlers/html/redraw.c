/*
 * Copyright 2004-2008 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2004-2007 John M Bell <jmb202@ecs.soton.ac.uk>
 * Copyright 2004-2007 Richard Wilson <info@tinct.net>
 * Copyright 2005-2006 Adrian Lees <adrianl@users.sourceforge.net>
 * Copyright 2006 Rob Kendrick <rjek@netsurf-browser.org>
 * Copyright 2008 Michael Drake <tlsa@netsurf-browser.org>
 * Copyright 2009 Paul Blokus <paul_pl@users.sourceforge.net>
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
 *
 * Redrawing CONTENT_HTML implementation.
 */

#include "content/handlers/html/table.h"
#include <dom/dom.h>
#include <wisp/utils/config.h>
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libwapcaplet/libwapcaplet.h>
#include <wisp/bitmap.h>
#include <wisp/browser.h>
#include <wisp/browser_window.h>
#include <wisp/content.h>
#include <wisp/content/content.h>
#include <wisp/content/content_protected.h>
#include <wisp/content/handlers/css/utils.h>
#include <wisp/content/hlcache.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/desktop/print.h>
#include <wisp/desktop/textarea.h>
#include <wisp/layout.h>
#include <wisp/plotters.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/log.h>
#include <wisp/utils/messages.h>
#include <wisp/utils/nsoption.h>
#include <wisp/utils/utils.h>
#include "content/textsearch.h"
#include "desktop/browser_private.h"
#include "desktop/scrollbar.h"
#include "desktop/selection.h"
#include "content/handlers/html/restyle_profile.h"

#include <libcss/gradient.h>
#include <wisp/content/handlers/html/box.h>
#include <wisp/content/handlers/html/box_inspect.h>
#include <wisp/content/handlers/html/form_internal.h>
#include <wisp/content/handlers/html/private.h>
#include "content/handlers/html/box_manipulate.h"
#include "content/handlers/html/font.h"
#include "content/handlers/html/layout.h"
#include "content/handlers/html/redraw_helpers.h"
#include "content/handlers/html/stacking.h"
#include "content/handlers/html/box_sticky.h"
#include "content/handlers/image/svg.h"

bool html_redraw_debug = false;

/**
 * Determine if a box has a background that needs drawing
 *
 * \param box  Box to consider
 * \return True if box has a background, false otherwise.
 */
static bool html_redraw_box_has_background(struct box *box)
{
    if (box->background != NULL)
        return true;

    if (box->style != NULL) {
        css_color colour;
        lwc_string *url;

        /* Check for background color */
        css_computed_background_color(box->style, &colour);
        if (nscss_color_is_transparent(colour) == false)
            return true;

        /* Check for gradient background */
        for (uint32_t i = 0; i < css_computed_background_layer_count(box->style); i++) {
            uint8_t bg_type = css_computed_background_image_layer(box->style, i, &url);
            if (bg_type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT || bg_type == CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT ||
                (bg_type == CSS_BACKGROUND_IMAGE_IMAGE && url != NULL))
                return true;
        }
    }

    return false;
}

/**
 * Find the background box for a box
 *
 * \param box  Box to find background box for
 * \return Pointer to background box, or NULL if there is none
 */
static struct box *html_redraw_find_bg_box(struct box *box)
{
    /* Thanks to backwards compatibility, CSS defines the following:
     *
     * + If the box is for the root element and it has a background,
     *   use that (and then process the body box with no special case)
     * + If the box is for the root element and it has no background,
     *   then use the background (if any) from the body element as if
     *   it were specified on the root. Then, when the box for the body
     *   element is processed, ignore the background.
     * + For any other box, just use its own styling.
     */
    if (box->parent == NULL) {
        /* Root box */
        if (html_redraw_box_has_background(box))
            return box;

        /* No background on root box: consider body box, if any */
        if (box->children != NULL) {
            if (html_redraw_box_has_background(box->children))
                return box->children;
        }
    } else if (box->parent != NULL && box->parent->parent == NULL) {
        /* Body box: only render background if root has its own */
        if (html_redraw_box_has_background(box) && html_redraw_box_has_background(box->parent))
            return box;
    } else {
        /* Any other box */
        if (html_redraw_box_has_background(box))
            return box;
    }

    return NULL;
}

/**
 * Calculate render dimensions for object-fit CSS property.
 *
 * Computes the dimensions and position at which to render replaced element
 * content (images, videos) based on the object-fit and object-position properties.
 *
 * \param style           CSS computed style (for object-position)
 * \param unit_len_ctx    Length conversion context
 * \param object_fit      CSS object-fit value
 * \param box_width       Box content area width
 * \param box_height      Box content area height
 * \param intrinsic_width   Content intrinsic width
 * \param intrinsic_height  Content intrinsic height
 * \param render_width    Updated with computed render width
 * \param render_height   Updated with computed render height
 * \param offset_x        Updated with x offset within box
 * \param offset_y        Updated with y offset within box
 */
static void calculate_object_fit_dimensions(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
    uint8_t object_fit, int box_width, int box_height, int intrinsic_width, int intrinsic_height, int *render_width,
    int *render_height, int *offset_x, int *offset_y)
{
    /* Handle zero dimensions */
    if (intrinsic_width <= 0 || intrinsic_height <= 0 || box_width <= 0 || box_height <= 0) {
        *render_width = box_width;
        *render_height = box_height;
        *offset_x = 0;
        *offset_y = 0;
        return;
    }

    float box_ratio = (float)box_width / (float)box_height;
    float img_ratio = (float)intrinsic_width / (float)intrinsic_height;

    switch (object_fit) {
    case CSS_OBJECT_FIT_FILL:
    default:
        /* Stretch to fill box (default behavior) */
        *render_width = box_width;
        *render_height = box_height;
        break;

    case CSS_OBJECT_FIT_CONTAIN:
        /* Scale to fit entirely within box, preserving aspect ratio */
        if (img_ratio > box_ratio) {
            /* Image is wider - fit to width */
            *render_width = box_width;
            *render_height = (int)(box_width / img_ratio);
        } else {
            /* Image is taller - fit to height */
            *render_height = box_height;
            *render_width = (int)(box_height * img_ratio);
        }
        break;

    case CSS_OBJECT_FIT_COVER:
        /* Scale to fill entire box, preserving aspect ratio (may overflow) */
        if (img_ratio > box_ratio) {
            /* Image is wider - fit to height, overflow width */
            *render_height = box_height;
            *render_width = (int)(box_height * img_ratio);
        } else {
            /* Image is taller - fit to width, overflow height */
            *render_width = box_width;
            *render_height = (int)(box_width / img_ratio);
        }
        break;

    case CSS_OBJECT_FIT_NONE:
        /* Use intrinsic size, no scaling */
        *render_width = intrinsic_width;
        *render_height = intrinsic_height;
        break;

    case CSS_OBJECT_FIT_SCALE_DOWN:
        /* Use 'none' if intrinsic fits, else 'contain' */
        if (intrinsic_width <= box_width && intrinsic_height <= box_height) {
            *render_width = intrinsic_width;
            *render_height = intrinsic_height;
        } else {
            /* Recurse with contain */
            calculate_object_fit_dimensions(style, unit_len_ctx, CSS_OBJECT_FIT_CONTAIN, box_width, box_height,
                intrinsic_width, intrinsic_height, render_width, render_height, offset_x, offset_y);
            return;
        }
        break;
    }

    /* Calculate object-position offsets */
    css_fixed_or_calc hlength = (css_fixed_or_calc)0, vlength = (css_fixed_or_calc)0;
    css_unit hunit = CSS_UNIT_PCT, vunit = CSS_UNIT_PCT;

    if (style != NULL) {
        uint8_t type = css_computed_object_position(style, &hlength, &hunit, &vlength, &vunit);
        (void)type; /* Silence unused variable warning */
    } else {
        /* Default: 50% 50% (center center) */
        hlength.value = INTTOFIX(50);
        vlength.value = INTTOFIX(50);
    }

    /* Calculate horizontal offset */
    int available_x = box_width - *render_width;
    if (style != NULL && unit_len_ctx != NULL) {
        int px = 0;
        if (css_computed_length_to_px(style, unit_len_ctx, available_x, hlength, hunit, &px) == CSS_OK) {
            *offset_x = px;
        } else {
            *offset_x = 0;
        }
    } else {
        /* Default: 50% 50% (center center) */
        *offset_x = FIXTOINT(FDIV(FMUL(INTTOFIX(available_x), hlength.value), INTTOFIX(100)));
    }

    /* Calculate vertical offset */
    int available_y = box_height - *render_height;
    if (style != NULL && unit_len_ctx != NULL) {
        int px = 0;
        if (css_computed_length_to_px(style, unit_len_ctx, available_y, vlength, vunit, &px) == CSS_OK) {
            *offset_y = px;
        } else {
            *offset_y = 0;
        }
    } else {
        /* Default: 50% 50% (center center) */
        *offset_y = FIXTOINT(FDIV(FMUL(INTTOFIX(available_y), vlength.value), INTTOFIX(100)));
    }
}

/**
 * Redraw a short text string, complete with highlighting
 * (for selection/search)
 *
 * \param utf8_text pointer to UTF-8 text string
 * \param utf8_len  length of string, in bytes
 * \param offset    byte offset within textual representation
 * \param space     width of space that follows string (0 = no space)
 * \param fstyle    text style to use (pass text size unscaled)
 * \param x         x ordinate at which to plot text
 * \param y         y ordinate at which to plot text
 * \param clip      pointer to current clip rectangle
 * \param height    height of text string
 * \param scale     current display scale (1.0 = 100%)
 * \param excluded  exclude this text string from the selection
 * \param c         Content being redrawn.
 * \param sel       Selection context
 * \param search    Search context
 * \param ctx	    current redraw context
 * \return true iff successful and redraw should proceed
 */

static bool text_redraw(const char *utf8_text, size_t utf8_len, size_t offset, int space,
    const plot_font_style_t *fstyle, int x, int y, const struct rect *clip, int height, float scale, bool excluded,
    struct content *c, const struct selection *sel, const struct redraw_context *ctx)
{
    bool highlighted = false;
    plot_font_style_t plot_fstyle = *fstyle;
    nserror res;

    int baseline_offset = font_baseline(((html_content *)c)->font_func, fstyle, height) * scale;

    /* Need scaled text size to pass to plotters */
    plot_fstyle.size *= scale;

    /* is this box part of a selection? */
    if (!excluded && ctx->interactive == true) {
        unsigned len = utf8_len + (space ? 1 : 0);
        unsigned start_idx;
        unsigned end_idx;

        /* first try the browser window's current selection */
        if (selection_highlighted(sel, offset, offset + len, &start_idx, &end_idx)) {
            highlighted = true;
        }

        /* what about the current search operation, if any? */
        if (!highlighted && (c->textsearch.context != NULL) &&
            content_textsearch_ishighlighted(c->textsearch.context, offset, offset + len, &start_idx, &end_idx)) {
            highlighted = true;
        }

        /* \todo make search terms visible within selected text */
        if (highlighted) {
            struct rect r;
            unsigned endtxt_idx = end_idx;
            bool clip_changed = false;
            bool text_visible = true;
            int startx, endx;
            plot_style_t pstyle_fill_hback = *plot_style_fill_white;
            plot_font_style_t fstyle_hback = plot_fstyle;

            if (end_idx > utf8_len) {
                /* adjust for trailing space, not present in
                 * utf8_text */
                assert(end_idx == utf8_len + 1);
                endtxt_idx = utf8_len;
            }

            res = guit->layout->width(fstyle, utf8_text, start_idx, &startx);
            if (res != NSERROR_OK) {
                startx = 0;
            }

            res = guit->layout->width(fstyle, utf8_text, endtxt_idx, &endx);
            if (res != NSERROR_OK) {
                endx = 0;
            }

            /* is there a trailing space that should be highlighted
             * as well? */
            if (end_idx > utf8_len) {
                endx += space;
            }

            if (scale != 1.0) {
                startx *= scale;
                endx *= scale;
            }

            /* draw any text preceding highlighted portion */
            if ((start_idx > 0) &&
                (ctx->plot->text(ctx, &plot_fstyle, x, y + baseline_offset, utf8_text, start_idx) != NSERROR_OK))
                return false;

            pstyle_fill_hback.fill_colour = fstyle->foreground;

            /* highlighted portion */
            r.x0 = x + startx;
            r.y0 = y;
            r.x1 = x + endx;
            r.y1 = y + height * scale;
            res = ctx->plot->rectangle(ctx, &pstyle_fill_hback, &r);
            if (res != NSERROR_OK) {
                return false;
            }

            if (start_idx > 0) {
                int px0 = max(x + startx, clip->x0);
                int px1 = min(x + endx, clip->x1);

                if (px0 < px1) {
                    r.x0 = px0;
                    r.y0 = clip->y0;
                    r.x1 = px1;
                    r.y1 = clip->y1;
                    res = ctx->plot->clip(ctx, &r);
                    if (res != NSERROR_OK) {
                        return false;
                    }

                    clip_changed = true;
                } else {
                    text_visible = false;
                }
            }

            fstyle_hback.background = pstyle_fill_hback.fill_colour;
            fstyle_hback.foreground = colour_to_bw_furthest(pstyle_fill_hback.fill_colour);

            if (text_visible &&
                (ctx->plot->text(ctx, &fstyle_hback, x, y + baseline_offset, utf8_text, endtxt_idx) != NSERROR_OK)) {
                return false;
            }

            /* draw any text succeeding highlighted portion */
            if (endtxt_idx < utf8_len) {
                int px0 = max(x + endx, clip->x0);
                if (px0 < clip->x1) {

                    r.x0 = px0;
                    r.y0 = clip->y0;
                    r.x1 = clip->x1;
                    r.y1 = clip->y1;
                    res = ctx->plot->clip(ctx, &r);
                    if (res != NSERROR_OK) {
                        return false;
                    }

                    clip_changed = true;

                    res = ctx->plot->text(ctx, &plot_fstyle, x, y + baseline_offset, utf8_text, utf8_len);
                    if (res != NSERROR_OK) {
                        return false;
                    }
                }
            }

            if (clip_changed && (ctx->plot->clip(ctx, clip) != NSERROR_OK)) {
                return false;
            }
        }
    }

    if (!highlighted) {
        res = ctx->plot->text(ctx, &plot_fstyle, x, y + baseline_offset, utf8_text, utf8_len);
        if (res != NSERROR_OK) {
            return false;
        }
    }
    return true;
}


/**
 * Plot a checkbox.
 *
 * \param  x	     left coordinate
 * \param  y	     top coordinate
 * \param  width     dimensions of checkbox
 * \param  height    dimensions of checkbox
 * \param  selected  the checkbox is selected
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_checkbox(int x, int y, int width, int height, bool selected, const struct redraw_context *ctx)
{
    double z;
    nserror res;
    struct rect rect;

    z = width * 0.15;
    if (z == 0) {
        z = 1;
    }

    rect.x0 = x;
    rect.y0 = y;
    rect.x1 = x + width;
    rect.y1 = y + height;
    res = ctx->plot->rectangle(ctx, plot_style_fill_wbasec, &rect);
    if (res != NSERROR_OK) {
        return false;
    }

    /* dark line across top */
    rect.y1 = y;
    res = ctx->plot->line(ctx, plot_style_stroke_darkwbasec, &rect);
    if (res != NSERROR_OK) {
        return false;
    }

    /* dark line across left */
    rect.x1 = x;
    rect.y1 = y + height;
    res = ctx->plot->line(ctx, plot_style_stroke_darkwbasec, &rect);
    if (res != NSERROR_OK) {
        return false;
    }

    /* light line across right */
    rect.x0 = x + width;
    rect.x1 = x + width;
    res = ctx->plot->line(ctx, plot_style_stroke_lightwbasec, &rect);
    if (res != NSERROR_OK) {
        return false;
    }

    /* light line across bottom */
    rect.x0 = x;
    rect.y0 = y + height;
    res = ctx->plot->line(ctx, plot_style_stroke_lightwbasec, &rect);
    if (res != NSERROR_OK) {
        return false;
    }

    if (selected) {
        if (width < 12 || height < 12) {
            /* render a solid box instead of a tick */
            rect.x0 = x + z + z;
            rect.y0 = y + z + z;
            rect.x1 = x + width - z;
            rect.y1 = y + height - z;
            res = ctx->plot->rectangle(ctx, plot_style_fill_wblobc, &rect);
            if (res != NSERROR_OK) {
                return false;
            }
        } else {
            /* render a tick, as it'll fit comfortably */
            rect.x0 = x + width - z;
            rect.y0 = y + z;
            rect.x1 = x + (z * 3);
            rect.y1 = y + height - z;
            res = ctx->plot->line(ctx, plot_style_stroke_wblobc, &rect);
            if (res != NSERROR_OK) {
                return false;
            }

            rect.x0 = x + (z * 3);
            rect.y0 = y + height - z;
            rect.x1 = x + z + z;
            rect.y1 = y + (height / 2);
            res = ctx->plot->line(ctx, plot_style_stroke_wblobc, &rect);
            if (res != NSERROR_OK) {
                return false;
            }
        }
    }
    return true;
}


/**
 * Plot a radio icon.
 *
 * \param  x	     left coordinate
 * \param  y	     top coordinate
 * \param  width     dimensions of radio icon
 * \param  height    dimensions of radio icon
 * \param  selected  the radio icon is selected
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */
static bool html_redraw_radio(int x, int y, int width, int height, bool selected, const struct redraw_context *ctx)
{
    nserror res;

    /* plot background of radio button */
    res = ctx->plot->disc(ctx, plot_style_fill_wbasec, x + width * 0.5, y + height * 0.5, width * 0.5 - 1);
    if (res != NSERROR_OK) {
        return false;
    }

    /* plot dark arc */
    res = ctx->plot->arc(ctx, plot_style_fill_darkwbasec, x + width * 0.5, y + height * 0.5, width * 0.5 - 1, 45, 225);
    if (res != NSERROR_OK) {
        return false;
    }

    /* plot light arc */
    res = ctx->plot->arc(ctx, plot_style_fill_lightwbasec, x + width * 0.5, y + height * 0.5, width * 0.5 - 1, 225, 45);
    if (res != NSERROR_OK) {
        return false;
    }

    if (selected) {
        /* plot selection blob */
        res = ctx->plot->disc(ctx, plot_style_fill_wblobc, x + width * 0.5, y + height * 0.5, width * 0.3 - 1);
        if (res != NSERROR_OK) {
            return false;
        }
    }

    return true;
}


/**
 * Plot a file upload input.
 *
 * \param  x	     left coordinate
 * \param  y	     top coordinate
 * \param  width     dimensions of input
 * \param  height    dimensions of input
 * \param  box	     box of input
 * \param  scale     scale for redraw
 * \param  background_colour  current background colour
 * \param  html           Document supplying font and length context
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_file(int x, int y, int width, int height, struct box *box, float scale,
    colour background_colour, const html_content *html, const struct redraw_context *ctx)
{
    int text_width;
    const char *text;
    size_t length;
    plot_font_style_t fstyle;
    nserror res;

    font_plot_style_from_css(html, box->style, &fstyle);
    fstyle.background = background_colour;

    if (box->gadget->value) {
        text = box->gadget->value;
    } else {
        text = messages_get("Form_Drop");
    }
    length = strlen(text);

    res = guit->layout->width(&fstyle, text, length, &text_width);
    if (res != NSERROR_OK) {
        return false;
    }
    text_width *= scale;
    if (width < text_width + 8) {
        x = x + width - text_width - 4;
    } else {
        x = x + 4;
    }

    res = ctx->plot->text(ctx, &fstyle, x, y + height * 0.75, text, length);
    if (res != NSERROR_OK) {
        return false;
    }
    return true;
}


#if defined(WISP_USE_NATIVE_GRADIENTS) || defined(WISP_USE_NATIVE_RADIAL_GRADIENTS)
/* Native plotters accept stops in [0,1]. Clip the color function, not the
 * specified positions: clamping an off-image stop would change its slope. */
static unsigned int gradient_native_stops(
    const css_gradient_stop *source, const float *positions, unsigned int count, struct gradient_stop *stops)
{
    unsigned int n = 0;
    stops[n++] = (struct gradient_stop){
        .color = nscss_color_to_ns(html_gradient_color_at(source, positions, count, 0)), .offset = 0};
    for (unsigned int i = 0; i < count; i++) {
        if (positions[i] > 0 && positions[i] < 1)
            stops[n++] = (struct gradient_stop){.color = nscss_color_to_ns(source[i].color), .offset = positions[i]};
    }
    stops[n++] = (struct gradient_stop){
        .color = nscss_color_to_ns(html_gradient_color_at(source, positions, count, 1)), .offset = 1};
    /* Qt's setColorAt replaces coincident stops. Retain both sides of a hard
     * edge by separating earlier stops by one floating-point step. */
    for (unsigned int i = n - 1; i > 0; i--) {
        if (stops[i - 1].offset >= stops[i].offset && stops[i].offset > 0)
            stops[i - 1].offset = nextafterf(stops[i].offset, 0);
    }
    return n;
}
#endif


/**
 * Render a linear gradient.
 *
 * \param gradient  Gradient data from computed style
 * \param r         Rectangle to fill with gradient
 * \param scale     Current scale factor
 * \param ctx       Redraw context
 * \return true on success, false on failure
 */
static bool html_redraw_linear_gradient(const css_linear_gradient *gradient, const css_computed_style *style,
    const css_unit_ctx *unit_ctx, const struct rect *r, const struct rect *clip, float scale,
    const struct redraw_context *ctx)
{
    if (gradient == NULL || gradient->stop_count < 2)
        return true; /* Nothing to draw */

    int width = r->x1 - r->x0;
    int height = r->y1 - r->y0;
    if (width <= 0 || height <= 0)
        return true;

    bool is_vertical = (gradient->direction == CSS_GRADIENT_TO_BOTTOM || gradient->direction == CSS_GRADIENT_TO_TOP);
    int total_length = is_vertical ? height : width;
    float positions[CSS_LINEAR_GRADIENT_MAX_STOPS];
    html_gradient_positions(gradient->stops, gradient->stop_count, style, unit_ctx, total_length, scale, positions);
    ctx->plot->clip(ctx, clip);

#ifdef WISP_USE_NATIVE_GRADIENTS
    /* Native gradient rendering - compile-time selected */
    NSLOG(plot, DEBUG, "Linear gradient: Using NATIVE rendering path");

    struct gradient_stop stops[CSS_LINEAR_GRADIENT_MAX_STOPS + 2];
    unsigned int count = gradient_native_stops(gradient->stops, positions, gradient->stop_count, stops);

    /* Calculate gradient line based on direction */
    float x0, y0, x1, y1;
    switch (gradient->direction) {
    case CSS_GRADIENT_TO_BOTTOM:
        x0 = (r->x0 + r->x1) / 2.0f;
        y0 = r->y0;
        x1 = x0;
        y1 = r->y1;
        break;
    case CSS_GRADIENT_TO_TOP:
        x0 = (r->x0 + r->x1) / 2.0f;
        y0 = r->y1;
        x1 = x0;
        y1 = r->y0;
        break;
    case CSS_GRADIENT_TO_RIGHT:
        x0 = r->x0;
        y0 = (r->y0 + r->y1) / 2.0f;
        x1 = r->x1;
        y1 = y0;
        break;
    case CSS_GRADIENT_TO_LEFT:
        x0 = r->x1;
        y0 = (r->y0 + r->y1) / 2.0f;
        x1 = r->x0;
        y1 = y0;
        break;
    default:
        /* Unsupported direction - just fill with first color */
        {
            plot_style_t pstyle = {
                .fill_type = PLOT_OP_TYPE_SOLID,
                .fill_colour = nscss_color_to_ns(gradient->stops[0].color),
            };
            ctx->plot->rectangle(ctx, &pstyle, r);
            return true;
        }
    }

    /* Set clip and render with native gradient */
    if (ctx->plot->linear_gradient != NULL) {
        NSLOG(plot, DEBUG, "Linear gradient: Calling native plotter (%.1f,%.1f) to (%.1f,%.1f) with %u stops", x0, y0,
            x1, y1, gradient->stop_count);
        /* CSS gradients use clip rect, not path - pass NULL for path */
        nserror err = ctx->plot->linear_gradient(ctx, NULL, 0, NULL, x0, y0, x1, y1, stops, count);
        if (err == NSERROR_OK) {
            NSLOG(plot, DEBUG, "Linear gradient: Native plotter succeeded");
            return true;
        }
        NSLOG(plot, WARNING, "Linear gradient: Native plotter FAILED with error %d", err);
    } else {
        NSLOG(plot, WARNING, "Linear gradient: Native plotter is NULL!");
    }
#endif
    /* Native support is optional; all frontends can use the same fixed-up stops. */
    bool reversed = (gradient->direction == CSS_GRADIENT_TO_TOP || gradient->direction == CSS_GRADIENT_TO_LEFT);

    /* Draw gradient using strips */
    int num_strips = (total_length > 100) ? 100 : total_length;
    if (num_strips < 2)
        num_strips = 2;

    for (int strip = 0; strip < num_strips; strip++) {
        /* Calculate position along gradient (0.0 to 1.0) */
        float pos = ((float)strip + 0.5f) / num_strips;
        if (reversed)
            pos = 1.0f - pos;

        colour strip_color = nscss_color_to_ns(
            html_gradient_color_at(gradient->stops, positions, gradient->stop_count, pos));

        /* Calculate strip rectangle */
        struct rect strip_rect;
        int strip_start = (strip * total_length) / num_strips;
        int strip_end = ((strip + 1) * total_length) / num_strips;

        if (is_vertical) {
            strip_rect.x0 = r->x0;
            strip_rect.x1 = r->x1;
            strip_rect.y0 = r->y0 + strip_start;
            strip_rect.y1 = r->y0 + strip_end;
        } else {
            strip_rect.x0 = r->x0 + strip_start;
            strip_rect.x1 = r->x0 + strip_end;
            strip_rect.y0 = r->y0;
            strip_rect.y1 = r->y1;
        }

        /* Fill the strip */
        plot_style_t pstyle = {
            .fill_type = PLOT_OP_TYPE_SOLID,
            .fill_colour = strip_color,
        };

        nserror res = ctx->plot->rectangle(ctx, &pstyle, &strip_rect);
        if (res != NSERROR_OK)
            return false;
    }

    return true;
}

/**
 * Draw a radial gradient background
 *
 * \param gradient  The radial gradient to draw
 * \param r         Rectangle to fill with gradient
 * \param scale     Current scale
 * \param ctx       Redraw context
 * \return true on success, false on error
 */
static bool html_redraw_radial_gradient(const css_radial_gradient *gradient, const css_computed_style *style,
    const css_unit_ctx *unit_ctx, const struct rect *r, const struct rect *clip, float scale,
    const struct redraw_context *ctx)
{
    if (gradient == NULL || gradient->stop_count < 2)
        return true; /* Nothing to draw */

    /* Calculate center and dimensions */
    float cx = (r->x0 + r->x1) / 2.0f;
    float cy = (r->y0 + r->y1) / 2.0f;
    int width = r->x1 - r->x0;
    int height = r->y1 - r->y0;

    if (width <= 0 || height <= 0)
        return true;

    /* Set clip rect to contain the gradient within the box */
    ctx->plot->clip(ctx, clip);

    /* Farthest-corner sizing: gradient extends to cover entire box
     * Circle: radius = distance to corner (will be clipped)
     * Ellipse: rx/ry = half dimensions scaled by sqrt(2) */
    float rx, ry;
    if (gradient->shape == CSS_RADIAL_SHAPE_CIRCLE) {
        float half_w = width / 2.0f;
        float half_h = height / 2.0f;
        float corner_dist = sqrtf(half_w * half_w + half_h * half_h);
        rx = ry = corner_dist;
    } else {
        rx = width / 2.0f * 1.41421356f; /* sqrt(2) */
        ry = height / 2.0f * 1.41421356f;
    }

    if (rx < 1.0f || ry < 1.0f)
        return true;

    float positions[CSS_RADIAL_GRADIENT_MAX_STOPS];
    html_gradient_positions(gradient->stops, gradient->stop_count, style, unit_ctx, rx, scale, positions);

#ifdef WISP_USE_NATIVE_RADIAL_GRADIENTS
    /* Native radial gradient rendering - compile-time selected */
    NSLOG(plot, DEBUG, "Radial gradient: Using NATIVE rendering path");
    if (ctx->plot->radial_gradient != NULL) {
        struct gradient_stop stops[CSS_RADIAL_GRADIENT_MAX_STOPS + 2];
        unsigned int count = gradient_native_stops(gradient->stops, positions, gradient->stop_count, stops);

        NSLOG(plot, DEBUG, "Radial gradient: Calling native plotter (%.1f,%.1f) rx=%.1f ry=%.1f with %u stops", cx, cy,
            rx, ry, gradient->stop_count);
        /* CSS gradients use clip rect, not path - pass NULL for path */
        nserror err = ctx->plot->radial_gradient(ctx, NULL, 0, NULL, cx, cy, rx, ry, stops, count);
        if (err == NSERROR_OK) {
            NSLOG(plot, DEBUG, "Radial gradient: Native plotter succeeded");
            return true;
        }
        /* Native failed, fall through to disc rendering */
    }
#endif /* NEOSURF_USE_NATIVE_RADIAL_GRADIENTS */

    /* Fallback: Draw gradient using concentric ellipses from outside to inside */
    int max_dim = (width > height) ? width : height;
    int num_rings = (max_dim > 200) ? 100 : (max_dim > 50) ? 50 : max_dim;
    if (num_rings < 2)
        num_rings = 2;

    /* Draw from outside (ring=0) to inside (ring=num_rings-1) */
    for (int ring = 0; ring < num_rings; ring++) {
        /* ring=0 is outermost (edge), ring=num_rings-1 is innermost (center) */
        float t_ring = (float)ring / (float)(num_rings - 1);

        /* Scale factor: 1.0 at ring=0 (outer), 0.0 at ring=num_rings-1 (center) */
        float ring_scale = 1.0f - t_ring;

        int ring_rx = (int)(rx * ring_scale);
        int ring_ry = (int)(ry * ring_scale);
        if (ring_rx < 1)
            ring_rx = 1;
        if (ring_ry < 1)
            ring_ry = 1;

        /* Calculate position along gradient: 0.0 = center, 1.0 = edge */
        float pos = ring_scale;
        colour ring_color = nscss_color_to_ns(
            html_gradient_color_at(gradient->stops, positions, gradient->stop_count, pos));

        /* Draw filled ellipse for this ring */
        plot_style_t pstyle = {
            .fill_type = PLOT_OP_TYPE_SOLID,
            .fill_colour = ring_color,
        };

        /* Use disc for circles. For ellipses, use transform to stretch circle */
        if (ring_rx == ring_ry) {
            nserror res = ctx->plot->disc(ctx, &pstyle, (int)cx, (int)cy, ring_rx);
            if (res != NSERROR_OK)
                return false;
        } else {
            /* For ellipses, draw a unit circle with scaling transform */
            float scale_x = (float)ring_rx;
            float scale_y = (float)ring_ry;

            float transform[6] = {scale_x, 0.0f, 0.0f, scale_y, cx, cy};

            if (ctx->plot->push_transform) {
                ctx->plot->push_transform(ctx, transform);
                nserror res = ctx->plot->disc(ctx, &pstyle, 0, 0, 1);
                ctx->plot->pop_transform(ctx);
                if (res != NSERROR_OK)
                    return false;
            } else {
                /* Fallback: just draw a circle with max radius */
                int max_r = (ring_rx > ring_ry) ? ring_rx : ring_ry;
                nserror res = ctx->plot->disc(ctx, &pstyle, (int)cx, (int)cy, max_r);
                if (res != NSERROR_OK)
                    return false;
            }
        }
    }

    return true;
}


/* Gradients have no intrinsic dimensions: background-size:auto gives them the
 * positioning area's size. Position/repeat still apply to each generated image. */
static bool html_redraw_gradient_layer(const css_computed_style *style, uint32_t layer, const struct rect *area,
    const struct rect *clip, float scale, const css_unit_ctx *units, const struct redraw_context *ctx)
{
    const css_linear_gradient *linear = css_computed_background_gradient_layer(style, layer);
    const css_radial_gradient *radial = css_computed_background_radial_gradient_layer(style, layer);
    if (linear == NULL && radial == NULL)
        return true;
    int width = area->x1 - area->x0, height = area->y1 - area->y0;
    if (width <= 0 || height <= 0 || clip->x0 >= clip->x1 || clip->y0 >= clip->y1)
        return true;
    css_fixed_or_calc x = {0}, y = {0};
    css_unit xu = CSS_UNIT_PX, yu = CSS_UNIT_PX;
    css_computed_background_position_layer(style, layer, &x, &xu, &y, &yu);
    int dx = 0, dy = 0;
    if (xu == CSS_UNIT_CALC)
        css_computed_length_to_px(style, units, 0, x, xu, &dx);
    else if (xu != CSS_UNIT_PCT)
        dx = FIXTOINT(css_unit_len2device_px(style, units, x.value, xu));
    if (yu == CSS_UNIT_CALC)
        css_computed_length_to_px(style, units, 0, y, yu, &dy);
    else if (yu != CSS_UNIT_PCT)
        dy = FIXTOINT(css_unit_len2device_px(style, units, y.value, yu));
    uint8_t repeat = css_computed_background_repeat_layer(style, layer);
    bool repeat_x = repeat == CSS_BACKGROUND_REPEAT_REPEAT || repeat == CSS_BACKGROUND_REPEAT_REPEAT_X;
    bool repeat_y = repeat == CSS_BACKGROUND_REPEAT_REPEAT || repeat == CSS_BACKGROUND_REPEAT_REPEAT_Y;
    int64_t start_x = area->x0 + (int64_t)(dx * scale), start_y = area->y0 + (int64_t)(dy * scale);
    if (repeat_x)
        start_x += (int64_t)floor((double)(clip->x0 - start_x) / width) * width;
    if (repeat_y)
        start_y += (int64_t)floor((double)(clip->y0 - start_y) / height) * height;
    for (int64_t ty = start_y; ty < clip->y1; ty += height) {
        for (int64_t tx = start_x; tx < clip->x1; tx += width) {
            struct rect tile = {tx, ty, tx + width, ty + height};
            struct rect r = {((tile.x0 > clip->x0) ? tile.x0 : clip->x0), ((tile.y0 > clip->y0) ? tile.y0 : clip->y0),
                ((tile.x1 < clip->x1) ? tile.x1 : clip->x1), ((tile.y1 < clip->y1) ? tile.y1 : clip->y1)};
            if (r.x0 < r.x1 && r.y0 < r.y1) {
                bool ok = linear != NULL ? html_redraw_linear_gradient(linear, style, units, &tile, &r, scale, ctx)
                                         : html_redraw_radial_gradient(radial, style, units, &tile, &r, scale, ctx);
                if (!ok)
                    return false;
            }
            if (!repeat_x)
                break;
        }
        if (!repeat_y)
            break;
    }
    return true;
}

static bool html_redraw_background_layer(int x, int y, struct box *box, float scale, const struct rect *clip,
    colour *background_colour, struct box *background, const css_unit_ctx *unit_len_ctx,
    const struct redraw_context *ctx, uint32_t layer, bool paint_colour)
{
    struct hlcache_handle *image = box_background_layer(background, layer);
    bool repeat_x = false;
    bool repeat_y = false;
    bool plot_colour = paint_colour;
    bool plot_content;
    bool clip_to_children = false;
    struct box *clip_box = box;
    int ox = x, oy = y;
    int width, height;
    css_fixed_or_calc hpos = (css_fixed_or_calc)0, vpos = (css_fixed_or_calc)0;
    css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
    struct box *parent;
    struct rect r = *clip;
    css_color bgcol;
    plot_style_t pstyle_fill_bg = {
        .fill_type = PLOT_OP_TYPE_SOLID,
        .fill_colour = *background_colour,
    };
    nserror res;

    if (ctx->background_images == false)
        return true;

    plot_content = (image != NULL);

    if (plot_content) {
        if (!box->parent) {
            /* Root element, special case:
             * background origin calc. is based on margin box */
            x -= box->margin[LEFT] * scale;
            y -= box->margin[TOP] * scale;
            width = box->margin[LEFT] + box->padding[LEFT] + box->width + box->padding[RIGHT] + box->margin[RIGHT];
            height = box->margin[TOP] + box->padding[TOP] + box->height + box->padding[BOTTOM] + box->margin[BOTTOM];
        } else {
            width = box->padding[LEFT] + box->width + box->padding[RIGHT];
            height = box->padding[TOP] + box->height + box->padding[BOTTOM];
        }
        /* handle background-repeat */
        switch (css_computed_background_repeat_layer(background->style, layer)) {
        case CSS_BACKGROUND_REPEAT_REPEAT:
            repeat_x = repeat_y = true;
            /* optimisation: only plot the colour if
             * bitmap is not opaque */
            plot_colour = plot_colour && !content_get_opaque(image);
            break;

        case CSS_BACKGROUND_REPEAT_REPEAT_X:
            repeat_x = true;
            break;

        case CSS_BACKGROUND_REPEAT_REPEAT_Y:
            repeat_y = true;
            break;

        case CSS_BACKGROUND_REPEAT_NO_REPEAT:
            break;

        default:
            break;
        }

        /* handle background-position */
        css_computed_background_position_layer(background->style, layer, &hpos, &hunit, &vpos, &vunit);
        {
            int bg_width = content_get_width(image);
            int bg_height = content_get_height(image);
#ifdef WISP_DEVICE_PIXEL_LAYOUT
            {
                int dpi = browser_get_dpi();
                if (dpi > 0 && dpi != 96) {
                    bg_width = (bg_width * dpi) / 96;
                    bg_height = (bg_height * dpi) / 96;
                }
            }
#endif
            if (hunit == CSS_UNIT_PCT) {
                x += (width - bg_width) * scale * FIXTOFLT(hpos.value) / 100.;
            } else if (hunit == CSS_UNIT_CALC) {
                int px = 0;
                if (css_computed_length_to_px(background->style, unit_len_ctx, width - bg_width, hpos, hunit, &px) ==
                    CSS_OK) {
                    x += (int)(px * scale);
                }
            } else {
                x += (int)(FIXTOFLT(css_unit_len2device_px(background->style, unit_len_ctx, hpos.value, hunit)) *
                    scale);
            }

            if (vunit == CSS_UNIT_PCT) {
                y += (height - bg_height) * scale * FIXTOFLT(vpos.value) / 100.;
            } else if (vunit == CSS_UNIT_CALC) {
                int px = 0;
                if (css_computed_length_to_px(background->style, unit_len_ctx, height - bg_height, vpos, vunit, &px) ==
                    CSS_OK) {
                    y += (int)(px * scale);
                }
            } else {
                y += (int)(FIXTOFLT(css_unit_len2device_px(background->style, unit_len_ctx, vpos.value, vunit)) *
                    scale);
            }
        }
    }

    /* special case for table rows as their background needs
     * to be clipped to all the cells */
    if (box->type == BOX_TABLE_ROW) {
        css_fixed_or_calc h = (css_fixed_or_calc)0, v = (css_fixed_or_calc)0;
        css_unit hu = CSS_UNIT_PX, vu = CSS_UNIT_PX;

        for (parent = box->parent; ((parent) && (parent->type != BOX_TABLE)); parent = parent->parent)
            ;
        assert(parent && (parent->style));

        css_computed_border_spacing(parent->style, &h, &hu, &v, &vu);

        if (hu == CSS_UNIT_CALC || vu == CSS_UNIT_CALC) {
            int hpx = 0;
            int vpx = 0;
            bool ok_h = css_computed_length_to_px(parent->style, unit_len_ctx, -1, h, hu, &hpx) == CSS_OK;
            bool ok_v = css_computed_length_to_px(parent->style, unit_len_ctx, -1, v, vu, &vpx) == CSS_OK;
            clip_to_children = (ok_h && hpx > 0) || (ok_v && vpx > 0);
        } else {
            clip_to_children = (h.value > 0) || (v.value > 0);
        }

        if (clip_to_children)
            clip_box = box->children;
    }

    for (; clip_box; clip_box = clip_box->next) {
        /* clip to child boxes if needed */
        if (clip_to_children) {
            assert(clip_box->type == BOX_TABLE_CELL);

            /* update clip.* to the child cell */
            r.x0 = ox + (clip_box->x * scale);
            r.y0 = oy + (clip_box->y * scale);
            r.x1 = r.x0 + (clip_box->padding[LEFT] + clip_box->width + clip_box->padding[RIGHT]) * scale;
            r.y1 = r.y0 + (clip_box->padding[TOP] + clip_box->height + clip_box->padding[BOTTOM]) * scale;

            if (r.x0 < clip->x0)
                r.x0 = clip->x0;
            if (r.y0 < clip->y0)
                r.y0 = clip->y0;
            if (r.x1 > clip->x1)
                r.x1 = clip->x1;
            if (r.y1 > clip->y1)
                r.y1 = clip->y1;

            css_computed_background_color(clip_box->style, &bgcol);

            /* Opaque cell colors cover the row. Translucent colors and
             * partially covering images still need the row underneath. */
            if ((r.x0 >= r.x1) || (r.y0 >= r.y1) || (bgcol >> 24) == 0xff)
                continue;
            if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
                return false;
        }

        /* plot the background colour */
        css_computed_background_color(background->style, &bgcol);

        if (nscss_color_is_transparent(bgcol) == false) {
            *background_colour = nscss_color_to_ns(bgcol);
            pstyle_fill_bg.fill_colour = *background_colour;
            if (plot_colour) {
                res = ctx->plot->rectangle(ctx, &pstyle_fill_bg, &r);
                if (res != NSERROR_OK) {
                    return false;
                }
            }
        }

        /* Generated images have the padding-box size with background-size:auto. */
        struct rect area = {
            .x0 = ox,
            .y0 = oy,
            .x1 = ox + (box->padding[LEFT] + box->width + box->padding[RIGHT]) * scale,
            .y1 = oy + (box->padding[TOP] + box->height + box->padding[BOTTOM]) * scale,
        };
        if (!html_redraw_gradient_layer(background->style, layer, &area, &r, scale, unit_len_ctx, ctx))
            return false;

        /* and plot the image */
        if (plot_content) {
            width = content_get_width(image);
            height = content_get_height(image);
#ifdef WISP_DEVICE_PIXEL_LAYOUT
            {
                int dpi = browser_get_dpi();
                if (dpi > 0 && dpi != 96) {
                    width = (width * dpi) / 96;
                    height = (height * dpi) / 96;
                }
            }
#endif

            /* ensure clip area only as large as required */
            if (!repeat_x) {
                if (r.x0 < x)
                    r.x0 = x;
                if (r.x1 > x + width * scale)
                    r.x1 = x + width * scale;
            }
            if (!repeat_y) {
                if (r.y0 < y)
                    r.y0 = y;
                if (r.y1 > y + height * scale)
                    r.y1 = y + height * scale;
            }
            /* valid clipping rectangles only */
            if ((r.x0 < r.x1) && (r.y0 < r.y1)) {
                struct content_redraw_data bg_data;

                res = ctx->plot->clip(ctx, &r);
                if (res != NSERROR_OK) {
                    return false;
                }

                bg_data.x = x;
                bg_data.y = y;
                bg_data.width = ceilf(width * scale);
                bg_data.height = ceilf(height * scale);
                bg_data.background_colour = *background_colour;
                bg_data.scale = scale;
                bg_data.repeat_x = repeat_x;
                bg_data.repeat_y = repeat_y;

                /* We just continue if redraw fails */
                content_redraw(image, &bg_data, &r, ctx);
            }
        }

        /* only <tr> rows being clipped to child boxes loop */
        if (!clip_to_children)
            return true;
    }
    return true;
}


/**
 * Plot background images.
 *
 * The reason for the presence of \a background is the backwards compatibility
 * mess that is backgrounds on &lt;body&gt;. The background will be drawn
 * relative to \a box, using the background information contained within \a
 * background.
 *
 * \param  x	  coordinate of box
 * \param  y	  coordinate of box
 * \param  box	  box to draw background image of
 * \param  scale  scale for redraw
 * \param  clip   current clip rectangle
 * \param  background_colour  current background colour
 * \param  background  box containing background details (usually \a box)
 * \param  unit_len_ctx  Length conversion context
 * \param  ctx      current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_background(int x, int y, struct box *box, float scale, const struct rect *clip,
    colour *background_colour, struct box *background, const css_unit_ctx *unit_len_ctx,
    const struct redraw_context *ctx)
{
    uint32_t count = css_computed_background_layer_count(background->style);
    if (count == 1)
        return html_redraw_background_layer(
            x, y, box, scale, clip, background_colour, background, unit_len_ctx, ctx, 0, true);
    for (uint32_t i = count; i-- > 0;) {
        if (ctx->plot->clip(ctx, clip) != NSERROR_OK ||
            !html_redraw_background_layer(
                x, y, box, scale, clip, background_colour, background, unit_len_ctx, ctx, i, i == count - 1))
            return false;
    }
    return true;
}

/**
 * Plot an inline's background and/or background image.
 *
 * \param  x	  coordinate of box
 * \param  y	  coordinate of box
 * \param  box	  BOX_INLINE which created the background
 * \param  scale  scale for redraw
 * \param  clip	  coordinates of clip rectangle
 * \param  b	  coordinates of border edge rectangle
 * \param  first  true if this is the first rectangle associated with the inline
 * \param  last   true if this is the last rectangle associated with the inline
 * \param  background_colour  updated to current background colour if plotted
 * \param  unit_len_ctx  Length conversion context
 * \param  ctx      current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_inline_background_layer(int x, int y, struct box *box, float scale, const struct rect *clip,
    struct rect b, bool first, bool last, colour *background_colour, const css_unit_ctx *unit_len_ctx,
    const struct redraw_context *ctx, uint32_t layer, bool paint_colour)
{
    struct hlcache_handle *image = box_background_layer(box, layer);
    struct rect r = *clip;
    bool repeat_x = false;
    bool repeat_y = false;
    bool plot_colour = paint_colour;
    bool plot_content;
    css_fixed_or_calc hpos = (css_fixed_or_calc)0, vpos = (css_fixed_or_calc)0;
    css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
    css_color bgcol;
    plot_style_t pstyle_fill_bg = {
        .fill_type = PLOT_OP_TYPE_SOLID,
        .fill_colour = *background_colour,
    };
    nserror res;

    plot_content = (image != NULL);

    if (html_redraw_printing && nsoption_bool(remove_backgrounds))
        return true;

    if (plot_content) {
        /* handle background-repeat */
        switch (css_computed_background_repeat_layer(box->style, layer)) {
        case CSS_BACKGROUND_REPEAT_REPEAT:
            repeat_x = repeat_y = true;
            /* optimisation: only plot the colour if
             * bitmap is not opaque
             */
            plot_colour = plot_colour && !content_get_opaque(image);
            break;

        case CSS_BACKGROUND_REPEAT_REPEAT_X:
            repeat_x = true;
            break;

        case CSS_BACKGROUND_REPEAT_REPEAT_Y:
            repeat_y = true;
            break;

        case CSS_BACKGROUND_REPEAT_NO_REPEAT:
            break;

        default:
            break;
        }

        /* handle background-position */
        css_computed_background_position_layer(box->style, layer, &hpos, &hunit, &vpos, &vunit);
        {
            int bg_width = content_get_width(image);
            int bg_height = content_get_height(image);
#ifdef WISP_DEVICE_PIXEL_LAYOUT
            {
                int dpi = browser_get_dpi();
                if (dpi > 0 && dpi != 96) {
                    bg_width = (bg_width * dpi) / 96;
                    bg_height = (bg_height * dpi) / 96;
                }
            }
#endif
            if (hunit == CSS_UNIT_PCT) {
                x += (b.x1 - b.x0 - bg_width * scale) * FIXTOFLT(hpos.value) / 100.;

                if (!repeat_x) {
                    int hpos_pct = FIXTOINT(hpos.value);
                    if ((hpos_pct < 2 && !first) || (hpos_pct > 98 && !last)) {
                        plot_content = false;
                    }
                }
            } else if (hunit == CSS_UNIT_CALC) {
                int available_x = 0;
                if (scale > 0.0f) {
                    available_x = (int)((b.x1 - b.x0) / scale) - bg_width;
                }
                int px = 0;
                if (css_computed_length_to_px(box->style, unit_len_ctx, available_x, hpos, hunit, &px) == CSS_OK) {
                    x += (int)(px * scale);
                }
            } else {
                x += (int)(FIXTOFLT(css_unit_len2device_px(box->style, unit_len_ctx, hpos.value, hunit)) * scale);
            }

            if (vunit == CSS_UNIT_PCT) {
                y += (b.y1 - b.y0 - bg_height * scale) * FIXTOFLT(vpos.value) / 100.;
            } else if (vunit == CSS_UNIT_CALC) {
                int available_y = 0;
                if (scale > 0.0f) {
                    available_y = (int)((b.y1 - b.y0) / scale) - bg_height;
                }
                int px = 0;
                if (css_computed_length_to_px(box->style, unit_len_ctx, available_y, vpos, vunit, &px) == CSS_OK) {
                    y += (int)(px * scale);
                }
            } else {
                y += (int)(FIXTOFLT(css_unit_len2device_px(box->style, unit_len_ctx, vpos.value, vunit)) * scale);
            }
        }
    }

    /* plot the background colour */
    css_computed_background_color(box->style, &bgcol);

    if (nscss_color_is_transparent(bgcol) == false) {
        *background_colour = nscss_color_to_ns(bgcol);
        pstyle_fill_bg.fill_colour = *background_colour;

        if (plot_colour) {
            res = ctx->plot->rectangle(ctx, &pstyle_fill_bg, &r);
            if (res != NSERROR_OK) {
                return false;
            }
        }
    }
    if (!html_redraw_gradient_layer(box->style, layer, &b, &r, scale, unit_len_ctx, ctx))
        return false;
    /* and plot the image */
    if (plot_content) {
        int width = content_get_width(image);
        int height = content_get_height(image);
#ifdef WISP_DEVICE_PIXEL_LAYOUT
        {
            int dpi = browser_get_dpi();
            if (dpi > 0 && dpi != 96) {
                width = (width * dpi) / 96;
                height = (height * dpi) / 96;
            }
        }
#endif

        if (!repeat_x) {
            if (r.x0 < x)
                r.x0 = x;
            if (r.x1 > x + width * scale)
                r.x1 = x + width * scale;
        }
        if (!repeat_y) {
            if (r.y0 < y)
                r.y0 = y;
            if (r.y1 > y + height * scale)
                r.y1 = y + height * scale;
        }
        /* valid clipping rectangles only */
        if ((r.x0 < r.x1) && (r.y0 < r.y1)) {
            struct content_redraw_data bg_data;

            res = ctx->plot->clip(ctx, &r);
            if (res != NSERROR_OK) {
                return false;
            }

            bg_data.x = x;
            bg_data.y = y;
            bg_data.width = ceilf(width * scale);
            bg_data.height = ceilf(height * scale);
            bg_data.background_colour = *background_colour;
            bg_data.scale = scale;
            bg_data.repeat_x = repeat_x;
            bg_data.repeat_y = repeat_y;

            /* We just continue if redraw fails */
            content_redraw(image, &bg_data, &r, ctx);
        }
    }

    return true;
}


static bool html_redraw_inline_background(int x, int y, struct box *box, float scale, const struct rect *clip,
    struct rect b, bool first, bool last, colour *background_colour, const css_unit_ctx *unit_len_ctx,
    const struct redraw_context *ctx)
{
    uint32_t count = css_computed_background_layer_count(box->style);
    if (count == 1)
        return html_redraw_inline_background_layer(
            x, y, box, scale, clip, b, first, last, background_colour, unit_len_ctx, ctx, 0, true);
    for (uint32_t i = count; i-- > 0;) {
        if (ctx->plot->clip(ctx, clip) != NSERROR_OK ||
            !html_redraw_inline_background_layer(
                x, y, box, scale, clip, b, first, last, background_colour, unit_len_ctx, ctx, i, i == count - 1))
            return false;
    }
    return true;
}

/**
 * Plot text decoration for an inline box.
 *
 * \param  box     box to plot decorations for, of type BOX_INLINE
 * \param  x       x coordinate of parent of box
 * \param  y       y coordinate of parent of box
 * \param  scale   scale for redraw
 * \param  colour  colour for decorations
 * \param  ratio   position of line as a ratio of line height
 * \param  ctx	   current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_text_decoration_inline(
    struct box *box, int x, int y, float scale, colour colour, float ratio, const struct redraw_context *ctx)
{
    struct box *c;
    plot_style_t plot_style_box = {
        .stroke_type = PLOT_OP_TYPE_SOLID,
        .stroke_colour = colour,
    };
    nserror res;
    struct rect rect;

    for (c = box->next; c && c != box->inline_end; c = c->next) {
        if (c->type != BOX_TEXT) {
            continue;
        }
        rect.x0 = (x + c->x) * scale;
        rect.y0 = (y + c->y + c->height * ratio) * scale;
        rect.x1 = (x + c->x + c->width) * scale;
        rect.y1 = (y + c->y + c->height * ratio) * scale;
        res = ctx->plot->line(ctx, &plot_style_box, &rect);
        if (res != NSERROR_OK) {
            return false;
        }
    }
    return true;
}


/**
 * Plot text decoration for an non-inline box.
 *
 * \param  box     box to plot decorations for, of type other than BOX_INLINE
 * \param  x       x coordinate of box
 * \param  y       y coordinate of box
 * \param  scale   scale for redraw
 * \param  colour  colour for decorations
 * \param  ratio   position of line as a ratio of line height
 * \param  ctx	   current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_text_decoration_block(
    struct box *box, int x, int y, float scale, colour colour, float ratio, const struct redraw_context *ctx)
{
    struct box *c;
    plot_style_t plot_style_box = {
        .stroke_type = PLOT_OP_TYPE_SOLID,
        .stroke_colour = colour,
    };
    nserror res;
    struct rect rect;

    /* draw through text descendants */
    for (c = box->children; c; c = c->next) {
        if (c->type == BOX_TEXT) {
            rect.x0 = (x + c->x) * scale;
            rect.y0 = (y + c->y + c->height * ratio) * scale;
            rect.x1 = (x + c->x + c->width) * scale;
            rect.y1 = (y + c->y + c->height * ratio) * scale;
            res = ctx->plot->line(ctx, &plot_style_box, &rect);
            if (res != NSERROR_OK) {
                return false;
            }
        } else if ((c->type == BOX_INLINE_CONTAINER) || (c->type == BOX_BLOCK)) {
            if (!html_redraw_text_decoration_block(c, x + c->x, y + c->y, scale, colour, ratio, ctx))
                return false;
        }
    }
    return true;
}


/**
 * Plot text decoration for a box.
 *
 * \param  box       box to plot decorations for
 * \param  x_parent  x coordinate of parent of box
 * \param  y_parent  y coordinate of parent of box
 * \param  scale     scale for redraw
 * \param  background_colour  current background colour
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_text_decoration(struct box *box, int x_parent, int y_parent, float scale,
    colour background_colour, const struct redraw_context *ctx)
{
    static const enum css_text_decoration_e decoration[] = {
        CSS_TEXT_DECORATION_UNDERLINE, CSS_TEXT_DECORATION_OVERLINE, CSS_TEXT_DECORATION_LINE_THROUGH};
    static const float line_ratio[] = {0.9, 0.1, 0.5};
    colour fgcol;
    unsigned int i;
    css_color col;

    css_computed_color(box->style, &col);
    fgcol = nscss_color_to_ns(col);

    /* antialias colour for under/overline */
    if (html_redraw_printing == false)
        fgcol = blend_colour(background_colour, fgcol);

    if (box->type == BOX_INLINE) {
        if (!box->inline_end)
            return true;
        for (i = 0; i != NOF_ELEMENTS(decoration); i++)
            if (css_computed_text_decoration(box->style) & decoration[i])
                if (!html_redraw_text_decoration_inline(box, x_parent, y_parent, scale, fgcol, line_ratio[i], ctx))
                    return false;
    } else {
        for (i = 0; i != NOF_ELEMENTS(decoration); i++)
            if (css_computed_text_decoration(box->style) & decoration[i])
                if (!html_redraw_text_decoration_block(
                        box, x_parent + box->x, y_parent + box->y, scale, fgcol, line_ratio[i], ctx))
                    return false;
    }

    return true;
}


/**
 * Redraw the text content of a box, possibly partially highlighted
 * because the text has been selected, or matches a search operation.
 *
 * \param html The html content to redraw text within.
 * \param  box      box with text content
 * \param  x        x co-ord of box
 * \param  y        y co-ord of box
 * \param  clip     current clip rectangle
 * \param  scale    current scale setting (1.0 = 100%)
 * \param  current_background_color
 * \param  ctx	    current redraw context
 * \return true iff successful and redraw should proceed
 */

static bool html_redraw_text_box(const html_content *html, struct box *box, int x, int y, const struct rect *clip,
    float scale, colour current_background_color, const struct redraw_context *ctx)
{
    bool excluded = (box->object != NULL);
    plot_font_style_t fstyle;

    font_plot_style_from_css(html, box->style, &fstyle);
    fstyle.background = current_background_color;

    if (!text_redraw(box->text, box->length, box->byte_offset, box->space, &fstyle, x, y, clip, box->height, scale,
            excluded, (struct content *)html, html->sel, ctx))
        return false;

    return true;
}

static bool html_redraw_box(const html_content *html, struct box *box, int x_parent, int y_parent,
    const struct rect *clip, float scale, colour current_background_color, const struct content_redraw_data *data,
    const struct rect *paint_clip, const struct redraw_context *ctx, enum box_paint_phase phase);

/* Traverse the structural tree in each phase, preserving ancestor clips and
 * scroll offsets. The float list is a layout index, not CSS tree order. */
static bool html_redraw_box_phase(const html_content *html, struct box *box, int x, int y, const struct rect *clip,
    float scale, colour background, const struct content_redraw_data *data, const struct rect *paint_clip,
    const struct redraw_context *ctx, enum box_paint_phase phase)
{
    for (struct box *c = box->children; c; c = c->next) {
        if (box_get_paint_z_index(c) != Z_INDEX_AUTO)
            continue; /* Deferred by the nearest real stacking context. */
        bool atomic = stacking_box_is_atomic(c);
        if (atomic && stacking_box_phase(c) != phase)
            continue;
        if (!atomic && !c->children && stacking_box_phase(c) != phase &&
            !(phase == BOX_PAINT_INLINE &&
                (c->object || c->gadget || (c->flags & REPLACE_DIM) || c->iframe || box_has_outside_marker(c) ||
                    c->scroll_x || c->scroll_y)))
            continue;
        if (!html_redraw_box(
                html, c, x, y, clip, scale, background, data, paint_clip, ctx, atomic ? BOX_PAINT_ALL : phase))
            return false;
    }
    return true;
}

/**
 * Draw the various children of a box.
 *
 * \param  html	     html content
 * \param  box	     box to draw children of
 * \param  x_parent  coordinate of parent box
 * \param  y_parent  coordinate of parent box
 * \param  clip      clip rectangle
 * \param  scale     scale for redraw
 * \param  current_background_color  background colour under this box
 * \param  paint_clip Immutable paint-pass boundary, including the document viewport
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_box_children(const html_content *html, struct box *box, int x_parent, int y_parent,
    const struct rect *clip, float scale, colour current_background_color, const struct content_redraw_data *data,
    const struct rect *paint_clip, const struct redraw_context *ctx, enum box_paint_phase phase)
{
    struct box *c;
    int x_offset, y_offset;
    struct stacking_context negative_zindex; /* z-index < 0 */
    struct stacking_context positive_zindex; /* z-index >= 0 */
    const struct stacking_coordinates coordinates = {.origin_x = data ? data->x : 0,
        .origin_y = data ? data->y : 0,
        .scroll_x = data ? data->viewport_x / scale - data->x : 0,
        .scroll_y = data ? data->viewport_y / scale - data->y : 0};
    size_t i;
    bool render_ok = true;

    x_offset = x_parent + box->x - scrollbar_get_offset(box->scroll_x);
    y_offset = y_parent + box->y - scrollbar_get_offset(box->scroll_y);

    if (phase != BOX_PAINT_ALL) {
        return html_redraw_box_phase(html, box, x_offset, y_offset, clip, scale, current_background_color, data,
                   paint_clip, ctx, phase) &&
            (phase != BOX_PAINT_BLOCK || html_redraw_table_borders(box, x_offset, y_offset, clip, scale, ctx));
    }

    /* Initialize separate stacking contexts for negative and positive */
    stacking_context_init(&negative_zindex);
    stacking_context_init(&positive_zindex);

    /*
     * CSS 2.1 Stacking Order:
     * 1. Background and borders of the stacking context root
     * 2. Negative z-index stacking contexts (sorted)
     * 3. In-flow, non-positioned block descendants
     * 4. Non-positioned floats
     * 5. In-flow, non-positioned inline descendants
     * 6. z-index: 0 stacking contexts and positioned descendants
     * 7. Positive z-index stacking contexts (sorted)
     */

    /* Each real context collects once. Ordinary and positioned-auto boxes
     * paint their in-flow contents here; their positioned descendants are
     * already owned by the surrounding context's deferred lists. */
    bool owns_stacking = box->parent == NULL || box_creates_stacking_context(box);
    if (owns_stacking) {
        for (c = box->children; c; c = c->next) {
            if (!stacking_context_collect_subtree(
                    &negative_zindex, &positive_zindex, c, x_offset, y_offset, &coordinates))
                goto collection_failed;
        }
    }

    /* Negative contexts paint above this context's background, below its
     * ordinary descendants (CSS 2.2 Appendix E.2). */
    if (negative_zindex.count > 0) {
        stacking_context_sort(&negative_zindex);
        for (i = 0; i < negative_zindex.count; i++) {
            if (!html_redraw_box(html, negative_zindex.entries[i].box, negative_zindex.entries[i].x_parent,
                    negative_zindex.entries[i].y_parent, clip, scale, current_background_color, data, paint_clip, ctx,
                    BOX_PAINT_ALL)) {
                render_ok = false;
                goto cleanup;
            }
        }
    }

    /* CSS 2.2 Appendix E.2 steps 4–7. A block background must stay below
     * floats even when that block's inline contents paint above them. */
    for (enum box_paint_phase pass = BOX_PAINT_BLOCK; pass <= BOX_PAINT_INLINE; pass++) {
        /* Decorations of an atomic/root box belong to its inline phase too. */
        if (pass == BOX_PAINT_INLINE && box->style && css_computed_visibility(box->style) != CSS_VISIBILITY_HIDDEN &&
            css_computed_text_decoration(box->style) != CSS_TEXT_DECORATION_NONE &&
            (ctx->plot->clip(ctx, clip) != NSERROR_OK ||
                !html_redraw_text_decoration(box, x_parent, y_parent, scale, current_background_color, ctx))) {
            render_ok = false;
            goto cleanup;
        }
        if (!html_redraw_box_phase(
                html, box, x_offset, y_offset, clip, scale, current_background_color, data, paint_clip, ctx, pass)) {
            render_ok = false;
            goto cleanup;
        }
        if (pass == BOX_PAINT_BLOCK && !html_redraw_table_borders(box, x_offset, y_offset, clip, scale, ctx)) {
            render_ok = false;
            goto cleanup;
        }
    }

    /* Pass 4: Render positive z-index elements (on top of in-flow content)
     */
    if (positive_zindex.count > 0) {
        stacking_context_sort(&positive_zindex);
        for (i = 0; i < positive_zindex.count; i++) {
            if (!html_redraw_box(html, positive_zindex.entries[i].box, positive_zindex.entries[i].x_parent,
                    positive_zindex.entries[i].y_parent, clip, scale, current_background_color, data, paint_clip, ctx,
                    BOX_PAINT_ALL)) {
                render_ok = false;
                goto cleanup;
            }
        }
    }
    goto cleanup;

collection_failed:
    /* Do not paint a partial deferred list or silently omit its descendants. */
    render_ok = false;

cleanup:
    stacking_context_fini(&negative_zindex);
    stacking_context_fini(&positive_zindex);
    return render_ok;
}

/**
 * Recursively draw a box.
 *
 * \param  html	     html content
 * \param  box	     box to draw
 * \param  x_parent  coordinate of parent box
 * \param  y_parent  coordinate of parent box
 * \param  clip      clip rectangle
 * \param  scale     scale for redraw
 * \param  current_background_color  background colour under this box
 * \param  paint_clip Immutable paint-pass boundary, including the document viewport
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 *
 * x, y, clip_[xy][01] are in target coordinates.
 */

static bool html_redraw_box(const html_content *html, struct box *box, int x_parent, int y_parent,
    const struct rect *clip, const float scale, colour current_background_color, const struct content_redraw_data *data,
    const struct rect *paint_clip, const struct redraw_context *ctx, enum box_paint_phase phase)
{
    if (box->flags & IS_OPACITY_ZERO)
        return true;

    bool structural_float = box->type == BOX_FLOAT_LEFT || box->type == BOX_FLOAT_RIGHT;
    if (structural_float)
        stacking_float_parent_coords(box, &x_parent, &y_parent);
    if (!structural_float && !box->abs_containing_block && box->sticky) {
        const int scroll[] = {
            data ? data->viewport_x / scale - data->x : 0, data ? data->viewport_y / scale - data->y : 0};
        int64_t dx, dy;
        box_sticky_local_offset(box, scroll, &dx, &dy);
        x_parent = box_sticky_coordinate((int64_t)x_parent + dx);
        y_parent = box_sticky_coordinate((int64_t)y_parent + dy);
    }

    bool paint_background = phase == BOX_PAINT_ALL || phase == stacking_box_phase(box);
    bool paint_content = phase == BOX_PAINT_ALL || phase == BOX_PAINT_INLINE;

    const struct plotter_table *plot = ctx->plot;
    int x, y;
    int width, height;
    int padding_left, padding_top, padding_width, padding_height;
    int border_left, border_top, border_right, border_bottom;
    struct rect r;
    struct rect rect;
    int x_scrolled, y_scrolled;
    struct rect viewport_clip;
    struct box *bg_box = NULL;
    css_computed_clip_rect css_rect;
    enum css_overflow_e overflow_x = CSS_OVERFLOW_VISIBLE;
    enum css_overflow_e overflow_y = CSS_OVERFLOW_VISIBLE;
    dom_exception exc;
    dom_html_element_type tag_type;

    if (html_redraw_printing && (box->flags & PRINTED))
        return true;

    bool has_transform = false;
    bool need_transform = false;
    bool result = true; /* Track success/failure for cleanup */
    float transform_matrix[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}; /* Identity matrix */

    if (box->style != NULL) {
        overflow_x = css_computed_overflow_x(box->style);
        overflow_y = css_computed_overflow_y(box->style);

        /* Check for CSS transform and apply if available */
        uint32_t transform_count = 0;
        const css_transform_function *transform_functions = NULL;
        uint8_t transform_type = css_computed_transform(box->style, &transform_count, &transform_functions);


        if (transform_type == CSS_TRANSFORM_FUNCTIONS && transform_count > 0 && transform_functions != NULL &&
            ctx->plot->push_transform != NULL && box->node != NULL) {
            /* Only apply transform if this box owns a DOM node - prevents child boxes
             * sharing the parent's style from having the transform applied again */

            /* CSS Transforms spec says:
             * 1. Start with identity matrix
             * 2. Translate by transform-origin
             * 3. Multiply by each transform function left-to-right
             * 4. Translate by negated transform-origin
             *
             * Matrix format: [a, b, c, d, tx, ty] where:
             * | a  c  tx |
             * | b  d  ty |
             * | 0  0  1  |
             */
            /* Use transform_matrix from function scope */
            int box_x = x_parent + box->x;
            int box_y = y_parent + box->y;
            float cx = box_x + box->width / 2.0f; /* transform-origin default: center */
            float cy = box_y + box->height / 2.0f;

            /* Build composed transform transform_matrix M (starting with identity, no pre-translate) */
            for (uint32_t i = 0; i < transform_count; i++) {
                const css_transform_function *func = &transform_functions[i];
                float val1 = FIXTOFLT(func->value1);
                float val2 = FIXTOFLT(func->value2);

                /* Note: Percentage conversion depends on the transform function type.
                 * For translate functions:
                 *   - translateX(%) uses width
                 *   - translateY(%) uses height
                 *   - translate(x%, y%) uses width for x, height for y
                 * Other functions handle percentages within their case blocks.
                 */

                switch (func->type) {
                case CSS_TRANSFORM_FUNC_TRANSLATE: {
                    /* M' = M * T(tx,ty) -- right multiply by translation */
                    float tx = val1, ty = val2;
                    if (func->unit1 == CSS_UNIT_PCT)
                        tx = (val1 / 100.0f) * box->width;
                    if (func->unit2 == CSS_UNIT_PCT)
                        ty = (val2 / 100.0f) * box->height;
                    transform_matrix[4] += tx * transform_matrix[0] + ty * transform_matrix[2];
                    transform_matrix[5] += tx * transform_matrix[1] + ty * transform_matrix[3];
                    break;
                }
                case CSS_TRANSFORM_FUNC_TRANSLATEX: {
                    float tx = val1;
                    if (func->unit1 == CSS_UNIT_PCT)
                        tx = (val1 / 100.0f) * box->width;
                    transform_matrix[4] += tx * transform_matrix[0];
                    transform_matrix[5] += tx * transform_matrix[1];
                    break;
                }
                case CSS_TRANSFORM_FUNC_TRANSLATEY: {
                    float ty = val1;
                    if (func->unit1 == CSS_UNIT_PCT)
                        ty = (val1 / 100.0f) * box->height; /* Use HEIGHT for translateY */
                    transform_matrix[4] += ty * transform_matrix[2];
                    transform_matrix[5] += ty * transform_matrix[3];
                    break;
                }
                case CSS_TRANSFORM_FUNC_SCALE: {
                    /* M' = M * S(sx,sy) -- right multiply by scale */
                    float sx = val1;
                    /* Parsing has already supplied the omitted Y argument.
                     * An explicit zero makes the transform non-invertible. */
                    float sy = val2;
                    transform_matrix[0] *= sx;
                    transform_matrix[1] *= sx;
                    transform_matrix[2] *= sy;
                    transform_matrix[3] *= sy;
                    /* tx, ty unchanged when multiplying by pure scale from right */
                    break;
                }
                case CSS_TRANSFORM_FUNC_SCALEX:
                    transform_matrix[0] *= val1;
                    transform_matrix[1] *= val1;
                    break;
                case CSS_TRANSFORM_FUNC_SCALEY:
                    transform_matrix[2] *= val1;
                    transform_matrix[3] *= val1;
                    break;
                case CSS_TRANSFORM_FUNC_ROTATE: {
                    /* M' = M * R(θ) -- right multiply by rotation */
                    float rad = val1 * M_PI / 180.0f;
                    float cos_a = cosf(rad), sin_a = sinf(rad);
                    float a = transform_matrix[0], b = transform_matrix[1], c = transform_matrix[2],
                          d = transform_matrix[3];
                    transform_matrix[0] = a * cos_a + c * sin_a;
                    transform_matrix[1] = b * cos_a + d * sin_a;
                    transform_matrix[2] = c * cos_a - a * sin_a;
                    transform_matrix[3] = d * cos_a - b * sin_a;
                    /* tx, ty unchanged when multiplying by pure rotation from right */
                    break;
                }
                default:
                    break;
                }
            }

            /* Apply transform-origin wrap: Final = T(cx,cy) * M * T(-cx,-cy)
             * This computes to: tx' = tx + cx*(1-a) - cy*c, ty' = ty + cy*(1-d) - cx*b */
            float a = transform_matrix[0], b = transform_matrix[1], c = transform_matrix[2], d = transform_matrix[3];
            transform_matrix[4] += cx * (1.0f - a) - cy * c;
            transform_matrix[5] += cy * (1.0f - d) - cx * b;

            /* Check if the transform_matrix has any non-identity transform */
            bool is_identity = (transform_matrix[0] == 1.0f && transform_matrix[1] == 0.0f &&
                transform_matrix[2] == 0.0f && transform_matrix[3] == 1.0f && transform_matrix[4] == 0.0f &&
                transform_matrix[5] == 0.0f);

            /* Mark that we need a transform, but don't push yet - wait until after early bailouts */
            need_transform = !is_identity;
        }
    }

    /* avoid trivial FP maths */
    if (scale == 1.0) {
        /* For absolute/fixed positioned elements, use box_coords() to get
         * correct screen position from the containing block.
         * box_coords() returns document-relative coordinates, so we must add
         * the viewport scroll offset (data->x/y) to get screen coordinates. */
        if (box->abs_containing_block != NULL) {
            int doc_x, doc_y;
            box_coords_at_scroll(box, &doc_x, &doc_y,
                data ? data->viewport_x - data->x : 0, data ? data->viewport_y - data->y : 0);
            x = doc_x;
            y = doc_y;
            /* Add viewport scroll offset */
            if (data != NULL) {
                x += data->x;
                y += data->y;
            }
            x_parent = x - box->x;
            y_parent = y - box->y;
        } else {
            x = x_parent + box->x;
            y = y_parent + box->y;
        }
        width = box->width;
        height = box->height;
        padding_left = box->padding[LEFT];
        padding_top = box->padding[TOP];
        padding_width = padding_left + box->width + box->padding[RIGHT];
        padding_height = padding_top + box->height + box->padding[BOTTOM];
        border_left = box->border[LEFT].width;
        border_top = box->border[TOP].width;
        border_right = box->border[RIGHT].width;
        border_bottom = box->border[BOTTOM].width;

    } else {
        /* For absolute/fixed positioned elements, use box_coords() to get
         * correct screen position from the containing block, not from the
         * (incorrect) parent chain accumulation. Per CSS 2.1 §10.1.
         * Add viewport scroll offset (data->x/y) since box_coords returns
         * document-relative coordinates. */
        if (box->abs_containing_block != NULL) {
            int abs_x, abs_y;
            box_coords_at_scroll(box, &abs_x, &abs_y,
                data ? data->viewport_x / scale - data->x : 0,
                data ? data->viewport_y / scale - data->y : 0);
            /* Add viewport scroll offset before scaling */
            if (data != NULL) {
                abs_x += data->x;
                abs_y += data->y;
            }
            x_parent = abs_x - box->x;
            y_parent = abs_y - box->y;
            x = abs_x * scale;
            y = abs_y * scale;
        } else {
            x = (x_parent + box->x) * scale;
            y = (y_parent + box->y) * scale;
        }
        width = box->width * scale;
        height = box->height * scale;
        /* left and top padding values are normally zero,
         * so avoid trivial FP maths */
        padding_left = box->padding[LEFT] ? box->padding[LEFT] * scale : 0;
        padding_top = box->padding[TOP] ? box->padding[TOP] * scale : 0;
        padding_width = (box->padding[LEFT] + box->width + box->padding[RIGHT]) * scale;
        padding_height = (box->padding[TOP] + box->height + box->padding[BOTTOM]) * scale;
        border_left = box->border[LEFT].width * scale;
        border_top = box->border[TOP].width * scale;
        border_right = box->border[RIGHT].width * scale;
        border_bottom = box->border[BOTTOM].width * scale;
    }

    /* Positioned descendants may escape a DOM ancestor's clip, but never the
     * requested paint area. paint_clip is unchanged throughout this document's
     * walk; a nested HTML redraw starts with its own frame/content clip. */
    if (box->abs_containing_block != NULL && data != NULL) {
        viewport_clip = (struct rect){data->viewport_x, data->viewport_y, data->viewport_x + data->root_width,
            data->viewport_y + data->root_height};
        if (html_clip_intersect_box(&viewport_clip, paint_clip, false) == CLIP_RESULT_EMPTY)
            return true;
        clip = &viewport_clip;
    }

    bool has_css_clip = box->style && css_computed_position(box->style) == CSS_POSITION_ABSOLUTE &&
        css_computed_clip(box->style, &css_rect) == CSS_CLIP_RECT;

    /* Deferred paint must retain clips that the collection walk bypassed. */
    struct rect ancestor_clip;
    if (box_get_paint_z_index(box) != Z_INDEX_AUTO) {
        const int scroll[] = {
            data ? data->viewport_x / scale - data->x : 0, data ? data->viewport_y / scale - data->y : 0};
        int ox = data ? data->x : 0, oy = data ? data->y : 0;
        ancestor_clip = (struct rect){floorf(clip->x0 / scale) - ox, floorf(clip->y0 / scale) - oy,
            ceilf(clip->x1 / scale) - ox, ceilf(clip->y1 / scale) - oy};
        if (!box_clip_to_ancestors(html, box, scroll, &ancestor_clip))
            return true;
        ancestor_clip = (struct rect){fmaxf(clip->x0, (ancestor_clip.x0 + ox) * scale),
            fmaxf(clip->y0, (ancestor_clip.y0 + oy) * scale), fminf(clip->x1, (ancestor_clip.x1 + ox) * scale),
            fminf(clip->y1, (ancestor_clip.y1 + oy) * scale)};
        clip = &ancestor_clip;
    }

    /* Calculate the painting bounds. Positioning does not clip visible
     * overflow: even an empty box can have visible descendants. Explicit
     * clip rectangles still start at the border box for their auto edges. */
    if (box->style && (has_css_clip || overflow_x != CSS_OVERFLOW_VISIBLE) && box->parent != NULL) {
        /* explicit clipping or non-visible overflow: use box size */
        r.x0 = x - border_left;
        r.x1 = x + padding_width + border_right;
    } else {
        /* visible overflow: use descendant extents */
        if (scale == 1.0) {
            r.x0 = x + box->descendant_x0;
            r.x1 = x + box->descendant_x1 + 1;
        } else {
            r.x0 = x + box->descendant_x0 * scale;
            r.x1 = x + box->descendant_x1 * scale + 1;
        }
        if (!box->parent) {
            int margin_left, margin_right;
            if (scale == 1.0) {
                margin_left = box->margin[LEFT];
                margin_right = box->margin[RIGHT];
            } else {
                margin_left = box->margin[LEFT] * scale;
                margin_right = box->margin[RIGHT] * scale;
            }
            r.x0 = x - border_left - margin_left < r.x0 ? x - border_left - margin_left : r.x0;
            r.x1 = x + padding_width + border_right + margin_right > r.x1
                ? x + padding_width + border_right + margin_right
                : r.x1;
        }
    }

    /* calculate rectangle covering this box and descendants */
    if (box->style && (has_css_clip || overflow_y != CSS_OVERFLOW_VISIBLE) && box->parent != NULL) {
        /* explicit clipping or non-visible overflow: use box size */
        r.y0 = y - border_top;
        r.y1 = y + padding_height + border_bottom;
    } else {
        /* visible overflow: use descendant extents */
        if (scale == 1.0) {
            r.y0 = y + box->descendant_y0;
            r.y1 = y + box->descendant_y1 + 1;
        } else {
            r.y0 = y + box->descendant_y0 * scale;
            r.y1 = y + box->descendant_y1 * scale + 1;
        }
        if (!box->parent) {
            int margin_top, margin_bottom;
            if (scale == 1.0) {
                margin_top = box->margin[TOP];
                margin_bottom = box->margin[BOTTOM];
            } else {
                margin_top = box->margin[TOP] * scale;
                margin_bottom = box->margin[BOTTOM] * scale;
            }
            r.y0 = y - border_top - margin_top < r.y0 ? y - border_top - margin_top : r.y0;
            r.y1 = y + padding_height + border_bottom + margin_bottom > r.y1
                ? y + padding_height + border_bottom + margin_bottom
                : r.y1;
        }
    }

    /* Settled descendant extents describe layout/scroll ranges, not sticky
     * travel. Keep marked visible-overflow branches open to the current clip;
     * real overflow and CSS clips continue to constrain their descendants. */
    if ((box->flags & HAS_STICKY_DESCENDANT) && !has_css_clip) {
        if (overflow_x == CSS_OVERFLOW_VISIBLE || !box->parent) {
            r.x0 = clip->x0;
            r.x1 = clip->x1;
        }
        if (overflow_y == CSS_OVERFLOW_VISIBLE || !box->parent) {
            r.y0 = clip->y0;
            r.y1 = clip->y1;
        }
    }

    /* return if the rectangle is completely outside the clip rectangle */
    if (clip->y1 < r.y0 || r.y1 < clip->y0 || clip->x1 < r.x0 || r.x1 < clip->x0) {
        /* A viewport-fixed descendant may still be visible when its DOM
         * ancestor is off screen. Its own redraw restores the viewport clip. */
        if (box->flags & HAS_FIXED_DESCENDANT) {
            int px = box->abs_containing_block ? x / scale - box->x : x_parent;
            int py = box->abs_containing_block ? y / scale - box->y : y_parent;
            return html_redraw_box_children(
                html, box, px, py, clip, scale, current_background_color, data, paint_clip, ctx, phase);
        }
        return true; /* Early return BEFORE push_transform, no cleanup needed */
    }

    /*if the rectangle is under the page bottom but it can fit in a page,
    don't print it now*/
    if (html_redraw_printing) {
        if (r.y1 > html_redraw_printing_border) {
            if (r.y1 - r.y0 <= html_redraw_printing_border &&
                (box->type == BOX_TEXT || box->type == BOX_TABLE_CELL || box->object || box->gadget)) {
                /*remember the highest of all points from the
                not printed elements*/
                if (r.y0 < html_redraw_printing_top_cropped)
                    html_redraw_printing_top_cropped = r.y0;
                return true; /* Early return BEFORE push_transform, no cleanup needed */
            }
        } else if (paint_content)
            box->flags |= PRINTED; /*it won't be printed anymore*/
    }

    /* if visibility is hidden render children only - NO transform needed for hidden elements */
    if (box->style && css_computed_visibility(box->style) == CSS_VISIBILITY_HIDDEN) {
        if ((ctx->plot->group_start) && (ctx->plot->group_start(ctx, "hidden box") != NSERROR_OK)) {
            return false;
        }
        if (!html_redraw_box_children(
                html, box, x_parent, y_parent, &r, scale, current_background_color, data, paint_clip, ctx, phase)) {
            return false;
        }
        return ((!ctx->plot->group_end) || (ctx->plot->group_end(ctx) == NSERROR_OK));
    }

    /* NOW push the transform - after all early bailouts, before rendering */
    if (need_transform && ctx->plot->push_transform != NULL) {
        if (ctx->plot->push_transform(ctx, transform_matrix) == NSERROR_OK) {
            has_transform = true;
        }
    }

    if ((ctx->plot->group_start) && (ctx->plot->group_start(ctx, "vis box") != NSERROR_OK)) {
        goto cleanup;
    }

    if (has_css_clip) {
        /* We have an absolutly positioned box with a clip rect */
        if (css_rect.left_auto == false)
            r.x0 = x - border_left +
                FIXTOINT(css_unit_len2device_px(box->style, &html->unit_len_ctx, css_rect.left, css_rect.lunit));

        if (css_rect.top_auto == false)
            r.y0 = y - border_top +
                FIXTOINT(css_unit_len2device_px(box->style, &html->unit_len_ctx, css_rect.top, css_rect.tunit));

        if (css_rect.right_auto == false)
            r.x1 = x - border_left +
                FIXTOINT(css_unit_len2device_px(box->style, &html->unit_len_ctx, css_rect.right, css_rect.runit));

        if (css_rect.bottom_auto == false)
            r.y1 = y - border_top +
                FIXTOINT(css_unit_len2device_px(box->style, &html->unit_len_ctx, css_rect.bottom, css_rect.bunit));

        /* find intersection of clip rectangle and box */
        if (r.x0 < clip->x0)
            r.x0 = clip->x0;
        if (r.y0 < clip->y0)
            r.y0 = clip->y0;
        if (clip->x1 < r.x1)
            r.x1 = clip->x1;
        if (clip->y1 < r.y1)
            r.y1 = clip->y1;
        /* Nothing to do for invalid rectangles */
        if (r.x0 >= r.x1 || r.y0 >= r.y1)
            /* not an error */
            goto cleanup;
        /* clip to it */
        if (ctx->plot->clip(ctx, &r) != NSERROR_OK) {
            {
                result = false;
                goto cleanup;
            }
        }

    } else if (box->type == BOX_BLOCK || box->type == BOX_INLINE_BLOCK || box->type == BOX_TABLE_CELL || box->object) {
        /* Use helper function for clip intersection */
        clip_result_t result = html_clip_intersect_box(&r, clip, has_transform);
        if (result == CLIP_RESULT_EMPTY) {
            /* no point trying to draw 0-width/height boxes */
            goto cleanup;
        }
        /* clip to it */
        if (ctx->plot->clip(ctx, &r) != NSERROR_OK) {
            {
                result = false;
                goto cleanup;
            }
        }
    } else {
        /* clip box is fine, clip to it */
        r = *clip;
        if (ctx->plot->clip(ctx, &r) != NSERROR_OK) {
            {
                result = false;
                goto cleanup;
            }
        }
    }

    /* background colour and image for block level content and replaced
     * inlines */

    bg_box = html_redraw_find_bg_box(box);

    /* bg_box == NULL implies that this box should not have
     * its background rendered. Otherwise filter out linebreaks,
     * optimize away non-differing inlines, only plot background
     * for BOX_TEXT it's in an inline */
    if (paint_background && bg_box && bg_box->type != BOX_BR && bg_box->type != BOX_TEXT &&
        bg_box->type != BOX_INLINE_END &&
        (bg_box->type != BOX_INLINE || bg_box->object || bg_box->flags & IFRAME || box->flags & REPLACE_DIM ||
            (bg_box->gadget != NULL &&
                (bg_box->gadget->type == GADGET_TEXTAREA || bg_box->gadget->type == GADGET_TEXTBOX ||
                    bg_box->gadget->type == GADGET_PASSWORD)))) {
        const char *tag = "";
        const char *cls = "";
        dom_string *name = NULL;
        dom_string *class_attr = NULL;
        if (box->node != NULL) {
            if (dom_node_get_node_name(box->node, &name) == DOM_NO_ERR && name != NULL) {
                tag = dom_string_data(name);
            }
            if (dom_element_get_attribute(box->node, corestring_dom_class, &class_attr) == DOM_NO_ERR &&
                class_attr != NULL) {
                cls = dom_string_data(class_attr);
            }
        }
        uint8_t bg_attach = (box->style != NULL) ? css_computed_background_attachment(box->style)
                                                 : CSS_BACKGROUND_ATTACHMENT_SCROLL;
        // TODO remove in production
        bool log_target = (cls != NULL &&
            (strstr(cls, "submenu-wrapper") || strstr(cls, "hn-container") || strstr(cls, "sub-menu")));
        bool expand_viewport_bg = false;
        if (data != NULL) {
            int vp_x = data->viewport_x;
            int root_w = data->root_width;
            int b_left = x - border_left;
            int b_right = x + padding_width + border_right;
            int target_left = (int)(-vp_x * scale);
            int target_right = (int)((-vp_x + root_w) * scale);
            int tol = 8;
            bool left_match = (b_left >= target_left - tol) && (b_left <= target_left + tol);
            bool right_match = (b_right >= target_right - tol) && (b_right <= target_right + tol);
            int root_w_scaled = (int)(root_w * scale);
            int bg_extent = padding_width + border_left + border_right;
            bool abs_full_width = (bg_extent >= root_w_scaled - tol);
            bool is_root_or_body = (box->parent == NULL) || (box->parent != NULL && box->parent->parent == NULL);

            /* Existing viewport background handling:
             * 1. background-attachment: fixed
             * 2. Root/body elements if full-width
             *
             * For all other elements, do NOT expand - log if they would have
             * matched the old heuristics so we can detect if needed. */
            if (bg_attach == CSS_BACKGROUND_ATTACHMENT_FIXED) {
                /* CSS spec: only background-attachment:fixed expands to viewport.
                 * position:fixed elements are fixed to viewport but their background
                 * respects the element's box dimensions. */
                expand_viewport_bg = true;
            } else if (is_root_or_body && ((left_match && right_match) || abs_full_width)) {
                /* Root/body elements: expand if they span the viewport width */
                expand_viewport_bg = true;
            } else if ((left_match && right_match) || abs_full_width) {
                /* Would have expanded under old heuristics - log for detection */
                NSLOG(wisp, DEBUG,
                    "VIEWPORT_EXPANSION_SKIP: tag=%s class=%s box=%p - full-width element NOT expanded "
                    "(left_match=%d right_match=%d abs_full_width=%d)",
                    tag, cls, (void *)box, left_match, right_match, abs_full_width);
            }
        }
        if (expand_viewport_bg) {
            NSLOG(layout, INFO, "bg draw pre: tag %s class %s box %p pad_w %i pad_h %i", tag, cls, box, padding_width,
                padding_height);
        }
        /* Debug: log table padding */
        if (box->type == BOX_TABLE) {
            NSLOG(layout, INFO,
                "TABLE draw: tag %s class %s box %p width %i height %i padding[L]=%d [T]=%d [R]=%d [B]=%d", tag, cls,
                (void *)box, box->width, box->height, box->padding[LEFT], box->padding[TOP], box->padding[RIGHT],
                box->padding[BOTTOM]);
        }
        /* find intersection of clip box and border edge */
        struct rect p;
        p.x0 = x - border_left < r.x0 ? r.x0 : x - border_left;
        p.y0 = y - border_top < r.y0 ? r.y0 : y - border_top;
        p.x1 = x + padding_width + border_right < r.x1 ? x + padding_width + border_right : r.x1;
        p.y1 = y + padding_height + border_bottom < r.y1 ? y + padding_height + border_bottom : r.y1;
        if (expand_viewport_bg && ctx->interactive) {
            int vp_x = (data != NULL) ? data->viewport_x : 0;
            int root_w = (data != NULL) ? data->root_width : 0;
            int vp_y = (data != NULL) ? data->viewport_y : 0;
            int root_h = (data != NULL) ? data->root_height : 0;
            if (vp_x < 0)
                vp_x = 0;
            if (root_w < 0)
                root_w = 0;
            if (vp_y < 0)
                vp_y = 0;
            if (root_h < 0)
                root_h = 0;
            p.x0 = (int)(-vp_x * scale);
            p.x1 = (int)((-vp_x + root_w) * scale);
            p.y0 = (int)(-vp_y * scale);
            p.y1 = (int)((-vp_y + root_h) * scale);
            /* Viewport backgrounds can escape the element clip, never the
             * immutable boundary of this paint pass (or child document). */
            html_clip_intersect_box(&p, paint_clip, false);
        }
        if (!box->parent) {
            /* Root element, special case:
             * background covers margins too */
            int m_left, m_top, m_right, m_bottom;
            if (scale == 1.0) {
                m_left = box->margin[LEFT];
                m_top = box->margin[TOP];
                m_right = box->margin[RIGHT];
                m_bottom = box->margin[BOTTOM];
            } else {
                m_left = box->margin[LEFT] * scale;
                m_top = box->margin[TOP] * scale;
                m_right = box->margin[RIGHT] * scale;
                m_bottom = box->margin[BOTTOM] * scale;
            }
            p.x0 = p.x0 - m_left < r.x0 ? r.x0 : p.x0 - m_left;
            p.y0 = p.y0 - m_top < r.y0 ? r.y0 : p.y0 - m_top;
            p.x1 = p.x1 + m_right < r.x1 ? p.x1 + m_right : r.x1;
            p.y1 = p.y1 + m_bottom < r.y1 ? p.y1 + m_bottom : r.y1;
        }
        /* valid clipping rectangles only */
        if ((p.x0 < p.x1) && (p.y0 < p.y1)) {
            if (expand_viewport_bg && ctx->interactive && ctx->plot->clip(ctx, &p) != NSERROR_OK) {
                result = false;
                goto cleanup;
            }
            /* plot background */
            if (!html_redraw_background(
                    x, y, box, scale, &p, &current_background_color, bg_box, &html->unit_len_ctx, ctx)) {
                {
                    result = false;
                    goto cleanup;
                }
            }
            if (expand_viewport_bg) {
                NSLOG(layout, INFO, "bg draw post: tag %s class %s box %p rect x0 %i x1 %i", tag, cls, box, p.x0, p.x1);
            }
            /* restore previous graphics window */
            if (ctx->plot->clip(ctx, &r) != NSERROR_OK) {
                {
                    result = false;
                    goto cleanup;
                }
            }
        }
        if (class_attr != NULL)
            dom_string_unref(class_attr);
        if (name != NULL)
            dom_string_unref(name);
    }

    if (!paint_background && bg_box && bg_box->style) {
        css_color bg;
        css_computed_background_color(bg_box->style, &bg);
        if (!nscss_color_is_transparent(bg))
            current_background_color = nscss_color_to_ns(bg);
    }

    /* borders for block level content and replaced inlines */
    if (paint_background && !table_has_collapsed_borders(box) && box->style && box->type != BOX_TEXT &&
        box->type != BOX_INLINE_END &&
        (box->type != BOX_INLINE || box->object || box->flags & IFRAME || box->flags & REPLACE_DIM ||
            (box->gadget != NULL &&
                (box->gadget->type == GADGET_TEXTAREA || box->gadget->type == GADGET_TEXTBOX ||
                    box->gadget->type == GADGET_PASSWORD))) &&
        (border_top || border_right || border_bottom || border_left)) {
        /* Compute unscaled box position for border drawing.
         * For normal boxes: x_parent + box->x
         * For absolute boxes: use pre-computed coordinates from box_coords */
        int border_x, border_y;
        if (box->abs_containing_block != NULL) {
            /* x,y are already scaled, but html_redraw_borders needs unscaled.
             * Divide by scale to get unscaled value. */
            if (scale != 1.0f) {
                border_x = (int)(x / scale);
                border_y = (int)(y / scale);
            } else {
                border_x = x;
                border_y = y;
            }
        } else {
            border_x = x_parent + box->x;
            border_y = y_parent + box->y;
        }
        if (!html_redraw_borders(box, border_x, border_y, padding_width, padding_height, &r, scale, ctx)) {
            {
                result = false;
                goto cleanup;
            }
        }
    }

    /* backgrounds and borders for non-replaced inlines */
    if (paint_background && box->style && box->type == BOX_INLINE &&
        (html_redraw_box_has_background(box) || border_top || border_right || border_bottom || border_left)) {
        /* inline backgrounds and borders span other boxes and may
         * wrap onto separate lines */
        struct box *ib;
        struct rect b; /* border edge rectangle */
        struct rect p; /* clipped rect */
        bool first = true;
        int ib_x;
        int ib_y = y;
        int ib_p_width;
        int ib_b_left, ib_b_right;

        b.x0 = x - border_left;
        b.x1 = x + padding_width + border_right;
        b.y0 = y - border_top;
        b.y1 = y + padding_height + border_bottom;

        p.x0 = b.x0 < r.x0 ? r.x0 : b.x0;
        p.x1 = b.x1 < r.x1 ? b.x1 : r.x1;
        p.y0 = b.y0 < r.y0 ? r.y0 : b.y0;
        p.y1 = b.y1 < r.y1 ? b.y1 : r.y1;
        for (ib = box; ib; ib = ib->next) {
            /* to get extents of rectangle(s) associated with
             * inline, cycle though all boxes in inline, skipping
             * over floats */
            if (ib->type == BOX_FLOAT_LEFT || ib->type == BOX_FLOAT_RIGHT)
                continue;
            if (scale == 1.0) {
                ib_x = x_parent + ib->x;
                ib_y = y_parent + ib->y;
                ib_p_width = ib->padding[LEFT] + ib->width + ib->padding[RIGHT];
                ib_b_left = ib->border[LEFT].width;
                ib_b_right = ib->border[RIGHT].width;
            } else {
                ib_x = (x_parent + ib->x) * scale;
                ib_y = (y_parent + ib->y) * scale;
                ib_p_width = (ib->padding[LEFT] + ib->width + ib->padding[RIGHT]) * scale;
                ib_b_left = ib->border[LEFT].width * scale;
                ib_b_right = ib->border[RIGHT].width * scale;
            }

            if ((ib->flags & NEW_LINE) && ib != box) {
                /* inline element has wrapped, plot background
                 * and borders */
                if (!html_redraw_inline_background(
                        x, y, box, scale, &p, b, first, false, &current_background_color, &html->unit_len_ctx, ctx)) {
                    {
                        result = false;
                        goto cleanup;
                    }
                }
                /* restore previous graphics window */
                if (ctx->plot->clip(ctx, &r) != NSERROR_OK) {
                    {
                        result = false;
                        goto cleanup;
                    }
                }
                if (!html_redraw_inline_borders(box, b, &r, scale, first, false, ctx)) {
                    {
                        result = false;
                        goto cleanup;
                    }
                }
                /* reset coords */
                b.x0 = ib_x - ib_b_left;
                b.y0 = ib_y - border_top - padding_top;
                b.y1 = ib_y + padding_height - padding_top + border_bottom;

                p.x0 = b.x0 < r.x0 ? r.x0 : b.x0;
                p.y0 = b.y0 < r.y0 ? r.y0 : b.y0;
                p.y1 = b.y1 < r.y1 ? b.y1 : r.y1;

                first = false;
            }

            /* increase width for current box */
            b.x1 = ib_x + ib_p_width + ib_b_right;
            p.x1 = b.x1 < r.x1 ? b.x1 : r.x1;

            if (!box->inline_end || ib == box->inline_end)
                /* An empty inline fragment has no separate end marker. */
                break;
        }
        /* plot background and borders for last rectangle of
         * the inline */
        if (!html_redraw_inline_background(
                x, ib_y, box, scale, &p, b, first, true, &current_background_color, &html->unit_len_ctx, ctx)) {
            {
                result = false;
                goto cleanup;
            }
        }
        /* restore previous graphics window */
        if (ctx->plot->clip(ctx, &r) != NSERROR_OK) {
            {
                result = false;
                goto cleanup;
            }
        }
        if (!html_redraw_inline_borders(box, b, &r, scale, first, true, ctx)) {
            {
                result = false;
                goto cleanup;
            }
        }
    }

    /* Debug outlines */
    if (paint_background && html_redraw_debug) {
        int margin_left, margin_right;
        int margin_top, margin_bottom;
        if (scale == 1.0) {
            /* avoid trivial fp maths */
            margin_left = box->margin[LEFT];
            margin_top = box->margin[TOP];
            margin_right = box->margin[RIGHT];
            margin_bottom = box->margin[BOTTOM];
        } else {
            margin_left = box->margin[LEFT] * scale;
            margin_top = box->margin[TOP] * scale;
            margin_right = box->margin[RIGHT] * scale;
            margin_bottom = box->margin[BOTTOM] * scale;
        }
        /* Content edge -- blue */
        rect.x0 = x + padding_left;
        rect.y0 = y + padding_top;
        rect.x1 = x + padding_left + width;
        rect.y1 = y + padding_top + height;
        if (ctx->plot->rectangle(ctx, plot_style_content_edge, &rect) != NSERROR_OK) {
            {
                result = false;
                goto cleanup;
            }
        }

        /* Padding edge -- red */
        rect.x0 = x;
        rect.y0 = y;
        rect.x1 = x + padding_width;
        rect.y1 = y + padding_height;
        if (ctx->plot->rectangle(ctx, plot_style_padding_edge, &rect) != NSERROR_OK) {
            {
                result = false;
                goto cleanup;
            }
        }

        /* Margin edge -- yellow */
        rect.x0 = x - border_left - margin_left;
        rect.y0 = y - border_top - margin_top;
        rect.x1 = x + padding_width + border_right + margin_right;
        rect.y1 = y + padding_height + border_bottom + margin_bottom;
        if (ctx->plot->rectangle(ctx, plot_style_margin_edge, &rect) != NSERROR_OK) {
            {
                result = false;
                goto cleanup;
            }
        }
    }

    /* clip to the padding edge for objects, or boxes with overflow hidden
     * or scroll, unless it's the root element.
     * Skip for transformed boxes - the visual bounds differ from box coords. */
    if (box->parent != NULL && !has_transform) {
        bool need_clip = false;
        if (box->object || box->flags & IFRAME ||
            (overflow_x != CSS_OVERFLOW_VISIBLE && overflow_y != CSS_OVERFLOW_VISIBLE)) {
            r.x0 = x;
            r.y0 = y;
            r.x1 = x + padding_width;
            r.y1 = y + padding_height;
            if (r.x0 < clip->x0)
                r.x0 = clip->x0;
            if (r.y0 < clip->y0)
                r.y0 = clip->y0;
            if (clip->x1 < r.x1)
                r.x1 = clip->x1;
            if (clip->y1 < r.y1)
                r.y1 = clip->y1;
            if (r.x1 <= r.x0 || r.y1 <= r.y0) {
                return (!ctx->plot->group_end || (ctx->plot->group_end(ctx) == NSERROR_OK));
            }
            need_clip = true;

        } else if (overflow_x != CSS_OVERFLOW_VISIBLE) {
            r.x0 = x;
            r.y0 = clip->y0;
            r.x1 = x + padding_width;
            r.y1 = clip->y1;
            if (r.x0 < clip->x0)
                r.x0 = clip->x0;
            if (clip->x1 < r.x1)
                r.x1 = clip->x1;
            if (r.x1 <= r.x0) {
                return (!ctx->plot->group_end || (ctx->plot->group_end(ctx) == NSERROR_OK));
            }
            need_clip = true;

        } else if (overflow_y != CSS_OVERFLOW_VISIBLE) {
            r.x0 = clip->x0;
            r.y0 = y;
            r.x1 = clip->x1;
            r.y1 = y + padding_height;
            if (r.y0 < clip->y0)
                r.y0 = clip->y0;
            if (clip->y1 < r.y1)
                r.y1 = clip->y1;
            if (r.y1 <= r.y0) {
                return (!ctx->plot->group_end || (ctx->plot->group_end(ctx) == NSERROR_OK));
            }
            need_clip = true;
        }

        if (need_clip &&
            (box->type == BOX_BLOCK || box->type == BOX_INLINE_BLOCK || box->type == BOX_TABLE_CELL || box->object)) {
            if (ctx->plot->clip(ctx, &r) != NSERROR_OK) {
                {
                    result = false;
                    goto cleanup;
                }
            }
        }
    }

    const struct rect *child_clip = overflow_x == CSS_OVERFLOW_VISIBLE && overflow_y == CSS_OVERFLOW_VISIBLE ? clip
                                                                                                             : &r;

    /* text decoration */
    if (paint_content && (phase != BOX_PAINT_ALL || !box->children) && box->type != BOX_TEXT && box->style &&
        css_computed_text_decoration(box->style) != CSS_TEXT_DECORATION_NONE) {
        if (!html_redraw_text_decoration(box, x_parent, y_parent, scale, current_background_color, ctx)) {
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if (paint_content && box->node != NULL) {
        exc = dom_html_element_get_tag_type(box->node, &tag_type);
        if (exc != DOM_NO_ERR) {
            tag_type = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
        }
    } else {
        tag_type = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
    }


    if (!paint_content) {
        if (!(box->flags & CONTENTS_SKIPPED) &&
            !html_redraw_box_children(html, box, x_parent, y_parent, child_clip, scale, current_background_color, data,
                paint_clip, ctx, phase)) {
            result = false;
            goto cleanup;
        }
    } else if (box->flags & CONTENTS_SKIPPED) {
        /* Background and borders above belong to the principal box. */
    } else if (box->object && width != 0 && height != 0) {
        struct content_redraw_data obj_data;
        struct rect object_clip = r;

        x_scrolled = x - scrollbar_get_offset(box->scroll_x) * scale;
        y_scrolled = y - scrollbar_get_offset(box->scroll_y) * scale;

        /* Get object-fit value from style (default: fill) */
        uint8_t object_fit = CSS_OBJECT_FIT_FILL;
        if (box->style) {
            object_fit = css_computed_object_fit(box->style);
        }

        /* Get intrinsic dimensions of content */
        int intrinsic_width = content_get_width(box->object);
        int intrinsic_height = content_get_height(box->object);

        /* Calculate render dimensions based on object-fit and object-position */
        int render_width, render_height, offset_x, offset_y;
        calculate_object_fit_dimensions(box->style, &html->unit_len_ctx, object_fit, width, height, intrinsic_width,
            intrinsic_height, &render_width, &render_height, &offset_x, &offset_y);

        /* Position: base position + padding + centering offset (all scaled) */
        obj_data.x = x_scrolled + padding_left + offset_x * scale;
        obj_data.y = y_scrolled + padding_top + offset_y * scale;
        /* Dimensions: pass unscaled, content_redraw applies obj_data.scale */
        obj_data.width = render_width;
        obj_data.height = render_height;
        /* If this box has a transparent background, don't fill behind the image.
         * Otherwise use the inherited background color for alpha blending. */
        if (box->style != NULL) {
            css_color bgcol;
            css_computed_background_color(box->style, &bgcol);
            if (nscss_color_is_transparent(bgcol)) {
                obj_data.background_colour = NS_TRANSPARENT;
            } else {
                obj_data.background_colour = current_background_color;
            }
        } else {
            obj_data.background_colour = current_background_color;
        }
        obj_data.scale = scale;
        obj_data.repeat_x = false;
        obj_data.repeat_y = false;

        /* For cover/none/scale-down modes, clip to box bounds to crop overflow. */
        if (object_fit == CSS_OBJECT_FIT_COVER || object_fit == CSS_OBJECT_FIT_NONE ||
            object_fit == CSS_OBJECT_FIT_SCALE_DOWN) {
            int box_left = x_scrolled + padding_left;
            int box_top = y_scrolled + padding_top;
            int box_right = box_left + width * scale;
            int box_bottom = box_top + height * scale;

            /* Use helper function - skips intersection for transformed boxes */
            html_clip_intersect_object_fit(&object_clip, box_left, box_top, box_right, box_bottom, has_transform);
        }


        if (content_get_type(box->object) == CONTENT_HTML) {
            obj_data.x /= scale;
            obj_data.y /= scale;
        }

        NSLOG(plot, INFO,
            "REDRAW_IMG box=%p box_w=%d box_h=%d scaled_w=%d scaled_h=%d render=%dx%d scale=%.2f intrinsic=%dx%d", box,
            box->width, box->height, width, height, render_width, render_height, scale, intrinsic_width,
            intrinsic_height);
        NSLOG(plot, INFO,
            "REDRAW_IMG obj_data: x=%d y=%d w=%d h=%d obj_clip: x0=%d y0=%d x1=%d y1=%d (clip_size=%dx%d)", obj_data.x,
            obj_data.y, obj_data.width, obj_data.height, object_clip.x0, object_clip.y0, object_clip.x1, object_clip.y1,
            object_clip.x1 - object_clip.x0, object_clip.y1 - object_clip.y0);

        if (!content_redraw(box->object, &obj_data, &object_clip, ctx)) {
            const char *tag = "";
            const char *cls = "";
            dom_string *name = NULL;
            dom_string *class_attr = NULL;
            if (box->node != NULL) {
                if (dom_node_get_node_name(box->node, &name) == DOM_NO_ERR && name != NULL) {
                    tag = dom_string_data(name);
                }
                if (dom_element_get_attribute(box->node, corestring_dom_class, &class_attr) == DOM_NO_ERR &&
                    class_attr != NULL) {
                    cls = dom_string_data(class_attr);
                }
            }
            const char *url = NULL;
            const char *mime_c = NULL;
            {
                nsurl *nurl = content_get_url(hlcache_handle_get_content(box->object));
                url = nurl ? nsurl_access(nurl) : NULL;
                lwc_string *mime = content_get_mime_type(box->object);
                mime_c = mime ? lwc_string_data(mime) : NULL;
                if (mime)
                    lwc_string_unref(mime);
            }
            NSLOG(wisp, ERROR,
                "Object redraw failed; red square shown for element tag=%s class=%s mime=%s url=%s size=%dx%d",
                tag ? tag : "", cls ? cls : "", mime_c ? mime_c : "", url ? url : "", width, height);
            if (class_attr != NULL)
                dom_string_unref(class_attr);
            if (name != NULL)
                dom_string_unref(name);
            /* Show image fail */
            /* Unicode (U+FFFC) 'OBJECT REPLACEMENT CHARACTER' */
            static const char obj[] = "\xef\xbf\xbc";
            int obj_width;
            int obj_x = x + padding_left;
            nserror res;

            rect.x0 = x + padding_left;
            rect.y0 = y + padding_top;
            rect.x1 = x + padding_left + width - 1;
            rect.y1 = y + padding_top + height - 1;
            res = ctx->plot->rectangle(ctx, plot_style_broken_object, &rect);
            if (res != NSERROR_OK) {
                {
                    {
                        result = false;
                        goto cleanup;
                    }
                }
            }

            res = guit->layout->width(plot_fstyle_broken_object, obj, sizeof(obj) - 1, &obj_width);
            if (res != NSERROR_OK) {
                obj_x += 1;
            } else {
                obj_x += width / 2 - obj_width / 2;
            }

            if (ctx->plot->text(ctx, plot_fstyle_broken_object, obj_x, y + padding_top + (int)(height * 0.75), obj,
                    sizeof(obj) - 1) != NSERROR_OK) {
                {
                    result = false;
                    goto cleanup;
                }
            }
        }
    } else if (tag_type == DOM_HTML_ELEMENT_TYPE_CANVAS && box->node != NULL && box->flags & REPLACE_DIM) {
        /* Canvas to draw */
        struct bitmap *bitmap = NULL;
        exc = dom_node_get_user_data(box->node, corestring_dom___ns_key_canvas_node_data, &bitmap);
        if (exc != DOM_NO_ERR) {
            bitmap = NULL;
        }
        if (bitmap != NULL &&
            ctx->plot->bitmap(ctx, bitmap, x + padding_left, y + padding_top, width, height, current_background_color,
                BITMAPF_NONE) != NSERROR_OK) {
            {
                result = false;
                goto cleanup;
            }
        }
    } else if (box->iframe) {
        /* Offset is passed to browser window redraw unscaled */
        if (!browser_window_redraw(box->iframe, x + padding_left, y + padding_top, &r, ctx)) {
            result = false;
            goto cleanup;
        }

    } else if (box->gadget && box->gadget->type == GADGET_CHECKBOX) {
        if (!html_redraw_checkbox(x + padding_left, y + padding_top, width, height, box->gadget->selected, ctx)) {
            {
                result = false;
                goto cleanup;
            }
        }

    } else if (box->gadget && box->gadget->type == GADGET_RADIO) {
        if (!html_redraw_radio(x + padding_left, y + padding_top, width, height, box->gadget->selected, ctx)) {
            {
                result = false;
                goto cleanup;
            }
        }

    } else if (box->gadget && box->gadget->type == GADGET_FILE) {
        if (!html_redraw_file(
                x + padding_left, y + padding_top, width, height, box, scale, current_background_color, html, ctx)) {
            {
                result = false;
                goto cleanup;
            }
        }

    } else if (box->gadget &&
        (box->gadget->type == GADGET_TEXTAREA || box->gadget->type == GADGET_PASSWORD ||
            box->gadget->type == GADGET_TEXTBOX)) {
        textarea_redraw(box->gadget->data.text.ta, x, y, current_background_color, scale, &r, ctx);

    } else if (box->text) {
        if (!html_redraw_text_box(html, box, x, y, &r, scale, current_background_color, ctx)) {
            {
                result = false;
                goto cleanup;
            }
        }

    } else {
        /* Parent coordinates were normalized before painting; retain their
         * integer layout precision instead of dividing rounded device pixels. */
        if (!html_redraw_box_children(html, box, x_parent, y_parent, child_clip, scale, current_background_color, data,
                paint_clip, ctx, phase)) {
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if (box->type == BOX_BLOCK || box->type == BOX_INLINE_BLOCK || box->type == BOX_TABLE_CELL ||
        box->type == BOX_INLINE)
        if (ctx->plot->clip(ctx, clip) != NSERROR_OK) {
            {
                result = false;
                goto cleanup;
            }
        }

    /* list marker */
    if (paint_content && box_has_outside_marker(box)) {
        if (!html_redraw_box(html, box->list_marker, x_parent + box->x - scrollbar_get_offset(box->scroll_x),
                y_parent + box->y - scrollbar_get_offset(box->scroll_y), clip, scale, current_background_color, data,
                paint_clip, ctx, BOX_PAINT_ALL)) {
            {
                result = false;
                goto cleanup;
            }
        }
    }

    /* scrollbars */
    if (paint_content &&
        ((box->style && box->type != BOX_BR && box->type != BOX_TABLE && box->type != BOX_INLINE &&
             box->type != BOX_FLEX && box->type != BOX_INLINE_FLEX && box->type != BOX_GRID &&
             box->type != BOX_INLINE_GRID && (box->gadget == NULL || box->gadget->type != GADGET_TEXTAREA) &&
             (overflow_x == CSS_OVERFLOW_SCROLL || overflow_x == CSS_OVERFLOW_AUTO ||
                 overflow_y == CSS_OVERFLOW_SCROLL || overflow_y == CSS_OVERFLOW_AUTO)) ||
            (box->object && content_get_type(box->object) == CONTENT_HTML)) &&
        box->parent != NULL && box->parent != html->layout) {
        nserror res;
        bool has_x_scroll = (overflow_x == CSS_OVERFLOW_SCROLL);
        bool has_y_scroll = (overflow_y == CSS_OVERFLOW_SCROLL);

        has_x_scroll |= (overflow_x == CSS_OVERFLOW_AUTO) && box_hscrollbar_present(box);
        has_y_scroll |= (overflow_y == CSS_OVERFLOW_AUTO) && box_vscrollbar_present(box);

        res = box_handle_scrollbars((struct content *)html, box, has_x_scroll, has_y_scroll);
        if (res != NSERROR_OK) {
            NSLOG(wisp, INFO, "%s", messages_get_errorcode(res));
            {
                {
                    result = false;
                    goto cleanup;
                }
            }
        }

        if (box->scroll_x != NULL)
            scrollbar_redraw(box->scroll_x, x_parent + box->x,
                y_parent + box->y + box->padding[TOP] + box->height + box->padding[BOTTOM] - SCROLLBAR_WIDTH, clip,
                scale, ctx);
        if (box->scroll_y != NULL)
            scrollbar_redraw(box->scroll_y,
                x_parent + box->x + box->padding[LEFT] + box->width + box->padding[RIGHT] - SCROLLBAR_WIDTH,
                y_parent + box->y, clip, scale, ctx);
    }

    if (box->type == BOX_BLOCK || box->type == BOX_INLINE_BLOCK || box->type == BOX_TABLE_CELL ||
        box->type == BOX_INLINE) {
        if (ctx->plot->clip(ctx, clip) != NSERROR_OK) {
            goto cleanup;
        }
    }

cleanup:
    /* Pop any transform that was pushed */
    if (has_transform && ctx->plot->pop_transform != NULL) {
        ctx->plot->pop_transform(ctx);
    }

    return result;
}

/**
 * Draw a CONTENT_HTML using the current set of plotters (plot).
 *
 * \param  c	 content of type CONTENT_HTML
 * \param  data	 redraw data for this content redraw
 * \param  clip	 current clip region
 * \param  ctx	 current redraw context
 * \return true if successful, false otherwise
 *
 * x, y, clip_[xy][01] are in target coordinates.
 */

bool html_redraw(
    struct content *c, struct content_redraw_data *data, const struct rect *clip, const struct redraw_context *ctx)
{
    html_content *html = (html_content *)c;
    struct box *box;
    bool result = true;
    bool select, select_only;
    plot_style_t pstyle_fill_bg = {
        .fill_type = PLOT_OP_TYPE_SOLID,
        .fill_colour = data->background_colour,
    };
    /* A script can leave a completed document without a root. Clear its old
     * pixels; a temporary gap during box construction retains the old paint. */
    if (html->layout == NULL)
        return html->box_conversion_context != NULL || ctx->plot->rectangle(ctx, &pstyle_fill_bg, clip) == NSERROR_OK;

    NSLOG(wisp, DEBUG, "PROFILER: START HTML redraw %p", c);
    HTML_PROFILE_START(paint_start);
    HTML_PROFILE_CPU_START(paint_cpu_start);

    box = html->layout;

    /* The select menu needs special treating because, when opened, it
     * reaches beyond its layout box.
     */
    select = false;
    select_only = false;
    if (ctx->interactive && html->visible_select_menu != NULL) {
        struct form_control *control = html->visible_select_menu;
        select = true;
        /* check if the redraw rectangle is completely inside of the
           select menu */
        select_only = form_clip_inside_select_menu(control, data->scale, clip);
    }

    if (!select_only) {
        /* clear to background colour */
        result = (ctx->plot->clip(ctx, clip) == NSERROR_OK);

        if (html->background_colour != NS_TRANSPARENT)
            pstyle_fill_bg.fill_colour = html->background_colour;

        result &= (ctx->plot->rectangle(ctx, &pstyle_fill_bg, clip) == NSERROR_OK);

        result &= html_redraw_box(
            html, box, data->x, data->y, clip, data->scale, pstyle_fill_bg.fill_colour, data, clip, ctx, BOX_PAINT_ALL);
    }

    if (select) {
        int menu_x, menu_y;
        box = html->visible_select_menu->box;
        box_coords(box, &menu_x, &menu_y);

        menu_x -= box->border[LEFT].width;
        menu_y += box->height + box->border[BOTTOM].width + box->padding[BOTTOM] + box->padding[TOP];
        result &= form_redraw_select_menu(
            html->visible_select_menu, data->x + menu_x, data->y + menu_y, data->scale, clip, ctx);
    }

    NSLOG(wisp, DEBUG, "PROFILER: STOP HTML redraw %p", c);
    HTML_PROFILE(html->render_profile.paint_calls++; html->render_profile.paint_ns += html_profile_now() - paint_start);
    HTML_PROFILE(if (paint_cpu_start) html->render_profile.paint_cpu_ns += html_profile_cpu_now() - paint_cpu_start);
    HTML_PROFILE(if (html->restyle_profile.start_ns != 0 && !html->restyling) {
        uint64_t end = html_profile_now();
        html->restyle_profile.paint_ns += end - paint_start;
        html->restyle_profile.last_paint_ns = end;
        html->restyle_profile.paint_calls++;
    });
    return result;
}
