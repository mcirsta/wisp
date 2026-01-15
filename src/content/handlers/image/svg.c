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

/**
 * \file
 * implementation of content for image/svg using libsvgtiny.
 */

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <svgtiny.h>

#include <neosurf/content.h>
#include <neosurf/content/content_protected.h>
#include <neosurf/desktop/gui_internal.h>
#include <neosurf/layout.h>
#include <neosurf/plotters.h>
#include <neosurf/utils/log.h>
#include <neosurf/utils/messages.h>
#include <neosurf/utils/nsurl.h>
#include <neosurf/utils/utils.h>
#include "content/content_factory.h"

#include "content/handlers/image/svg.h"

/**
 * Render a dashed line as a series of filled rectangles.
 *
 * This provides cross-platform dash rendering that works on ALL backends
 * (Qt, GTK, BeOS, framebuffer, Windows) without relying on platform-specific
 * dash pattern APIs.
 *
 * Only handles horizontal and vertical lines for now - diagonal lines
 * would require rotated rectangles which most plotters don't support.
 *
 * \param ctx          Redraw context
 * \param stroke_colour Stroke colour for the dashes
 * \param x1,y1        Start of line
 * \param x2,y2        End of line
 * \param stroke_width Width of the stroke
 * \param dasharray    Array of dash/gap lengths (scaled to display coords)
 * \param dasharray_count Number of elements in dasharray
 * \param dashoffset   Offset into the dash pattern
 * \param transform    Affine transform to apply
 * \return NSERROR_OK on success
 */
static nserror svg_plot_dashed_line_as_rects(const struct redraw_context *ctx, colour stroke_colour, float x1, float y1,
    float x2, float y2, float stroke_width, const float *dasharray, unsigned int dasharray_count, float dashoffset,
    const float transform[6])
{
    if (dasharray == NULL || dasharray_count == 0) {
        return NSERROR_INVALID;
    }

    /* Apply transform to line endpoints */
    float tx1 = x1 * transform[0] + y1 * transform[2] + transform[4];
    float ty1 = x1 * transform[1] + y1 * transform[3] + transform[5];
    float tx2 = x2 * transform[0] + y2 * transform[2] + transform[4];
    float ty2 = x2 * transform[1] + y2 * transform[3] + transform[5];

    /* Calculate line length and direction */
    float dx = tx2 - tx1;
    float dy = ty2 - ty1;
    float line_length = sqrtf(dx * dx + dy * dy);

    if (line_length < 1.0f) {
        return NSERROR_OK; /* Line too short */
    }

    /* Normalize direction */
    float ndx = dx / line_length;
    float ndy = dy / line_length;

    /* Perpendicular direction for stroke width */
    float px = -ndy * stroke_width / 2.0f;
    float py = ndx * stroke_width / 2.0f;

    /* Set up fill style for rectangles */
    plot_style_t fill_style = {
        .fill_type = PLOT_OP_TYPE_SOLID, .fill_colour = stroke_colour, .stroke_type = PLOT_OP_TYPE_NONE};

    /* Calculate total pattern length */
    float pattern_length = 0.0f;
    for (unsigned int i = 0; i < dasharray_count; i++) {
        pattern_length += dasharray[i];
    }
    if (pattern_length < 1.0f) {
        return NSERROR_OK; /* Pattern too short */
    }

    /* Start position, accounting for dash offset */
    float pos = -fmodf(dashoffset, pattern_length);
    if (pos > 0)
        pos -= pattern_length;

    unsigned int dash_idx = 0;
    bool draw_dash = true; /* Alternate between dash (draw) and gap (skip) */

    while (pos < line_length) {
        float dash_len = dasharray[dash_idx % dasharray_count];
        float dash_start = pos;
        float dash_end = pos + dash_len;

        /* Clamp to line bounds */
        if (dash_start < 0)
            dash_start = 0;
        if (dash_end > line_length)
            dash_end = line_length;

        /* Only draw if this is a dash (not a gap) and within bounds */
        if (draw_dash && dash_end > dash_start && dash_start < line_length) {
            /* Calculate rectangle corners */
            float sx = tx1 + ndx * dash_start;
            float sy = ty1 + ndy * dash_start;
            float ex = tx1 + ndx * dash_end;
            float ey = ty1 + ndy * dash_end;

            /* For horizontal lines (dy ≈ 0), create axis-aligned rect */
            if (fabsf(dy) < 0.01f) {
                struct rect r;
                r.x0 = (int)(fminf(sx, ex));
                r.y0 = (int)(sy - stroke_width / 2.0f);
                r.x1 = (int)(fmaxf(sx, ex));
                r.y1 = (int)(sy + stroke_width / 2.0f);
                ctx->plot->rectangle(ctx, &fill_style, &r);
            }
            /* For vertical lines (dx ≈ 0), create axis-aligned rect */
            else if (fabsf(dx) < 0.01f) {
                struct rect r;
                r.x0 = (int)(sx - stroke_width / 2.0f);
                r.y0 = (int)(fminf(sy, ey));
                r.x1 = (int)(sx + stroke_width / 2.0f);
                r.y1 = (int)(fmaxf(sy, ey));
                ctx->plot->rectangle(ctx, &fill_style, &r);
            }
            /* Diagonal lines - skip for now (would need polygon or rotated rect) */
        }

        pos += dash_len;
        dash_idx++;
        draw_dash = !draw_dash;
    }

    return NSERROR_OK;
}

/**
 * Render a gradient fill for an SVG shape using native gradient APIs.
 *
 * This is called for shapes that have fill_gradient_type != svgtiny_GRADIENT_NONE.
 * Uses native gradient plotting when available, otherwise skips (shape would have
 * been pre-rendered as triangles in older libsvgtiny versions).
 *
 * \param ctx          Redraw context
 * \param shape        SVG shape with gradient fill
 * \param path         Path data (float array with path commands)
 * \param path_len     Number of elements in path array
 * \param bbox         Bounding box in display coordinates (for clipping)
 * \param sx, sy       Scale factors (display/intrinsic)
 * \param transform    Affine transform to apply
 * \return NSERROR_OK on success, error code otherwise
 */
