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

#include <dom/dom.h>
#include <neosurf/utils/config.h>
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libwapcaplet/libwapcaplet.h>
#include <neosurf/bitmap.h>
#include <neosurf/browser_window.h>
#include <neosurf/content.h>
#include <neosurf/content/content.h>
#include <neosurf/content/content_protected.h>
#include <neosurf/content/handlers/css/utils.h>
#include <neosurf/content/hlcache.h>
#include <neosurf/desktop/gui_internal.h>
#include <neosurf/desktop/print.h>
#include <neosurf/desktop/textarea.h>
#include <neosurf/layout.h>
#include <neosurf/plotters.h>
#include <neosurf/utils/corestrings.h>
#include <neosurf/utils/log.h>
#include <neosurf/utils/messages.h>
#include <neosurf/utils/nsoption.h>
#include <neosurf/utils/utils.h>
#include "content/textsearch.h"
#include "desktop/browser_private.h"
#include "desktop/scrollbar.h"
#include "desktop/selection.h"

#include <libcss/gradient.h>
#include <neosurf/content/handlers/html/box.h>
#include <neosurf/content/handlers/html/box_inspect.h>
#include <neosurf/content/handlers/html/form_internal.h>
#include <neosurf/content/handlers/html/private.h>
#include "content/handlers/html/box_manipulate.h"
#include "content/handlers/html/font.h"
#include "content/handlers/html/layout.h"
#include "content/handlers/html/redraw_helpers.h"
#include "content/handlers/html/stacking.h"
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
        uint8_t bg_type = css_computed_background_image(box->style, &url);
        if (bg_type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT)
            return true;
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
static void calculate_object_fit_dimensions(const css_computed_style *style, uint8_t object_fit, int box_width,
    int box_height, int intrinsic_width, int intrinsic_height, int *render_width, int *render_height, int *offset_x,
    int *offset_y)
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
            calculate_object_fit_dimensions(style, CSS_OBJECT_FIT_CONTAIN, box_width, box_height, intrinsic_width,
                intrinsic_height, render_width, render_height, offset_x, offset_y);
            return;
        }
        break;
    }

    /* Calculate object-position offsets */
    css_fixed hlength = 0, vlength = 0;
    css_unit hunit = CSS_UNIT_PCT, vunit = CSS_UNIT_PCT;

    if (style != NULL) {
        uint8_t type = css_computed_object_position(style, &hlength, &hunit, &vlength, &vunit);
        (void)type; /* Silence unused variable warning */
    } else {
        /* Default: 50% 50% (center center) */
        hlength = INTTOFIX(50);
        vlength = INTTOFIX(50);
    }

    /* Calculate horizontal offset */
    int available_x = box_width - *render_width;
    if (hunit == CSS_UNIT_PCT) {
        /* Percentage: offset = (available space) * (percentage / 100) */
        *offset_x = FIXTOINT(FDIV(FMUL(INTTOFIX(available_x), hlength), INTTOFIX(100)));
    } else {
        /* Length unit - for now treat as pixels (proper unit conversion would need unit_ctx) */
        *offset_x = FIXTOINT(hlength);
    }

    /* Calculate vertical offset */
    int available_y = box_height - *render_height;
    if (vunit == CSS_UNIT_PCT) {
        /* Percentage: offset = (available space) * (percentage / 100) */
        *offset_y = FIXTOINT(FDIV(FMUL(INTTOFIX(available_y), vlength), INTTOFIX(100)));
    } else {
        /* Length unit - treat as pixels */
        *offset_y = FIXTOINT(vlength);
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
                (ctx->plot->text(ctx, &plot_fstyle, x, y + (int)(height * 0.75 * scale), utf8_text, start_idx) !=
                    NSERROR_OK))
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
                (ctx->plot->text(ctx, &fstyle_hback, x, y + (int)(height * 0.75 * scale), utf8_text, endtxt_idx) !=
                    NSERROR_OK)) {
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

                    res = ctx->plot->text(ctx, &plot_fstyle, x, y + (int)(height * 0.75 * scale), utf8_text, utf8_len);
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
        res = ctx->plot->text(ctx, &plot_fstyle, x, y + (int)(height * 0.75 * scale), utf8_text, utf8_len);
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
 * \param  unit_len_ctx   Length conversion context
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_file(int x, int y, int width, int height, struct box *box, float scale,
    colour background_colour, const css_unit_ctx *unit_len_ctx, const struct redraw_context *ctx)
{
    int text_width;
    const char *text;
    size_t length;
    plot_font_style_t fstyle;
    nserror res;

    font_plot_style_from_css(unit_len_ctx, box->style, &fstyle);
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


/**
 * Interpolate between two colors.
 *
 * \param c1  First color (CSS AARRGGBB format)
 * \param c2  Second color (CSS AARRGGBB format)
 * \param t   Interpolation factor (0.0 to 1.0)
 * \return Interpolated color in NetSurf format
 */
static colour gradient_interpolate_color(css_color c1, css_color c2, float t)
{
    if (t <= 0.0f)
        return nscss_color_to_ns(c1);
    if (t >= 1.0f)
        return nscss_color_to_ns(c2);

    /* Extract ARGB components from CSS AARRGGBB format */
    unsigned int a1 = (c1 >> 24) & 0xff;
    unsigned int r1 = (c1 >> 16) & 0xff;
    unsigned int g1 = (c1 >> 8) & 0xff;
    unsigned int b1 = (c1 >> 0) & 0xff;

    unsigned int a2 = (c2 >> 24) & 0xff;
    unsigned int r2 = (c2 >> 16) & 0xff;
    unsigned int g2 = (c2 >> 8) & 0xff;
    unsigned int b2 = (c2 >> 0) & 0xff;

    /* Linear interpolation */
    unsigned int a = (unsigned int)(a1 + t * ((float)a2 - (float)a1) + 0.5f);
    unsigned int r = (unsigned int)(r1 + t * ((float)r2 - (float)r1) + 0.5f);
    unsigned int g = (unsigned int)(g1 + t * ((float)g2 - (float)g1) + 0.5f);
    unsigned int b = (unsigned int)(b1 + t * ((float)b2 - (float)b1) + 0.5f);

    /* Clamp values */
    if (a > 255)
        a = 255;
    if (r > 255)
        r = 255;
    if (g > 255)
        g = 255;
    if (b > 255)
        b = 255;

    /* Construct CSS color and convert to NetSurf format */
    css_color result = (a << 24) | (r << 16) | (g << 8) | b;
    return nscss_color_to_ns(result);
}


/**
 * Render a linear gradient.
 *
 * \param gradient  Gradient data from computed style
 * \param r         Rectangle to fill with gradient
 * \param scale     Current scale factor
 * \param ctx       Redraw context
 * \return true on success, false on failure
 */
static bool html_redraw_linear_gradient(
    const css_linear_gradient *gradient, const struct rect *r, float scale, const struct redraw_context *ctx)
{
    if (gradient == NULL || gradient->stop_count < 2)
        return true; /* Nothing to draw */

    int width = r->x1 - r->x0;
    int height = r->y1 - r->y0;
    if (width <= 0 || height <= 0)
        return true;

#ifdef NEOSURF_USE_NATIVE_GRADIENTS
    /* Native gradient rendering - compile-time selected */
    NSLOG(plot, DEBUG, "Linear gradient: Using NATIVE rendering path");

    /* Build gradient stops array */
    struct gradient_stop *stops = alloca(gradient->stop_count * sizeof(struct gradient_stop));
    for (unsigned int i = 0; i < gradient->stop_count; i++) {
        stops[i].color = nscss_color_to_ns(gradient->stops[i].color);
        stops[i].offset = FIXTOFLT(gradient->stops[i].offset) / 100.0f;
    }

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
    ctx->plot->clip(ctx, r);
    if (ctx->plot->linear_gradient != NULL) {
        NSLOG(plot, DEBUG, "Linear gradient: Calling native plotter (%.1f,%.1f) to (%.1f,%.1f) with %u stops", x0, y0,
            x1, y1, gradient->stop_count);
        /* CSS gradients use clip rect, not path - pass NULL for path */
        nserror err = ctx->plot->linear_gradient(ctx, NULL, 0, NULL, x0, y0, x1, y1, stops, gradient->stop_count);
        if (err == NSERROR_OK) {
            NSLOG(plot, DEBUG, "Linear gradient: Native plotter succeeded");
            return true;
        }
        NSLOG(plot, WARNING, "Linear gradient: Native plotter FAILED with error %d", err);
    } else {
        NSLOG(plot, WARNING, "Linear gradient: Native plotter is NULL!");
    }
    /* Native gradient not available or failed - fill with solid color as fallback */
    {
        plot_style_t pstyle = {
            .fill_type = PLOT_OP_TYPE_SOLID,
            .fill_colour = nscss_color_to_ns(gradient->stops[0].color),
        };
        ctx->plot->rectangle(ctx, &pstyle, r);
    }
    return true;

#else /* !NEOSURF_USE_NATIVE_GRADIENTS */
    /* Fallback: strip-based rendering */
    NSLOG(plot, DEBUG, "Linear gradient: Using FALLBACK strip-based rendering");
    bool is_vertical = (gradient->direction == CSS_GRADIENT_TO_BOTTOM || gradient->direction == CSS_GRADIENT_TO_TOP);
    int total_length = is_vertical ? height : width;
    bool reversed = (gradient->direction == CSS_GRADIENT_TO_TOP || gradient->direction == CSS_GRADIENT_TO_LEFT);

    /* Draw gradient using strips */
    int num_strips = (total_length > 200) ? 100 : (total_length > 50) ? 50 : total_length;
    if (num_strips < 2)
        num_strips = 2;

    for (int strip = 0; strip < num_strips; strip++) {
        /* Calculate position along gradient (0.0 to 1.0) */
        float pos = (float)strip / (float)(num_strips - 1);
        if (reversed)
            pos = 1.0f - pos;

        /* Convert to percentage (fixed point uses 0-INTTOFIX(100)) */
        float pos_pct = pos * 100.0f;

        /* Find which two stops we're between */
        int stop_idx = 0;
        for (int i = 0; i < (int)gradient->stop_count - 1; i++) {
            float stop_pos = FIXTOFLT(gradient->stops[i + 1].offset);
            if (pos_pct <= stop_pos) {
                stop_idx = i;
                break;
            }
            stop_idx = i;
        }

        /* Interpolate color between stops */
        float stop1_pos = FIXTOFLT(gradient->stops[stop_idx].offset);
        float stop2_pos = FIXTOFLT(gradient->stops[stop_idx + 1].offset);
        float range = stop2_pos - stop1_pos;
        float t = (range > 0.001f) ? (pos_pct - stop1_pos) / range : 0.0f;

        colour strip_color = gradient_interpolate_color(
            gradient->stops[stop_idx].color, gradient->stops[stop_idx + 1].color, t);

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
#endif /* NEOSURF_USE_NATIVE_GRADIENTS */
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
static bool html_redraw_radial_gradient(
    const css_radial_gradient *gradient, const struct rect *r, float scale, const struct redraw_context *ctx)
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
    ctx->plot->clip(ctx, r);

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

#ifdef NEOSURF_USE_NATIVE_RADIAL_GRADIENTS
    /* Native radial gradient rendering - compile-time selected */
    NSLOG(plot, DEBUG, "Radial gradient: Using NATIVE rendering path");
    if (ctx->plot->radial_gradient != NULL) {
        /* Build gradient stops array */
        struct gradient_stop *stops = alloca(gradient->stop_count * sizeof(struct gradient_stop));
        for (unsigned int i = 0; i < gradient->stop_count; i++) {
            stops[i].color = nscss_color_to_ns(gradient->stops[i].color);
            stops[i].offset = FIXTOFLT(gradient->stops[i].offset) / 100.0f;
        }

        NSLOG(plot, DEBUG, "Radial gradient: Calling native plotter (%.1f,%.1f) rx=%.1f ry=%.1f with %u stops", cx, cy,
            rx, ry, gradient->stop_count);
        /* CSS gradients use clip rect, not path - pass NULL for path */
        nserror err = ctx->plot->radial_gradient(ctx, NULL, 0, NULL, cx, cy, rx, ry, stops, gradient->stop_count);
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
        float pos_pct = pos * 100.0f;

        /* Find which two stops we're between */
        int stop_idx = 0;
        for (int i = 0; i < (int)gradient->stop_count - 1; i++) {
            float stop_pos = FIXTOFLT(gradient->stops[i + 1].offset);
            if (pos_pct <= stop_pos) {
                stop_idx = i;
                break;
            }
            stop_idx = i;
        }

        /* Interpolate color between stops */
        float stop1_pos = FIXTOFLT(gradient->stops[stop_idx].offset);
        float stop2_pos = FIXTOFLT(gradient->stops[stop_idx + 1].offset);
        float range = stop2_pos - stop1_pos;
        float t = (range > 0.001f) ? (pos_pct - stop1_pos) / range : 0.0f;

        colour ring_color = gradient_interpolate_color(
            gradient->stops[stop_idx].color, gradient->stops[stop_idx + 1].color, t);

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
    bool repeat_x = false;
    bool repeat_y = false;
    bool plot_colour = true;
    bool plot_content;
    bool clip_to_children = false;
    struct box *clip_box = box;
    int ox = x, oy = y;
    int width, height;
    css_fixed hpos = 0, vpos = 0;
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

    plot_content = (background->background != NULL);

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
        switch (css_computed_background_repeat(background->style)) {
        case CSS_BACKGROUND_REPEAT_REPEAT:
            repeat_x = repeat_y = true;
            /* optimisation: only plot the colour if
             * bitmap is not opaque */
            plot_colour = !content_get_opaque(background->background);
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
        css_computed_background_position(background->style, &hpos, &hunit, &vpos, &vunit);
        if (hunit == CSS_UNIT_PCT) {
            x += (width - content_get_width(background->background)) * scale * FIXTOFLT(hpos) / 100.;
        } else {
            x += (int)(FIXTOFLT(css_unit_len2device_px(background->style, unit_len_ctx, hpos, hunit)) * scale);
        }

        if (vunit == CSS_UNIT_PCT) {
            y += (height - content_get_height(background->background)) * scale * FIXTOFLT(vpos) / 100.;
        } else {
            y += (int)(FIXTOFLT(css_unit_len2device_px(background->style, unit_len_ctx, vpos, vunit)) * scale);
        }
    }

    /* special case for table rows as their background needs
     * to be clipped to all the cells */
    if (box->type == BOX_TABLE_ROW) {
        css_fixed h = 0, v = 0;
        css_unit hu = CSS_UNIT_PX, vu = CSS_UNIT_PX;

        for (parent = box->parent; ((parent) && (parent->type != BOX_TABLE)); parent = parent->parent)
            ;
        assert(parent && (parent->style));

        css_computed_border_spacing(parent->style, &h, &hu, &v, &vu);

        clip_to_children = (h > 0) || (v > 0);

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

            /* <td> attributes override <tr> */
            /* if the background content is opaque there
             * is no need to plot underneath it.
             */
            if ((r.x0 >= r.x1) || (r.y0 >= r.y1) || (nscss_color_is_transparent(bgcol) == false) ||
                ((clip_box->background != NULL) && content_get_opaque(clip_box->background)))
                continue;
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

        /* Check for and render gradient background */
        {
            lwc_string *url;
            uint8_t bg_type = css_computed_background_image(background->style, &url);
            if (bg_type == CSS_BACKGROUND_IMAGE_LINEAR_GRADIENT) {
                const css_linear_gradient *gradient = css_computed_background_gradient(background->style);
                if (gradient != NULL) {
                    if (!html_redraw_linear_gradient(gradient, &r, scale, ctx)) {
                        return false;
                    }
                }
            } else if (bg_type == CSS_BACKGROUND_IMAGE_RADIAL_GRADIENT) {
                const css_radial_gradient *radial = css_computed_background_radial_gradient(background->style);
                if (radial != NULL) {
                    if (!html_redraw_radial_gradient(radial, &r, scale, ctx)) {
                        return false;
                    }
                }
            }
        }

        /* and plot the image */
        if (plot_content) {
            width = content_get_width(background->background);
            height = content_get_height(background->background);

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
                content_redraw(background->background, &bg_data, &r, ctx);
            }
        }

        /* only <tr> rows being clipped to child boxes loop */
        if (!clip_to_children)
            return true;
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

static bool html_redraw_inline_background(int x, int y, struct box *box, float scale, const struct rect *clip,
    struct rect b, bool first, bool last, colour *background_colour, const css_unit_ctx *unit_len_ctx,
    const struct redraw_context *ctx)
{
    struct rect r = *clip;
    bool repeat_x = false;
    bool repeat_y = false;
    bool plot_colour = true;
    bool plot_content;
    css_fixed hpos = 0, vpos = 0;
    css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
    css_color bgcol;
    plot_style_t pstyle_fill_bg = {
        .fill_type = PLOT_OP_TYPE_SOLID,
        .fill_colour = *background_colour,
    };
    nserror res;

    plot_content = (box->background != NULL);

    if (html_redraw_printing && nsoption_bool(remove_backgrounds))
        return true;

    if (plot_content) {
        /* handle background-repeat */
        switch (css_computed_background_repeat(box->style)) {
        case CSS_BACKGROUND_REPEAT_REPEAT:
            repeat_x = repeat_y = true;
            /* optimisation: only plot the colour if
             * bitmap is not opaque
             */
            plot_colour = !content_get_opaque(box->background);
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
        css_computed_background_position(box->style, &hpos, &hunit, &vpos, &vunit);
        if (hunit == CSS_UNIT_PCT) {
            x += (b.x1 - b.x0 - content_get_width(box->background) * scale) * FIXTOFLT(hpos) / 100.;

            if (!repeat_x && ((hpos < 2 && !first) || (hpos > 98 && !last))) {
                plot_content = false;
            }
        } else {
            x += (int)(FIXTOFLT(css_unit_len2device_px(box->style, unit_len_ctx, hpos, hunit)) * scale);
        }

        if (vunit == CSS_UNIT_PCT) {
            y += (b.y1 - b.y0 - content_get_height(box->background) * scale) * FIXTOFLT(vpos) / 100.;
        } else {
            y += (int)(FIXTOFLT(css_unit_len2device_px(box->style, unit_len_ctx, vpos, vunit)) * scale);
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
    /* and plot the image */
    if (plot_content) {
        int width = content_get_width(box->background);
        int height = content_get_height(box->background);

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
            content_redraw(box->background, &bg_data, &r, ctx);
        }
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

    font_plot_style_from_css(&html->unit_len_ctx, box->style, &fstyle);
    fstyle.background = current_background_color;

    if (!text_redraw(box->text, box->length, box->byte_offset, box->space, &fstyle, x, y, clip, box->height, scale,
            excluded, (struct content *)html, html->sel, ctx))
        return false;

    return true;
}

bool html_redraw_box(const html_content *html, struct box *box, int x_parent, int y_parent, const struct rect *clip,
    float scale, colour current_background_color, const struct content_redraw_data *data,
    const struct redraw_context *ctx);

/**
 * Render children with negative z-index before parent's background.
 *
 * This function collects children with negative z-index and renders them
 * BEFORE the parent's background is drawn, per CSS stacking order spec.
 *
 * \param  html      html content
 * \param  box       box to render negative z-index children of
 * \param  x_parent  coordinate of parent box
 * \param  y_parent  coordinate of parent box
 * \param  clip      clip rectangle
 * \param  scale     scale for redraw
 * \param  current_background_color  background colour
 * \param  data      redraw data
 * \param  ctx       redraw context
 * \return true if successful, false otherwise
 */
static bool html_redraw_negative_zindex_children(const html_content *html, struct box *box, int x_parent, int y_parent,
    const struct rect *clip, float scale, colour current_background_color, const struct content_redraw_data *data,
    const struct redraw_context *ctx)
{
    struct box *c;
    int x_offset, y_offset;
    struct stacking_context negative_zindex;
    size_t i;

    /* Only process if this box doesn't create a stacking context itself */
    if (box_creates_stacking_context(box)) {
        return true; /* Stacking context isolates children */
    }

    x_offset = x_parent + box->x - scrollbar_get_offset(box->scroll_x);
    y_offset = y_parent + box->y - scrollbar_get_offset(box->scroll_y);

    stacking_context_init(&negative_zindex);

    /* Collect only negative z-index children */
    for (c = box->children; c; c = c->next) {
        if (c->type == BOX_FLOAT_LEFT || c->type == BOX_FLOAT_RIGHT) {
            continue;
        }
        int32_t z = box_get_z_index(c);
        if (z != Z_INDEX_AUTO && z < 0) {
            if (!stacking_context_add(&negative_zindex, c, z, x_offset, y_offset)) {
                stacking_context_fini(&negative_zindex);
                return true; /* Fallback: let normal path handle
                              */
            }
        }
    }

    /* Render negative z-index children sorted */
    if (negative_zindex.count > 0) {
        stacking_context_sort(&negative_zindex);
        for (i = 0; i < negative_zindex.count; i++) {
            if (!html_redraw_box(html, negative_zindex.entries[i].box, negative_zindex.entries[i].x_parent,
                    negative_zindex.entries[i].y_parent, clip, scale, current_background_color, data, ctx)) {
                stacking_context_fini(&negative_zindex);
                return false;
            }
        }
    }

    stacking_context_fini(&negative_zindex);
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
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_box_children(const html_content *html, struct box *box, int x_parent, int y_parent,
    const struct rect *clip, float scale, colour current_background_color, const struct content_redraw_data *data,
    const struct redraw_context *ctx)
{
    struct box *c;
    int x_offset, y_offset;
    struct stacking_context negative_zindex; /* z-index < 0 */
    struct stacking_context positive_zindex; /* z-index >= 0 */
    size_t i;
    bool render_ok = true;

    x_offset = x_parent + box->x - scrollbar_get_offset(box->scroll_x);
    y_offset = y_parent + box->y - scrollbar_get_offset(box->scroll_y);

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

    /* Pass 1: Collect positioned descendants, split by z-index sign */
    for (c = box->children; c; c = c->next) {
        if (c->type == BOX_FLOAT_LEFT || c->type == BOX_FLOAT_RIGHT) {
            continue;
        }
        int32_t z = box_get_z_index(c);
        if (z != Z_INDEX_AUTO) {
            /* Collect into appropriate list based on z-index sign
             */
            struct stacking_context *ctx_ptr = (z < 0) ? &negative_zindex : &positive_zindex;
            if (!stacking_context_add(ctx_ptr, c, z, x_offset, y_offset)) {
                goto fallback;
            }
        } else {
            /* Recurse into children to find positioned descendants
             */
            if (!stacking_context_collect_subtree(&negative_zindex, &positive_zindex, c, x_offset, y_offset)) {
                goto fallback;
            }
        }
    }
    for (c = box->float_children; c; c = c->next_float) {
        int32_t z = box_get_z_index(c);
        if (z != Z_INDEX_AUTO) {
            struct stacking_context *ctx_ptr = (z < 0) ? &negative_zindex : &positive_zindex;
            if (!stacking_context_add(ctx_ptr, c, z, x_offset, y_offset)) {
                goto fallback;
            }
        } else {
            if (!stacking_context_collect_subtree(&negative_zindex, &positive_zindex, c, x_offset, y_offset)) {
                goto fallback;
            }
        }
    }

    /* NOTE: Pass 2 (negative z-index) removed - negative z-index elements
     * are now rendered by html_redraw_negative_zindex_children() before
     * the parent's background is drawn, per CSS 2.1 stacking order. */

    /* Pass 2: Render in-flow children (z-index: auto) */
    for (c = box->children; c; c = c->next) {
        if (c->type == BOX_FLOAT_LEFT || c->type == BOX_FLOAT_RIGHT) {
            continue;
        }
        int32_t z = box_get_z_index(c);
        if (z != Z_INDEX_AUTO) {
            continue; /* Already in deferred list */
        }
        if (!html_redraw_box(html, c, x_offset, y_offset, clip, scale, current_background_color, data, ctx)) {
            render_ok = false;
            goto cleanup;
        }
    }

    /* Pass 3: Render floats */
    for (c = box->float_children; c; c = c->next_float) {
        int32_t z = box_get_z_index(c);
        if (z != Z_INDEX_AUTO) {
            continue; /* Already in deferred list */
        }
        if (!html_redraw_box(html, c, x_offset, y_offset, clip, scale, current_background_color, data, ctx)) {
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
                    positive_zindex.entries[i].y_parent, clip, scale, current_background_color, data, ctx)) {
                render_ok = false;
                goto cleanup;
            }
        }
    }
    goto cleanup;

fallback:
    /* Allocation failed - render in document order as fallback */
    stacking_context_fini(&negative_zindex);
    stacking_context_fini(&positive_zindex);
    for (c = box->children; c; c = c->next) {
        if (c->type != BOX_FLOAT_LEFT && c->type != BOX_FLOAT_RIGHT) {
            if (!html_redraw_box(html, c, x_offset, y_offset, clip, scale, current_background_color, data, ctx)) {
                return false;
            }
        }
    }
    for (c = box->float_children; c; c = c->next_float) {
        if (!html_redraw_box(html, c, x_offset, y_offset, clip, scale, current_background_color, data, ctx)) {
            return false;
        }
    }
    return true;

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
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 *
 * x, y, clip_[xy][01] are in target coordinates.
 */

bool html_redraw_box(const html_content *html, struct box *box, int x_parent, int y_parent, const struct rect *clip,
    const float scale, colour current_background_color, const struct content_redraw_data *data,
    const struct redraw_context *ctx)
{
    const struct plotter_table *plot = ctx->plot;
    int x, y;
    int width, height;
    int padding_left, padding_top, padding_width, padding_height;
    int border_left, border_top, border_right, border_bottom;
    struct rect r;
    struct rect rect;
    int x_scrolled, y_scrolled;
    struct box *bg_box = NULL;
    css_computed_clip_rect css_rect;
    enum css_overflow_e overflow_x = CSS_OVERFLOW_VISIBLE;
    enum css_overflow_e overflow_y = CSS_OVERFLOW_VISIBLE;
    dom_exception exc;
    dom_html_element_type tag_type;


    if (html_redraw_printing && (box->flags & PRINTED))
        return true;

    bool has_transform = false;
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
            float matrix[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
            int box_x = x_parent + box->x;
            int box_y = y_parent + box->y;
            float cx = box_x + box->width / 2.0f; /* transform-origin default: center */
            float cy = box_y + box->height / 2.0f;

            /* Build composed transform matrix M (starting with identity, no pre-translate) */
            for (uint32_t i = 0; i < transform_count; i++) {
                const css_transform_function *func = &transform_functions[i];
                float val1 = FIXTOFLT(func->value1);
                float val2 = FIXTOFLT(func->value2);
                if (func->unit1 == CSS_UNIT_PCT)
                    val1 = (val1 / 100.0f) * box->width;
                if (func->unit2 == CSS_UNIT_PCT)
                    val2 = (val2 / 100.0f) * box->height;

                switch (func->type) {
                case CSS_TRANSFORM_FUNC_TRANSLATE: {
                    /* M' = M * T(tx,ty) -- right multiply by translation */
                    matrix[4] += val1 * matrix[0] + val2 * matrix[2];
                    matrix[5] += val1 * matrix[1] + val2 * matrix[3];
                    break;
                }
                case CSS_TRANSFORM_FUNC_TRANSLATEX: {
                    matrix[4] += val1 * matrix[0];
                    matrix[5] += val1 * matrix[1];
                    break;
                }
                case CSS_TRANSFORM_FUNC_TRANSLATEY: {
                    matrix[4] += val1 * matrix[2];
                    matrix[5] += val1 * matrix[3];
                    break;
                }
                case CSS_TRANSFORM_FUNC_SCALE: {
                    /* M' = M * S(sx,sy) -- right multiply by scale */
                    float sx = val1;
                    float sy = (func->value2 == 0) ? sx : val2;
                    matrix[0] *= sx;
                    matrix[1] *= sx;
                    matrix[2] *= sy;
                    matrix[3] *= sy;
                    /* tx, ty unchanged when multiplying by pure scale from right */
                    break;
                }
                case CSS_TRANSFORM_FUNC_SCALEX:
                    matrix[0] *= val1;
                    matrix[1] *= val1;
                    break;
                case CSS_TRANSFORM_FUNC_SCALEY:
                    matrix[2] *= val1;
                    matrix[3] *= val1;
                    break;
                case CSS_TRANSFORM_FUNC_ROTATE: {
                    /* M' = M * R(θ) -- right multiply by rotation */
                    float rad = val1 * M_PI / 180.0f;
                    float cos_a = cosf(rad), sin_a = sinf(rad);
                    float a = matrix[0], b = matrix[1], c = matrix[2], d = matrix[3];
                    matrix[0] = a * cos_a + c * sin_a;
                    matrix[1] = b * cos_a + d * sin_a;
                    matrix[2] = c * cos_a - a * sin_a;
                    matrix[3] = d * cos_a - b * sin_a;
                    /* tx, ty unchanged when multiplying by pure rotation from right */
                    break;
                }
                default:
                    break;
                }
            }

            /* Apply transform-origin wrap: Final = T(cx,cy) * M * T(-cx,-cy)
             * This computes to: tx' = tx + cx*(1-a) - cy*c, ty' = ty + cy*(1-d) - cx*b */
            float a = matrix[0], b = matrix[1], c = matrix[2], d = matrix[3];
            matrix[4] += cx * (1.0f - a) - cy * c;
            matrix[5] += cy * (1.0f - d) - cx * b;

            /* Check if the matrix has any non-identity transform */
            bool is_identity = (matrix[0] == 1.0f && matrix[1] == 0.0f && matrix[2] == 0.0f && matrix[3] == 1.0f &&
                matrix[4] == 0.0f && matrix[5] == 0.0f);


            if (!is_identity) {
                if (ctx->plot->push_transform(ctx, matrix) == NSERROR_OK)
                    has_transform = true;
            }
        }
    }

    {
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
        if (class_attr != NULL)
            dom_string_unref(class_attr);
        if (name != NULL)
            dom_string_unref(name);
    }

    /* avoid trivial FP maths */
    if (scale == 1.0) {
        x = x_parent + box->x;
        y = y_parent + box->y;
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
        /* TRACE: log final visual position for flex items */
        if (box->parent && box->parent->style && css_computed_display(box->parent->style, false) == CSS_DISPLAY_FLEX) {
            // Log removed - was too verbose
        }
    } else {
        x = (x_parent + box->x) * scale;
        y = (y_parent + box->y) * scale;
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

    /* calculate rectangle covering this box and descendants */
    if (box->style && box->parent != NULL && css_computed_position(box->style) != CSS_POSITION_STATIC) {
        /* positioned boxes: clip to their own box size */
        r.x0 = x - border_left;
        r.x1 = x + padding_width + border_right;
    } else if (box->style && overflow_x != CSS_OVERFLOW_VISIBLE && box->parent != NULL) {
        /* non-visible overflow: clip to box size */
        r.x0 = x - border_left;
        r.x1 = x + padding_width + border_right;
    } else {
        /* visible overflow and not positioned: use descendant extents
         */
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
    if (box->style && box->parent != NULL && css_computed_position(box->style) != CSS_POSITION_STATIC) {
        /* positioned boxes: clip to their own box size */
        r.y0 = y - border_top;
        r.y1 = y + padding_height + border_bottom;
    } else if (box->style && overflow_y != CSS_OVERFLOW_VISIBLE && box->parent != NULL) {
        /* non-visible overflow: clip to box size */
        r.y0 = y - border_top;
        r.y1 = y + padding_height + border_bottom;
    } else {
        /* visible overflow and not positioned: use descendant extents
         */
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

    /* return if the rectangle is completely outside the clip rectangle */
    if (clip->y1 < r.y0 || r.y1 < clip->y0 || clip->x1 < r.x0 || r.x1 < clip->x0)
        return true;

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
                return true;
            }
        } else
            box->flags |= PRINTED; /*it won't be printed anymore*/
    }

    /* if visibility is hidden render children only */
    if (box->style && css_computed_visibility(box->style) == CSS_VISIBILITY_HIDDEN) {
        if ((ctx->plot->group_start) && (ctx->plot->group_start(ctx, "hidden box") != NSERROR_OK))
            return false;
        if (!html_redraw_box_children(html, box, x_parent, y_parent, &r, scale, current_background_color, data, ctx))
            return false;
        return ((!ctx->plot->group_end) || (ctx->plot->group_end(ctx) == NSERROR_OK));
    }

    if ((ctx->plot->group_start) && (ctx->plot->group_start(ctx, "vis box") != NSERROR_OK)) {
        return false;
    }

    if (box->style != NULL && css_computed_position(box->style) == CSS_POSITION_ABSOLUTE &&
        css_computed_clip(box->style, &css_rect) == CSS_CLIP_RECT) {
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
            return ((!ctx->plot->group_end) || (ctx->plot->group_end(ctx) == NSERROR_OK));
        /* clip to it */
        if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
            return false;

    } else if (box->type == BOX_BLOCK || box->type == BOX_INLINE_BLOCK || box->type == BOX_TABLE_CELL || box->object) {
        /* Use helper function for clip intersection */
        clip_result_t result = html_clip_intersect_box(&r, clip, has_transform);
        if (result == CLIP_RESULT_EMPTY) {
            /* no point trying to draw 0-width/height boxes */
            return ((!ctx->plot->group_end) || (ctx->plot->group_end(ctx) == NSERROR_OK));
        }
        /* clip to it */
        if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
            return false;
    } else {
        /* clip box is fine, clip to it */
        r = *clip;
        if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
            return false;
    }

    /* CSS 2.1: Render children with negative z-index BEFORE parent's
     * background. This only applies if this box doesn't create its own
     * stacking context. */
    if (!html_redraw_negative_zindex_children(
            html, box, x_parent, y_parent, &r, scale, current_background_color, data, ctx)) {
        return false;
    }

    /* background colour and image for block level content and replaced
     * inlines */

    bg_box = html_redraw_find_bg_box(box);

    /* bg_box == NULL implies that this box should not have
     * its background rendered. Otherwise filter out linebreaks,
     * optimize away non-differing inlines, only plot background
     * for BOX_TEXT it's in an inline */
    if (bg_box && bg_box->type != BOX_BR && bg_box->type != BOX_TEXT && bg_box->type != BOX_INLINE_END &&
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
        uint8_t pos_enum = (box->style != NULL) ? css_computed_position(box->style) : CSS_POSITION_STATIC;
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

            if (pos_enum == CSS_POSITION_FIXED || bg_attach == CSS_BACKGROUND_ATTACHMENT_FIXED) {
                /* For fixed position, only expand if element spans full viewport width */
                expand_viewport_bg = (left_match && right_match) || abs_full_width;
            } else {
                expand_viewport_bg = left_match && right_match;
                if (!expand_viewport_bg && abs_full_width) {
                    expand_viewport_bg = true;
                } else if (!expand_viewport_bg && right_match && abs_full_width) {
                    expand_viewport_bg = true;
                } else if (!expand_viewport_bg && left_match && abs_full_width) {
                    expand_viewport_bg = true;
                }
                if (log_target) {
                    NSLOG(layout, INFO,
                        "bg decision: tag %s class %s box %p pos %u bgatt %u b_left %d b_right %d t_left %d t_right %d bg_extent %d root_scaled %d tol %d vp_x %d root_w %d expand %d",
                        tag, cls, box, (unsigned)pos_enum, (unsigned)bg_attach, b_left, b_right, target_left,
                        target_right, bg_extent, root_w_scaled, tol, vp_x, root_w, (int)expand_viewport_bg);
                }
            }
        }
        if (expand_viewport_bg) {
            NSLOG(layout, INFO, "bg draw pre: tag %s class %s box %p pad_w %i pad_h %i", tag, cls, box, padding_width,
                padding_height);
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
            if (vp_x < 0)
                vp_x = 0;
            if (root_w < 0)
                root_w = 0;
            p.x0 = (int)(-vp_x * scale);
            p.x1 = (int)((-vp_x + root_w) * scale);
            if (ctx->plot->clip(ctx, &p) != NSERROR_OK)
                return false;
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
            /* plot background */
            if (!html_redraw_background(
                    x, y, box, scale, &p, &current_background_color, bg_box, &html->unit_len_ctx, ctx))
                return false;
            if (expand_viewport_bg) {
                NSLOG(layout, INFO, "bg draw post: tag %s class %s box %p rect x0 %i x1 %i", tag, cls, box, p.x0, p.x1);
            }
            if (class_attr != NULL)
                dom_string_unref(class_attr);
            if (name != NULL)
                dom_string_unref(name);
            /* restore previous graphics window */
            if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
                return false;
        }
    }

    /* borders for block level content and replaced inlines */
    if (box->style && box->type != BOX_TEXT && box->type != BOX_INLINE_END &&
        (box->type != BOX_INLINE || box->object || box->flags & IFRAME || box->flags & REPLACE_DIM ||
            (box->gadget != NULL &&
                (box->gadget->type == GADGET_TEXTAREA || box->gadget->type == GADGET_TEXTBOX ||
                    box->gadget->type == GADGET_PASSWORD))) &&
        (border_top || border_right || border_bottom || border_left)) {
        if (!html_redraw_borders(box, x_parent, y_parent, padding_width, padding_height, &r, scale, ctx))
            return false;
    }

    /* backgrounds and borders for non-replaced inlines */
    if (box->style && box->type == BOX_INLINE && box->inline_end &&
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
                        x, y, box, scale, &p, b, first, false, &current_background_color, &html->unit_len_ctx, ctx))
                    return false;
                /* restore previous graphics window */
                if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
                    return false;
                if (!html_redraw_inline_borders(box, b, &r, scale, first, false, ctx))
                    return false;
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

            if (ib == box->inline_end)
                /* reached end of BOX_INLINE span */
                break;
        }
        /* plot background and borders for last rectangle of
         * the inline */
        if (!html_redraw_inline_background(
                x, ib_y, box, scale, &p, b, first, true, &current_background_color, &html->unit_len_ctx, ctx))
            return false;
        /* restore previous graphics window */
        if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
            return false;
        if (!html_redraw_inline_borders(box, b, &r, scale, first, true, ctx))
            return false;
    }

    /* Debug outlines */
    if (html_redraw_debug) {
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
        if (ctx->plot->rectangle(ctx, plot_style_content_edge, &rect) != NSERROR_OK)
            return false;

        /* Padding edge -- red */
        rect.x0 = x;
        rect.y0 = y;
        rect.x1 = x + padding_width;
        rect.y1 = y + padding_height;
        if (ctx->plot->rectangle(ctx, plot_style_padding_edge, &rect) != NSERROR_OK)
            return false;

        /* Margin edge -- yellow */
        rect.x0 = x - border_left - margin_left;
        rect.y0 = y - border_top - margin_top;
        rect.x1 = x + padding_width + border_right + margin_right;
        rect.y1 = y + padding_height + border_bottom + margin_bottom;
        if (ctx->plot->rectangle(ctx, plot_style_margin_edge, &rect) != NSERROR_OK)
            return false;
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
            if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
                return false;
        }
    }

    /* text decoration */
    if ((box->type != BOX_TEXT) && box->style && css_computed_text_decoration(box->style) != CSS_TEXT_DECORATION_NONE) {
        if (!html_redraw_text_decoration(box, x_parent, y_parent, scale, current_background_color, ctx))
            return false;
    }

    if (box->node != NULL) {
        exc = dom_html_element_get_tag_type(box->node, &tag_type);
        if (exc != DOM_NO_ERR) {
            tag_type = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
        }
    } else {
        tag_type = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
    }


    if (box->object && width != 0 && height != 0) {
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
        calculate_object_fit_dimensions(box->style, object_fit, width, height, intrinsic_width, intrinsic_height,
            &render_width, &render_height, &offset_x, &offset_y);

        /* Position: base position + padding + centering offset (all scaled) */
        obj_data.x = x_scrolled + padding_left + offset_x * scale;
        obj_data.y = y_scrolled + padding_top + offset_y * scale;
        /* Dimensions: pass unscaled, content_redraw applies obj_data.scale */
        obj_data.width = render_width;
        obj_data.height = render_height;
        obj_data.background_colour = current_background_color;
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
            NSLOG(neosurf, ERROR,
                "Object redraw failed; red square shown for element tag=%s class=%s mime=%s url=%s size=%dx%d",
                tag ? tag : "", cls ? cls : "", mime_c ? mime_c : "", url ? url : "", width, height);
            if (class_attr != NULL)
                dom_string_unref(class_attr);
            if (name != NULL)
                dom_string_unref(name);
            /* Show image fail */
            /* Unicode (U+FFFC) 'OBJECT REPLACEMENT CHARACTER' */
            const char *obj = "\xef\xbf\xbc";
            int obj_width;
            int obj_x = x + padding_left;
            nserror res;

            rect.x0 = x + padding_left;
            rect.y0 = y + padding_top;
            rect.x1 = x + padding_left + width - 1;
            rect.y1 = y + padding_top + height - 1;
            res = ctx->plot->rectangle(ctx, plot_style_broken_object, &rect);
            if (res != NSERROR_OK) {
                return false;
            }

            res = guit->layout->width(plot_fstyle_broken_object, obj, sizeof(obj) - 1, &obj_width);
            if (res != NSERROR_OK) {
                obj_x += 1;
            } else {
                obj_x += width / 2 - obj_width / 2;
            }

            if (ctx->plot->text(ctx, plot_fstyle_broken_object, obj_x, y + padding_top + (int)(height * 0.75), obj,
                    sizeof(obj) - 1) != NSERROR_OK)
                return false;
        }
    } else if (box->svg_diagram != NULL && width != 0 && height != 0) {
        /* Inline SVG rendering */
        x_scrolled = x - scrollbar_get_offset(box->scroll_x) * scale;
        y_scrolled = y - scrollbar_get_offset(box->scroll_y) * scale;

        NSLOG(neosurf, DEBUG, "SVG: Rendering inline SVG at (%d,%d) size %dx%d", (int)(x_scrolled + padding_left),
            (int)(y_scrolled + padding_top), (int)width, (int)height);

        /* Get CSS 'color' property for currentColor support */
        css_color css_col;
        css_computed_color(box->style, &css_col);
        colour current_col = nscss_color_to_ns(css_col);

        if (!svg_redraw_diagram(box->svg_diagram, (int)(x_scrolled + padding_left), (int)(y_scrolled + padding_top),
                (int)width, (int)height, &r, ctx, current_background_color, current_col)) {
            NSLOG(neosurf, WARNING, "SVG: Inline SVG redraw failed");
        }
    } else if (box->svg_diagram != NULL && (width == 0 || height == 0)) {
        NSLOG(neosurf, DEBUG, "SVG: Skipping inline SVG render - zero size (w=%d h=%d)", (int)width, (int)height);
    } else if (tag_type == DOM_HTML_ELEMENT_TYPE_CANVAS && box->node != NULL && box->flags & REPLACE_DIM) {
        /* Canvas to draw */
        struct bitmap *bitmap = NULL;
        exc = dom_node_get_user_data(box->node, corestring_dom___ns_key_canvas_node_data, &bitmap);
        if (exc != DOM_NO_ERR) {
            bitmap = NULL;
        }
        if (bitmap != NULL &&
            ctx->plot->bitmap(ctx, bitmap, x + padding_left, y + padding_top, width, height, current_background_color,
                BITMAPF_NONE) != NSERROR_OK)
            return false;
    } else if (box->iframe) {
        /* Offset is passed to browser window redraw unscaled */
        browser_window_redraw(box->iframe, x + padding_left, y + padding_top, &r, ctx);

    } else if (box->gadget && box->gadget->type == GADGET_CHECKBOX) {
        if (!html_redraw_checkbox(x + padding_left, y + padding_top, width, height, box->gadget->selected, ctx))
            return false;

    } else if (box->gadget && box->gadget->type == GADGET_RADIO) {
        if (!html_redraw_radio(x + padding_left, y + padding_top, width, height, box->gadget->selected, ctx))
            return false;

    } else if (box->gadget && box->gadget->type == GADGET_FILE) {
        if (!html_redraw_file(x + padding_left, y + padding_top, width, height, box, scale, current_background_color,
                &html->unit_len_ctx, ctx))
            return false;

    } else if (box->gadget &&
        (box->gadget->type == GADGET_TEXTAREA || box->gadget->type == GADGET_PASSWORD ||
            box->gadget->type == GADGET_TEXTBOX)) {
        textarea_redraw(box->gadget->data.text.ta, x, y, current_background_color, scale, &r, ctx);

    } else if (box->text) {
        if (!html_redraw_text_box(html, box, x, y, &r, scale, current_background_color, ctx))
            return false;

    } else {
        const struct rect *child_clip_ptr = &r;
        if (overflow_x == CSS_OVERFLOW_VISIBLE && overflow_y == CSS_OVERFLOW_VISIBLE) {
            child_clip_ptr = clip;
        }
        {
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
            // Debug logging removed - was hotnews.ro specific
            if (class_attr != NULL)
                dom_string_unref(class_attr);
            if (name != NULL)
                dom_string_unref(name);
        }
        if (!html_redraw_box_children(
                html, box, x_parent, y_parent, child_clip_ptr, scale, current_background_color, data, ctx))
            return false;
    }

    if (box->type == BOX_BLOCK || box->type == BOX_INLINE_BLOCK || box->type == BOX_TABLE_CELL ||
        box->type == BOX_INLINE)
        if (ctx->plot->clip(ctx, clip) != NSERROR_OK)
            return false;

    /* list marker */
    if (box->list_marker) {
        if (!html_redraw_box(html, box->list_marker, x_parent + box->x - scrollbar_get_offset(box->scroll_x),
                y_parent + box->y - scrollbar_get_offset(box->scroll_y), clip, scale, current_background_color, data,
                ctx))
            return false;
    }

    /* scrollbars */
    if (((box->style && box->type != BOX_BR && box->type != BOX_TABLE && box->type != BOX_INLINE &&
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
            NSLOG(neosurf, INFO, "%s", messages_get_errorcode(res));
            return false;
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
        if (ctx->plot->clip(ctx, clip) != NSERROR_OK)
            return false;
    }

    /* Pop any transform that was pushed */
    if (has_transform && ctx->plot->pop_transform != NULL) {
        ctx->plot->pop_transform(ctx);
    }

    return ((!plot->group_end) || (ctx->plot->group_end(ctx) == NSERROR_OK));
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
    /* The layout can be NULL if we are in the process of rebuilding it */
    if (html->layout == NULL)
        return true;

    plot_style_t pstyle_fill_bg = {
        .fill_type = PLOT_OP_TYPE_SOLID,
        .fill_colour = data->background_colour,
    };

    NSLOG(neosurf, DEBUG, "PROFILER: START HTML redraw %p", c);

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
            html, box, data->x, data->y, clip, data->scale, pstyle_fill_bg.fill_colour, data, ctx);
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

    NSLOG(neosurf, DEBUG, "PROFILER: STOP HTML redraw %p", c);
    return result;
}
