#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <svgtiny.h>
typedef uint32_t colour;

#include "wisp/plot_style.h"
#include "wisp/plotters.h"
#include "wisp/utils/errors.h"

static bool svg_plot_combo_diagram(const struct svgtiny_diagram *diagram, int x, int y, int width, int height,
    const struct rect *clip, const struct redraw_context *ctx, colour background_colour, colour current_color)
{
    (void)background_colour;
    (void)current_color;

    float transform[6];
    plot_font_style_t fstyle = *plot_style_font;
    plot_style_t pstyle;
    nserror res;
    bool ok = true;

    assert(diagram);

    transform[0] = 1.0f;
    transform[1] = 0.0f;
    transform[2] = 0.0f;
    transform[3] = 1.0f;
    transform[4] = x;
    transform[5] = y;

#define BGR(c) (((svgtiny_RED((c))) | ((svgtiny_GREEN((c)) << 8)) | ((svgtiny_BLUE((c)) << 16))))

    unsigned int max_path_len = 0;
    for (unsigned int i = 0; i != diagram->shape_count; i++) {
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

    float sx = 1.0f;
    float sy = 1.0f;

    for (unsigned int i = 0; i != diagram->shape_count; i++) {
        if (diagram->shape[i].path) {
            if (diagram->shape[i].stroke == svgtiny_TRANSPARENT) {
                pstyle.stroke_type = PLOT_OP_TYPE_NONE;
                pstyle.stroke_colour = NS_TRANSPARENT;
            } else {
                pstyle.stroke_type = PLOT_OP_TYPE_SOLID;
                pstyle.stroke_colour = BGR(diagram->shape[i].stroke);
            }
            pstyle.stroke_width = plot_style_int_to_fixed(diagram->shape[i].stroke_width);

            if (diagram->shape[i].fill == svgtiny_TRANSPARENT) {
                pstyle.fill_type = PLOT_OP_TYPE_NONE;
                pstyle.fill_colour = NS_TRANSPARENT;
            } else {
                pstyle.fill_type = PLOT_OP_TYPE_SOLID;
                pstyle.fill_colour = BGR(diagram->shape[i].fill);
            }

            if (scaled != NULL) {
                unsigned int j = 0;
                unsigned int k = 0;

                float minx = 0.0f, maxx = 0.0f, miny = 0.0f, maxy = 0.0f;
                int initbb = 0;

                while (j < diagram->shape[i].path_length) {
                    int cmd = (int)diagram->shape[i].path[j++];
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

                if (clip == NULL || !(rx < clip->x0 || lx >= clip->x1 || by < clip->y0 || ty >= clip->y1)) {
                    int same = combo_active && combo_style.stroke_type == pstyle.stroke_type &&
                        combo_style.fill_type == pstyle.fill_type &&
                        combo_style.stroke_colour == pstyle.stroke_colour &&
                        combo_style.fill_colour == pstyle.fill_colour &&
                        combo_style.stroke_width == pstyle.stroke_width;

                    if (!same) {
                        if (combo_active && combo_len > 0) {
                            res = ctx->plot->path(ctx, &combo_style, combo, combo_len, transform);
                            if (res != NSERROR_OK) {
                                ok = false;
                            }
                            combo_len = 0;
                        }
                        combo_style = pstyle;
                        combo_active = 1;
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
                }
            } else {
                res = ctx->plot->path(ctx, &pstyle, diagram->shape[i].path, diagram->shape[i].path_length, transform);
                if (res != NSERROR_OK) {
                    ok = false;
                }
            }
        } else if (diagram->shape[i].text) {
            if (combo_active && combo_len > 0) {
                res = ctx->plot->path(ctx, &combo_style, combo, combo_len, transform);
                if (res != NSERROR_OK) {
                    ok = false;
                }
                combo_len = 0;
                combo_active = 0;
            }
            int px = (int)(diagram->shape[i].text_x * sx) + transform[4];
            int py = (int)(diagram->shape[i].text_y * sy) + transform[5];
            fstyle.background = 0xffffff;
            fstyle.foreground = 0x000000;
            fstyle.size = (8 * PLOT_STYLE_SCALE);
            res = ctx->plot->text(ctx, &fstyle, px, py, diagram->shape[i].text, strlen(diagram->shape[i].text));
            if (res != NSERROR_OK) {
                ok = false;
            }
        }
    }

#undef BGR

    if (combo_active && combo_len > 0) {
        res = ctx->plot->path(ctx, &combo_style, combo, combo_len, transform);
        if (res != NSERROR_OK) {
            ok = false;
        }
    }
    if (scaled)
        free(scaled);
    if (combo)
        free(combo);
    return ok;
}

bool svg_redraw_diagram(struct svgtiny_diagram *diagram, int x, int y, int width, int height, const struct rect *clip,
    const struct redraw_context *ctx, colour background_colour, colour current_color)
{
    return svg_plot_combo_diagram(diagram, x, y, width, height, clip, ctx, background_colour, current_color);
}