static nserror svg_plot_gradient_fill(const struct redraw_context *ctx, const struct svgtiny_shape *shape,
    const float *path, unsigned int path_len, const struct rect *bbox, float sx, float sy, const float transform[6])
{
#ifdef NEOSURF_USE_NATIVE_GRADIENTS
    if (shape->fill_gradient_type == svgtiny_GRADIENT_NONE) {
        return NSERROR_OK;
    }

    NSLOG(neosurf, DEEPDEBUG, "SVG gradient: Using NATIVE rendering path for %s gradient",
        shape->fill_gradient_type == svgtiny_GRADIENT_LINEAR ? "linear" : "radial");

    if (ctx->plot->linear_gradient == NULL && shape->fill_gradient_type == svgtiny_GRADIENT_LINEAR) {
        NSLOG(neosurf, WARNING, "SVG gradient: Native linear_gradient plotter is NULL!");
        return NSERROR_NOT_IMPLEMENTED;
    }

    if (ctx->plot->radial_gradient == NULL && shape->fill_gradient_type == svgtiny_GRADIENT_RADIAL) {
        NSLOG(neosurf, WARNING, "SVG gradient: Native radial_gradient plotter is NULL!");
        return NSERROR_NOT_IMPLEMENTED;
    }

    /* Convert gradient stops from SVG format to plotter format */
    struct gradient_stop *stops = alloca(shape->fill_grad_stop_count * sizeof(struct gradient_stop));
    for (unsigned int i = 0; i < shape->fill_grad_stop_count; i++) {
        /* Convert svgtiny RGB color to neosurf color format (BGR) */
        svgtiny_colour c = shape->fill_grad_stops[i].color;
        stops[i].color = (svgtiny_RED(c)) | (svgtiny_GREEN(c) << 8) | (svgtiny_BLUE(c) << 16);
        stops[i].offset = shape->fill_grad_stops[i].offset;
    }

#ifdef SVG_GRADIENT_BBOX_CLIP
    /* Optional: Set clip to the shape's bounding box.
     * This can cause slight edge cutting for bezier curves due to bbox
     * calculation using control points rather than actual curve bounds.
     * Disabled by default since fillPath() clips to path shape naturally. */
    ctx->plot->clip(ctx, bbox);
#else
    (void)bbox; /* Unused when not clipping to bbox */
#endif

    /* Scale gradient coordinates to match path space (scaled but not translated).
     * The transform is applied during rendering by the plotter. */
    float gx1 = shape->fill_grad_x1 * sx;
    float gy1 = shape->fill_grad_y1 * sy;
    float gx2 = shape->fill_grad_x2 * sx;
    float gy2 = shape->fill_grad_y2 * sy;

    nserror err;
    if (shape->fill_gradient_type == svgtiny_GRADIENT_LINEAR) {
        NSLOG(neosurf, DEEPDEBUG,
            "SVG gradient: Calling native linear plotter (%.1f,%.1f) to (%.1f,%.1f) with %u stops, path_len=%u", gx1,
            gy1, gx2, gy2, shape->fill_grad_stop_count, path_len);
        err = ctx->plot->linear_gradient(
            ctx, path, path_len, transform, gx1, gy1, gx2, gy2, stops, shape->fill_grad_stop_count);
    } else {
        /* Radial gradient: fill_grad_x1,y1 = center, fill_grad_x2,y2 = radii
         * Scale to match path space (scaled but not translated). */
        float cx = shape->fill_grad_x1 * sx;
        float cy = shape->fill_grad_y1 * sy;
        float rx = shape->fill_grad_x2 * sx;
        float ry = shape->fill_grad_y2 * sy;
        NSLOG(neosurf, DEEPDEBUG,
            "SVG gradient: Calling native radial plotter (%.1f,%.1f) rx=%.1f ry=%.1f with %u stops, path_len=%u", cx,
            cy, rx, ry, shape->fill_grad_stop_count, path_len);
        err = ctx->plot->radial_gradient(
            ctx, path, path_len, transform, cx, cy, rx, ry, stops, shape->fill_grad_stop_count);
    }

    if (err == NSERROR_OK) {
        NSLOG(neosurf, DEEPDEBUG, "SVG gradient: Native plotter succeeded");
    } else {
        NSLOG(neosurf, WARNING, "SVG gradient: Native plotter FAILED with error %d", err);
    }

    return err;
#else
    NSLOG(neosurf, DEEPDEBUG, "SVG gradient: Native gradients DISABLED at compile time, using fallback");
    /* Native gradients disabled - nothing to do (triangle fallback not implemented here) */
    (void)ctx;
    (void)shape;
    (void)path;
    (void)path_len;
    (void)bbox;
    (void)sx;
    (void)sy;
    (void)transform;
    return NSERROR_NOT_IMPLEMENTED;
#endif
}


/* Maximum number of floats sent per plotter path call to avoid oversized
 * buffers */
#define SVG_COMBO_FLUSH_LIMIT 960

/* Split a path buffer into safe chunks at MOVE boundaries and plot each chunk
 */
static nserror svg_plot_path_chunked(const struct redraw_context *ctx, const plot_style_t *style, const float *p,
    unsigned int n, const float transform[6])
{
    unsigned int pos = 0;
    unsigned int grp_start = 0;
    unsigned int grp_len = 0;
    unsigned int grp_moves = 0;
    float gb_minx = 0.0f, gb_miny = 0.0f, gb_maxx = 0.0f, gb_maxy = 0.0f;
    int gb_init = 0;
    nserror r = NSERROR_OK;

    while (pos < n) {
        while (pos < n && (int)p[pos] != PLOTTER_PATH_MOVE)
            pos++;
        if (pos >= n)
            break;

        unsigned int sp_start = pos;
        float sb_minx = 0.0f, sb_miny = 0.0f, sb_maxx = 0.0f, sb_maxy = 0.0f;
        int sb_init = 0;

        while (pos < n) {
            int cmd = (int)p[pos++];
            if (cmd == PLOTTER_PATH_MOVE || cmd == PLOTTER_PATH_LINE) {
                float xx = p[pos++];
                float yy = p[pos++];
                if (!sb_init) {
                    sb_minx = sb_maxx = xx;
                    sb_miny = sb_maxy = yy;
                    sb_init = 1;
                }
                if (xx < sb_minx)
                    sb_minx = xx;
                if (xx > sb_maxx)
                    sb_maxx = xx;
                if (yy < sb_miny)
                    sb_miny = yy;
                if (yy > sb_maxy)
                    sb_maxy = yy;
            } else if (cmd == PLOTTER_PATH_BEZIER) {
                float x1 = p[pos++];
                float y1 = p[pos++];
                float x2 = p[pos++];
                float y2 = p[pos++];
                float x3 = p[pos++];
                float y3 = p[pos++];
                if (!sb_init) {
                    sb_minx = sb_maxx = x1;
                    sb_miny = sb_maxy = y1;
                    sb_init = 1;
                }
                if (x1 < sb_minx)
                    sb_minx = x1;
                if (x1 > sb_maxx)
                    sb_maxx = x1;
                if (y1 < sb_miny)
                    sb_miny = y1;
                if (y1 > sb_maxy)
                    sb_maxy = y1;
                if (x2 < sb_minx)
                    sb_minx = x2;
                if (x2 > sb_maxx)
                    sb_maxx = x2;
                if (y2 < sb_miny)
                    sb_miny = y2;
                if (y2 > sb_maxy)
                    sb_maxy = y2;
                if (x3 < sb_minx)
                    sb_minx = x3;
                if (x3 > sb_maxx)
                    sb_maxx = x3;
                if (y3 < sb_miny)
                    sb_miny = y3;
                if (y3 > sb_maxy)
                    sb_maxy = y3;
            } else if (cmd == PLOTTER_PATH_CLOSE) {
                /* no coords */
            }
            if (pos < n && (int)p[pos] == PLOTTER_PATH_MOVE)
                break;
        }
        unsigned int sp_end = pos;
        unsigned int sp_len = sp_end - sp_start;
        NSLOG(neosurf, INFO, "SVG subpath parsed: sp_len=%u sbbox=%.2f,%.2f..%.2f,%.2f", sp_len, sb_minx, sb_miny,
            sb_maxx, sb_maxy);

        if (grp_len == 0) {
            grp_start = sp_start;
            grp_len = sp_len;
            grp_moves = 1;
            gb_minx = sb_minx;
            gb_miny = sb_miny;
            gb_maxx = sb_maxx;
            gb_maxy = sb_maxy;
            gb_init = sb_init;
            continue;
        }

        int overlap = (sb_maxx >= gb_minx && sb_minx <= gb_maxx && sb_maxy >= gb_miny && sb_miny <= gb_maxy);
        NSLOG(neosurf, INFO,
            "SVG group decision: grp_len=%u grp_moves=%u sb_len=%u gbbox=%.2f,%.2f..%.2f,%.2f sbbox=%.2f,%.2f..%.2f,%.2f overlap=%d next_total=%u limit=%u",
            grp_len, grp_moves, sp_len, gb_minx, gb_miny, gb_maxx, gb_maxy, sb_minx, sb_miny, sb_maxx, sb_maxy, overlap,
            grp_len + sp_len, SVG_COMBO_FLUSH_LIMIT);
        if (!overlap || grp_len + sp_len > SVG_COMBO_FLUSH_LIMIT) {
            NSLOG(neosurf, INFO, "SVG chunk flush: len=%u moves=%u reason=%s", grp_len, grp_moves,
                (!overlap ? "disjoint" : "limit"));
            nserror rr = ctx->plot->path(ctx, style, p + grp_start, grp_len, transform);
            if (rr != NSERROR_OK) {
                NSLOG(neosurf, ERROR, "SVG chunk flush failed: len=%u err=%d; splitting fallback", grp_len, rr);
                unsigned int pos2 = grp_start;
                while (pos2 < grp_start + grp_len) {
                    while (pos2 < grp_start + grp_len && (int)p[pos2] != PLOTTER_PATH_MOVE)
                        pos2++;
                    if (pos2 >= grp_start + grp_len)
                        break;
                    unsigned int sp = pos2;
                    unsigned int ep = sp + 1;
                    while (ep < grp_start + grp_len) {
                        int c = (int)p[ep++];
                        if (c == PLOTTER_PATH_MOVE || c == PLOTTER_PATH_LINE) {
                            ep += 2;
                        } else if (c == PLOTTER_PATH_BEZIER) {
                            ep += 6;
                        } else if (c == PLOTTER_PATH_CLOSE) {
                        }
                        if (ep < grp_start + grp_len && (int)p[ep] == PLOTTER_PATH_MOVE)
                            break;
                    }
                    unsigned int slen = ep - sp;
                    NSLOG(neosurf, INFO, "SVG chunk fallback split: subpath_len=%u", slen);
                    nserror rr2 = ctx->plot->path(ctx, style, p + sp, slen, transform);
                    if (rr2 != NSERROR_OK) {
                        NSLOG(neosurf, ERROR, "SVG fallback subpath failed: len=%u err=%d", slen, rr2);
                        r = rr2;
                    }
                    pos2 = ep;
                }
            }
            grp_start = sp_start;
            grp_len = sp_len;
            grp_moves = 1;
            gb_minx = sb_minx;
            gb_miny = sb_miny;
            gb_maxx = sb_maxx;
            gb_maxy = sb_maxy;
            gb_init = sb_init;
        } else {
            grp_len += sp_len;
            grp_moves += 1;
            if (!gb_init) {
                gb_minx = sb_minx;
                gb_miny = sb_miny;
                gb_maxx = sb_maxx;
                gb_maxy = sb_maxy;
                gb_init = sb_init;
            }
            if (sb_minx < gb_minx)
                gb_minx = sb_minx;
            if (sb_maxx > gb_maxx)
                gb_maxx = sb_maxx;
            if (sb_miny < gb_miny)
                gb_miny = sb_miny;
            if (sb_maxy > gb_maxy)
                gb_maxy = sb_maxy;
        }
    }

    if (grp_len > 0) {
        NSLOG(neosurf, INFO, "SVG chunk final flush: len=%u moves=%u", grp_len, grp_moves);
        nserror rr = ctx->plot->path(ctx, style, p + grp_start, grp_len, transform);
        if (rr != NSERROR_OK) {
            NSLOG(neosurf, ERROR, "SVG chunk final flush failed: len=%u err=%d; splitting fallback", grp_len, rr);
            unsigned int pos2 = grp_start;
            while (pos2 < grp_start + grp_len) {
                while (pos2 < grp_start + grp_len && (int)p[pos2] != PLOTTER_PATH_MOVE)
                    pos2++;
                if (pos2 >= grp_start + grp_len)
                    break;
                unsigned int sp = pos2;
                unsigned int ep = sp + 1;
                while (ep < grp_start + grp_len) {
                    int c = (int)p[ep++];
                    if (c == PLOTTER_PATH_MOVE || c == PLOTTER_PATH_LINE) {
                        ep += 2;
                    } else if (c == PLOTTER_PATH_BEZIER) {
                        ep += 6;
                    } else if (c == PLOTTER_PATH_CLOSE) {
                    }
                    if (ep < grp_start + grp_len && (int)p[ep] == PLOTTER_PATH_MOVE)
                        break;
                }
                unsigned int slen = ep - sp;
                NSLOG(neosurf, INFO, "SVG chunk fallback split: subpath_len=%u", slen);
                nserror rr2 = ctx->plot->path(ctx, style, p + sp, slen, transform);
                if (rr2 != NSERROR_OK) {
                    NSLOG(neosurf, ERROR, "SVG fallback subpath failed: len=%u err=%d", slen, rr2);
                    r = rr2;
                }
                pos2 = ep;
            }
        }
    }
    return r;
}

typedef struct svg_content {
    struct content base;

    struct svgtiny_diagram *diagram;

    bool parsed; /**< True if SVG has been parsed at least once */
} svg_content;


static nserror svg_create_svg_data(svg_content *c)
{
    c->diagram = svgtiny_create();
    if (c->diagram == NULL)
        goto no_memory;

    c->parsed = false;

    return NSERROR_OK;

no_memory:
    content_broadcast_error(&c->base, NSERROR_NOMEM, NULL);
    return NSERROR_NOMEM;
}


/**
 * Create a CONTENT_SVG.
 */

static nserror svg_create(const content_handler *handler, lwc_string *imime_type, const struct http_parameter *params,
    struct llcache_handle *llcache, const char *fallback_charset, bool quirks, struct content **c)
{
    svg_content *svg;
    nserror error;

    svg = calloc(1, sizeof(svg_content));
    if (svg == NULL)
        return NSERROR_NOMEM;

    error = content__init(&svg->base, handler, imime_type, params, llcache, fallback_charset, quirks);
    if (error != NSERROR_OK) {
        free(svg);
        return error;
    }

    error = svg_create_svg_data(svg);
    if (error != NSERROR_OK) {
        free(svg);
        return error;
    }

    *c = (struct content *)svg;

    return NSERROR_OK;
}


/**
 * Convert a CONTENT_SVG for display.
 */

static bool svg_convert(struct content *c)
{
    svg_content *svg = (svg_content *)c;
    const uint8_t *source_data;
    size_t source_size;
    int intrinsic_width = 0, intrinsic_height = 0;

    assert(svg->diagram);

    /* Extract intrinsic dimensions from SVG width/height/viewBox attributes.
     * This lightweight parse only reads the header, not shapes.
     * This is needed so that layout can calculate aspect ratio for images
     * with width=100% height=auto before reformat is called. */
    source_data = content__get_source_data(c, &source_size);
    if (source_data != NULL && source_size > 0) {
        svgtiny_parse_dimensions((const char *)source_data, source_size, &intrinsic_width, &intrinsic_height);

        /* Set content dimensions from the extracted intrinsic size */
        c->width = intrinsic_width;
        c->height = intrinsic_height;
    }

    content_set_ready(c);
    content_set_done(c);
    /* Done: update status bar */
    content_set_status(c, "");

    return true;
}

/**
 * Reformat a CONTENT_SVG.
 */

static void svg_reformat(struct content *c, int width, int height)
{
    svg_content *svg = (svg_content *)c;
    const uint8_t *source_data;
    size_t source_size;

    assert(svg->diagram);

    /* Skip reformat if dimensions are unknown (0x0).
     * We can't do a meaningful parse without knowing the target viewport.
     * Intrinsic dimensions are already available from svg_convert via
     * svgtiny_parse_dimensions(), so we just wait for a call with real dimensions. */
    if (width <= 0 || height <= 0) {
        NSLOG(neosurf, DEBUG, "SVG reformat skipped: dimensions unknown (0x0)");
        return;
    }

    /* Parse the SVG at the viewport dimensions.
     * libsvgtiny bakes the CTM into shape coordinates and stroke widths,
     * so we need to parse at the actual display size for correct scaling. */
    NSLOG(neosurf, DEBUG, "SVG parsing with viewport: %dx%d", width, height);
    source_data = content__get_source_data(c, &source_size);

    svgtiny_parse(
        svg->diagram, (const char *)source_data, source_size, nsurl_access(content_get_url(c)), width, height);

    c->width = svg->diagram->width;
    c->height = svg->diagram->height;
}


/**
 * Redraw a CONTENT_SVG.
 */

static bool svg_redraw_internal(svg_content *svg, int x, int y, int width, int height, const struct rect *clip,
    const struct redraw_context *ctx, colour background_colour, colour current_color)
{
    float transform[6];
    struct svgtiny_diagram *diagram = svg->diagram;
    int px, py;
    unsigned int i;
    plot_font_style_t fstyle = *plot_style_font;
    plot_style_t pstyle;
    nserror res;
    bool ok = true;
    /* For inline SVGs, content_get_url may return NULL - handle gracefully */
    nsurl *content_url = content_get_url(&svg->base);
    const char *url_str = content_url ? nsurl_access(content_url) : "(inline)";

    assert(diagram);

    float sx = (float)width / (float)svg->base.width;
    float sy = (float)height / (float)svg->base.height;
    NSLOG(neosurf, WARNING, "SVG redraw: display=%dx%d intrinsic=%dx%d sx=%.3f sy=%.3f", width, height, svg->base.width,
        svg->base.height, sx, sy);
    transform[0] = 1.0f;
    transform[1] = 0.0f;
    transform[2] = 0.0f;
    transform[3] = 1.0f;
    transform[4] = x;
    transform[5] = y;

    NSLOG(neosurf, DEBUG, "PROFILER: START SVG rendering %p", svg);
    NSLOG(neosurf, INFO, "SVG redraw start: url=%s clip=%d,%d..%d,%d limit=%u", url_str, clip->x0, clip->y0, clip->x1,
        clip->y1, SVG_COMBO_FLUSH_LIMIT);

#define BGR(c) (((svgtiny_RED((c))) | (svgtiny_GREEN((c)) << 8) | (svgtiny_BLUE((c)) << 16)))

    unsigned int max_path_len = 0;
    for (i = 0; i != diagram->shape_count; i++) {
        if (diagram->shape[i].path && diagram->shape[i].path_length > max_path_len) {
            max_path_len = diagram->shape[i].path_length;
        }
    }
    float *scaled = NULL;
    if (max_path_len > 0) {
        scaled = malloc(sizeof(float) * max_path_len);
        if (scaled == NULL) {
            return false;
        }
    }
    float *combo = NULL;
    unsigned int combo_len = 0;
    unsigned int combo_cap = 0;
    plot_style_t combo_style;
    int combo_active = 0;

    for (i = 0; i != diagram->shape_count; i++) {
        if (diagram->shape[i].path) {
            NSLOG(neosurf, WARNING, "SVG shape[%u/%u]: fill=0x%x stroke=0x%x stroke_width=%d dasharray=%s", i,
                diagram->shape_count, (unsigned)diagram->shape[i].fill, (unsigned)diagram->shape[i].stroke,
                diagram->shape[i].stroke_width, diagram->shape[i].stroke_dasharray_set ? "yes" : "no");
            /* stroke style */
            svgtiny_colour stroke_c = diagram->shape[i].stroke;

            if (stroke_c == svgtiny_CURRENT_COLOR) {
                /* currentColor from CSS is already in neosurf format */
                pstyle.stroke_type = PLOT_OP_TYPE_SOLID;
                pstyle.stroke_colour = current_color;
            } else if (stroke_c == svgtiny_TRANSPARENT) {
                pstyle.stroke_type = PLOT_OP_TYPE_NONE;
                pstyle.stroke_colour = NS_TRANSPARENT;
            } else {
                pstyle.stroke_type = PLOT_OP_TYPE_SOLID;
                pstyle.stroke_colour = BGR(stroke_c);
            }
            /* Scale stroke_width by display/intrinsic ratio, just like path coordinates.
             * Use average of sx and sy for uniform stroke appearance. */
            float stroke_scale = (sx + sy) / 2.0f;
            int scaled_stroke_width = (int)(diagram->shape[i].stroke_width * stroke_scale + 0.5f);
            if (diagram->shape[i].stroke_width > 0 && scaled_stroke_width == 0)
                scaled_stroke_width = 1; /* Ensure visible strokes don't disappear */
            pstyle.stroke_width = plot_style_int_to_fixed(scaled_stroke_width);

            /* Pass dasharray to plotter for custom dash patterns */
            float scaled_dasharray[16]; /* Stack-allocated for common dash patterns */
            if (diagram->shape[i].stroke_dasharray_set && diagram->shape[i].stroke_dasharray_count > 0 &&
                diagram->shape[i].stroke_dasharray_count <= 16) {
                /* Scale dasharray values by same factor as stroke_width */
                for (unsigned int d = 0; d < diagram->shape[i].stroke_dasharray_count; d++) {
                    scaled_dasharray[d] = diagram->shape[i].stroke_dasharray[d] * stroke_scale;
                }
                NSLOG(neosurf, WARNING, "svg.c dasharray: raw=[%.1f,%.1f] stroke_scale=%.3f scaled=[%.1f,%.1f]",
                    diagram->shape[i].stroke_dasharray[0],
                    diagram->shape[i].stroke_dasharray_count > 1 ? diagram->shape[i].stroke_dasharray[1] : 0,
                    stroke_scale, scaled_dasharray[0],
                    diagram->shape[i].stroke_dasharray_count > 1 ? scaled_dasharray[1] : 0);
                pstyle.stroke_dasharray = scaled_dasharray;
                pstyle.stroke_dasharray_count = diagram->shape[i].stroke_dasharray_count;
                /* Scale dashoffset by the same factor as stroke_width */
                pstyle.stroke_dashoffset = diagram->shape[i].stroke_dashoffset * stroke_scale;
            } else {
                pstyle.stroke_dasharray = NULL;
                pstyle.stroke_dasharray_count = 0;
                pstyle.stroke_dashoffset = 0;
            }

            /* fill style */
            svgtiny_colour fill_c = diagram->shape[i].fill;

            if (fill_c == svgtiny_CURRENT_COLOR) {
                /* currentColor from CSS is already in neosurf format */
                pstyle.fill_type = PLOT_OP_TYPE_SOLID;
                pstyle.fill_colour = current_color;
            } else if (fill_c == svgtiny_TRANSPARENT) {
                pstyle.fill_type = PLOT_OP_TYPE_NONE;
                pstyle.fill_colour = NS_TRANSPARENT;
            } else {
                pstyle.fill_type = PLOT_OP_TYPE_SOLID;
                pstyle.fill_colour = BGR(fill_c);
            }
            if (scaled != NULL) {
                unsigned int j = 0;
                unsigned int k = 0;
                float minx = 0.0f, miny = 0.0f, maxx = 0.0f, maxy = 0.0f;
                int initbb = 0;
                while (j < diagram->shape[i].path_length) {
                    int cmd = (int)diagram->shape[i].path[j++];
                    scaled[k++] = (float)cmd;
                    switch (cmd) {
                    case PLOTTER_PATH_MOVE:
                    case PLOTTER_PATH_LINE: {
                        float xx = diagram->shape[i].path[j++] * sx;
                        float yy = diagram->shape[i].path[j++] * sy;
                        scaled[k++] = xx;
                        scaled[k++] = yy;
                        if (!initbb) {
                            minx = maxx = xx;
                            miny = maxy = yy;
                            initbb = 1;
                        }
                        if (xx < minx)
                            minx = xx;
                        if (xx > maxx)
                            maxx = xx;
                        if (yy < miny)
                            miny = yy;
                        if (yy > maxy)
                            maxy = yy;
                        break;
                    }
                    case PLOTTER_PATH_BEZIER: {
                        float x1 = diagram->shape[i].path[j++] * sx;
                        float y1 = diagram->shape[i].path[j++] * sy;
                        float x2 = diagram->shape[i].path[j++] * sx;
                        float y2 = diagram->shape[i].path[j++] * sy;
                        float x3 = diagram->shape[i].path[j++] * sx;
                        float y3 = diagram->shape[i].path[j++] * sy;
                        scaled[k++] = x1;
                        scaled[k++] = y1;
                        scaled[k++] = x2;
                        scaled[k++] = y2;
                        scaled[k++] = x3;
                        scaled[k++] = y3;
                        if (!initbb) {
                            minx = maxx = x1;
                            miny = maxy = y1;
                            initbb = 1;
                        }
                        if (x1 < minx)
                            minx = x1;
                        if (x1 > maxx)
                            maxx = x1;
                        if (y1 < miny)
                            miny = y1;
                        if (y1 > maxy)
                            maxy = y1;
                        if (x2 < minx)
                            minx = x2;
                        if (x2 > maxx)
                            maxx = x2;
                        if (y2 < miny)
                            miny = y2;
                        if (y2 > maxy)
                            maxy = y2;
                        if (x3 < minx)
                            minx = x3;
                        if (x3 > maxx)
                            maxx = x3;
                        if (y3 < miny)
                            miny = y3;
                        if (y3 > maxy)
                            maxy = y3;
                        break;
                    }
                    case PLOTTER_PATH_CLOSE:
                    default:
                        break;
                    }
                }
                int lx = (int)floorf(minx) + x;
                int rx = (int)ceilf(maxx) + x;
                int ty = (int)floorf(miny) + y;
                int by = (int)ceilf(maxy) + y;
                if (!(rx < clip->x0 || lx >= clip->x1 || by < clip->y0 || ty >= clip->y1)) {
                    NSLOG(neosurf, INFO, "SVG path begin: url=%s index=%u orig_len=%u scaled_len=%u bbox=%d,%d..%d,%d",
                        url_str, i, diagram->shape[i].path_length, k, lx, ty, rx, by);
                    NSLOG(neosurf, DEBUG, "  SVG bbox raw: minx=%.2f miny=%.2f maxx=%.2f maxy=%.2f", minx, miny, maxx,
                        maxy);
                    NSLOG(neosurf, DEBUG, "  SVG bbox floored: lx=%d ty=%d rx=%d by=%d (x=%d y=%d)", lx, ty, rx, by, x,
                        y);
                    NSLOG(neosurf, DEBUG, "  SVG transform: [%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]", transform[0],
                        transform[1], transform[2], transform[3], transform[4], transform[5]);

                    /* Check for gradient fill and render it */
                    if (diagram->shape[i].fill_gradient_type != svgtiny_GRADIENT_NONE) {
                        struct rect grad_clip = {.x0 = lx, .y0 = ty, .x1 = rx, .y1 = by};
                        nserror grad_err = svg_plot_gradient_fill(
                            ctx, &diagram->shape[i], scaled, k, &grad_clip, sx, sy, transform);
                        if (grad_err == NSERROR_OK) {
                            NSLOG(neosurf, DEBUG, "SVG gradient fill rendered successfully for shape %u", i);
                            /* Continue to render stroke if present */
                            if (pstyle.stroke_type != PLOT_OP_TYPE_NONE) {
                                plot_style_t stroke_only = pstyle;
                                stroke_only.fill_type = PLOT_OP_TYPE_NONE;
                                res = ctx->plot->path(ctx, &stroke_only, scaled, k, transform);
                            }
                            continue; /* Skip normal path rendering since gradient is done */
                        }
                        /* If gradient rendering failed, fall through to normal rendering */
                        NSLOG(neosurf, WARNING, "SVG gradient fill failed for shape %u, falling back to solid", i);
                    }

#ifdef NEOSURF_SVG_COMBO_DISABLE
                    res = ctx->plot->path(ctx, &pstyle, scaled, k, transform);
                    if (res != NSERROR_OK) {
                        ok = false;
                        int stroke_rgb = (svgtiny_RED(diagram->shape[i].stroke) << 16) |
                            (svgtiny_GREEN(diagram->shape[i].stroke) << 8) | (svgtiny_BLUE(diagram->shape[i].stroke));
                        int fill_rgb = (svgtiny_RED(diagram->shape[i].fill) << 16) |
                            (svgtiny_GREEN(diagram->shape[i].fill) << 8) | (svgtiny_BLUE(diagram->shape[i].fill));
                        NSLOG(neosurf, ERROR,
                            "SVG render failed: url=%s element=path index=%u path_len=%u err=%d stroke=#%06x fill=#%06x stroke_w=%d",
                            url_str, i, diagram->shape[i].path_length, res, stroke_rgb, fill_rgb,
                            diagram->shape[i].stroke_width);
                    }
                    continue;
#endif
                    /* For shapes with dasharray, use cross-platform rectangle rendering for
                     * simple lines (MOVE+LINE), or plot the path directly for complex shapes.
                     * This is also plotted immediately (not batched) because pstyle.stroke_dasharray
                     * points to a stack-allocated array. */
                    if (pstyle.stroke_dasharray != NULL) {
                        /* Flush any pending combo first */
                        if (combo_active && combo_len > 0) {
                            res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
                            combo_len = 0;
                        }

                        /* Check if this is a simple line:
                         * - 6 elements: MOVE,x,y,LINE,x,y (raw line)
                         * - 7 elements: MOVE,x,y,LINE,x,y,CLOSE (<line> element from svgtiny) */
                        bool is_simple_line = (((k == 6 || k == 7) && (int)scaled[0] == PLOTTER_PATH_MOVE &&
                            (int)scaled[3] == PLOTTER_PATH_LINE));

                        /* Use cross-platform rectangle-based rendering for simple dashed lines */
                        if (is_simple_line) {
                            float x1 = scaled[1];
                            float y1 = scaled[2];
                            float x2 = scaled[4];
                            float y2 = scaled[5];
                            colour stroke_colour = pstyle.stroke_colour;

                            res = svg_plot_dashed_line_as_rects(ctx, stroke_colour, x1, y1, x2, y2,
                                (float)scaled_stroke_width, pstyle.stroke_dasharray, pstyle.stroke_dasharray_count,
                                (float)diagram->shape[i].stroke_dashoffset, transform);

                            NSLOG(neosurf, INFO, "Dashed line->rects: stroke_width=%d dasharray=[%.1f,%.1f]",
                                scaled_stroke_width, pstyle.stroke_dasharray[0],
                                pstyle.stroke_dasharray_count > 1 ? pstyle.stroke_dasharray[1] : 0.0f);
                        } else {
                            /* Fall back to standard path rendering for complex shapes */
                            res = ctx->plot->path(ctx, &pstyle, scaled, k, transform);
                        }

                        if (res != NSERROR_OK) {
                            ok = false;
                        }
                        continue;
                    }
                    int same = combo_active && combo_style.stroke_type == pstyle.stroke_type &&
                        combo_style.fill_type == pstyle.fill_type &&
                        combo_style.stroke_colour == pstyle.stroke_colour &&
                        combo_style.fill_colour == pstyle.fill_colour &&
                        combo_style.stroke_width == pstyle.stroke_width;
                    if (!same) {
                        /* Flush previous combo group in
                         * chunks when style changes */
                        if (combo_active && combo_len > 0) {
                            NSLOG(neosurf, INFO, "SVG combo style change flush: len=%u", combo_len);
                            res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
                            if (res != NSERROR_OK) {
                                ok = false;
                                NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=path combo_flush len=%u",
                                    url_str, combo_len);
                            }
                            combo_len = 0;
                        }
                        combo_style = pstyle;
                        combo_active = 1;
                    }
                    /* Flush combo if adding current path
                     * would exceed chunk limit */
                    if (combo_active && combo_len > 0 && combo_len + k > SVG_COMBO_FLUSH_LIMIT) {
                        NSLOG(neosurf, INFO, "SVG combo limit flush: combo_len=%u next_len=%u", combo_len, k);
                        res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
                        if (res != NSERROR_OK) {
                            ok = false;
                            NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=path combo_flush len=%u", url_str,
                                combo_len);
                        }
                        combo_len = 0;
                    }
                    if (k > SVG_COMBO_FLUSH_LIMIT) {
                        NSLOG(neosurf, INFO, "SVG direct chunk plot: scaled_len=%u limit=%u", k, SVG_COMBO_FLUSH_LIMIT);
                        res = svg_plot_path_chunked(ctx, &pstyle, scaled, k, transform);
                        if (res != NSERROR_OK) {
                            ok = false;
                            int stroke_rgb = (svgtiny_RED(diagram->shape[i].stroke) << 16) |
                                (svgtiny_GREEN(diagram->shape[i].stroke) << 8) |
                                (svgtiny_BLUE(diagram->shape[i].stroke));
                            int fill_rgb = (svgtiny_RED(diagram->shape[i].fill) << 16) |
                                (svgtiny_GREEN(diagram->shape[i].fill) << 8) | (svgtiny_BLUE(diagram->shape[i].fill));
                            NSLOG(neosurf, ERROR,
                                "SVG render failed: url=%s element=path index=%u path_len=%u scaled_len=%u err=%d stroke=#%06x fill=#%06x stroke_w=%d",
                                url_str, i, diagram->shape[i].path_length, k, res, stroke_rgb, fill_rgb,
                                diagram->shape[i].stroke_width);
                        }
                        continue;
                    }
                    if (combo_len + k > combo_cap) {
                        unsigned int ncap = combo_cap ? combo_cap * 2 : k;
                        while (ncap < combo_len + k)
                            ncap *= 2;
                        float *nbuf = realloc(combo, sizeof(float) * ncap);
                        if (nbuf == NULL) {
                            if (scaled)
                                free(scaled);
                            if (combo)
                                free(combo);
                            return false;
                        }
                        combo = nbuf;
                        combo_cap = ncap;
                    }
                    memcpy(combo + combo_len, scaled, sizeof(float) * k);
                    combo_len += k;
                    /* Periodic chunked flush to keep combo
                     * buffer bounded */
                    if (combo_len >= SVG_COMBO_FLUSH_LIMIT) {
                        NSLOG(neosurf, INFO, "SVG periodic combo flush: len=%u", combo_len);
                        res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
                        if (res != NSERROR_OK) {
                            ok = false;
                            NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=path combo_flush len=%u", url_str,
                                combo_len);
                        }
                        combo_len = 0;
                    }
                }
            }

        } else if (diagram->shape[i].text) {
            /* Ensure combo is flushed safely before plotting text
             */
            if (combo_active && combo_len > 0) {
                res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
                if (res != NSERROR_OK) {
                    ok = false;
                    NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=text combo_flush", url_str);
                }
                combo_len = 0;
                combo_active = 0;
            }
            px = (int)(diagram->shape[i].text_x * sx) + transform[4];
            py = (int)(diagram->shape[i].text_y * sy) + transform[5];

            fstyle.background = 0xffffff;
            /* Use SVG fill color for text, default to black if
             * transparent */
            if (diagram->shape[i].fill == svgtiny_TRANSPARENT) {
                fstyle.foreground = 0x000000;
            } else {
                fstyle.foreground = BGR(diagram->shape[i].fill);
            }
            /* Use SVG font-size, fallback to 12.
             * SVG font-size is in USER UNITS (pixels). Scale with viewport
             * so fonts grow/shrink proportionally with the SVG.
             * Use sx (display_width/intrinsic_width) as the scale factor.
             * Use FONTF_SIZE_PIXELS flag to tell the frontend to use the
             * size directly as pixels without point-to-pixel conversion.
             */
            float fsize = diagram->shape[i].font_size;
            if (fsize <= 0.0f) {
                fsize = 12.0f;
            }
            /* Apply viewport scale (sx) so fonts scale with SVG display size */
            float scaled_fsize = fsize * sx;
            fstyle.size = (int)(scaled_fsize * PLOT_STYLE_SCALE);
            fstyle.flags |= FONTF_SIZE_PIXELS;


            /* Apply font-weight bold if specified */
            if (diagram->shape[i].font_weight_bold) {
                fstyle.weight = 700;
            }

            /* Adjust position for text-anchor */
            if (diagram->shape[i].text_anchor != svgtiny_TEXT_ANCHOR_START) {
                int text_width = 0;
                size_t text_len = strlen(diagram->shape[i].text);
                /* Use layout API if available, otherwise
                 * approximate */
                if (guit != NULL && guit->layout != NULL && guit->layout->width != NULL) {
                    guit->layout->width(&fstyle, diagram->shape[i].text, text_len, &text_width);
                } else {
                    /* Approximate: 0.6 * font_height per
                     * char */
                    int cw = (fstyle.size / PLOT_STYLE_SCALE) * 6 / 10;
                    text_width = (int)text_len * cw;
                }
                if (diagram->shape[i].text_anchor == svgtiny_TEXT_ANCHOR_MIDDLE) {
                    px -= text_width / 2;
                } else if (diagram->shape[i].text_anchor == svgtiny_TEXT_ANCHOR_END) {
                    px -= text_width;
                }
            }

            res = ctx->plot->text(ctx, &fstyle, px, py, diagram->shape[i].text, strlen(diagram->shape[i].text));
            if (res != NSERROR_OK) {
                ok = false;
                NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=text index=%u pos=%d,%d text='%s'", url_str, i,
                    px, py, diagram->shape[i].text);
            }
        }
    }

#undef BGR
    /* Final chunked flush of any remaining combined paths */
    if (combo_active && combo_len > 0) {
        NSLOG(neosurf, INFO, "SVG final combo flush: len=%u", combo_len);
        res = svg_plot_path_chunked(ctx, &combo_style, combo, combo_len, transform);
        if (res != NSERROR_OK) {
            ok = false;
            NSLOG(neosurf, ERROR, "SVG render failed: url=%s element=path final_flush len=%u", url_str, combo_len);
        }
    }
    if (scaled)
        free(scaled);
    if (combo)
        free(combo);
    NSLOG(neosurf, DEBUG, "PROFILER: STOP SVG rendering %p", svg);
    return ok;
}


bool svg_redraw_diagram(struct svgtiny_diagram *diagram, int x, int y, int width, int height, const struct rect *clip,
    const struct redraw_context *ctx, colour background_colour, colour current_color)
{
    svg_content tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.diagram = diagram;
    tmp.base.width = width;
    tmp.base.height = height;
    return svg_redraw_internal(&tmp, x, y, width, height, clip, ctx, background_colour, current_color);
}


static bool svg_redraw_tiled_internal(
    svg_content *svg, struct content_redraw_data *data, const struct rect *clip, const struct redraw_context *ctx)
{
    /* Tiled redraw required.  SVG repeats to extents of clip
     * rectangle, in x, y or both directions */
    int x, y, x0, y0, x1, y1;

    x = x0 = data->x;
    y = y0 = data->y;

    /* Find the redraw boundaries to loop within */
    if (data->repeat_x) {
        for (; x0 > clip->x0; x0 -= data->width)
            ;
        x1 = clip->x1;
    } else {
        x1 = x + 1;
    }
    if (data->repeat_y) {
        for (; y0 > clip->y0; y0 -= data->height)
            ;
        y1 = clip->y1;
    } else {
        y1 = y + 1;
    }

    /* Repeatedly plot the SVG across the area */
    for (y = y0; y < y1; y += data->height) {
        for (x = x0; x < x1; x += data->width) {
            if (!svg_redraw_internal(svg, x, y, data->width, data->height, clip, ctx, data->background_colour, 0)) {
                return false;
            }
        }
    }

    return true;
}


/**
 * Redraw a CONTENT_SVG.
 */
static bool svg_redraw(
    struct content *c, struct content_redraw_data *data, const struct rect *clip, const struct redraw_context *ctx)
{
    svg_content *svg = (svg_content *)c;

    if ((data->width <= 0) && (data->height <= 0)) {
        /* No point trying to plot SVG if it does not occupy a
         * valid area */
        return true;
    }

    if ((data->repeat_x == false) && (data->repeat_y == false)) {
        return svg_redraw_internal(
            svg, data->x, data->y, data->width, data->height, clip, ctx, data->background_colour, 0);
    }

    return svg_redraw_tiled_internal(svg, data, clip, ctx);
}


/**
 * Destroy a CONTENT_SVG and free all resources it owns.
 */

static void svg_destroy(struct content *c)
{
    svg_content *svg = (svg_content *)c;

    if (svg->diagram != NULL)
        svgtiny_free(svg->diagram);
}


static nserror svg_clone(const struct content *old, struct content **newc)
{
    svg_content *svg;
    nserror error;

    svg = calloc(1, sizeof(svg_content));
    if (svg == NULL)
        return NSERROR_NOMEM;

    error = content__clone(old, &svg->base);
    if (error != NSERROR_OK) {
        content_destroy(&svg->base);
        return error;
    }

    /* Simply replay create/convert */
    error = svg_create_svg_data(svg);
    if (error != NSERROR_OK) {
        content_destroy(&svg->base);
        return error;
    }

    if (old->status == CONTENT_STATUS_READY || old->status == CONTENT_STATUS_DONE) {
        if (svg_convert(&svg->base) == false) {
            content_destroy(&svg->base);
            return NSERROR_CLONE_FAILED;
        }
    }

    *newc = (struct content *)svg;

    return NSERROR_OK;
}

static content_type svg_content_type(void)
{
    return CONTENT_IMAGE;
}

static const content_handler svg_content_handler = {.create = svg_create,
    .data_complete = svg_convert,
    .reformat = svg_reformat,
    .destroy = svg_destroy,
    .redraw = svg_redraw,
    .clone = svg_clone,
    .type = svg_content_type,
    .no_share = true};

static const char *svg_types[] = {"image/svg", "image/svg+xml"};


CONTENT_FACTORY_REGISTER_TYPES(svg, svg_types, svg_content_handler);
