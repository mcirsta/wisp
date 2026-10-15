/* SPDX-License-Identifier: GPL-2.0-or-later
 * Viewport/unit context shared by parsing, responsive images and layout. */
#include <wisp/content/handlers/html/private.h>
#include <wisp/content.h>
#include <wisp/content/hlcache.h>
#include <wisp/utils/nsoption.h>
#include <wisp/utils/log.h>
#include "content/handlers/css/utils.h"

void html_get_viewport_context(html_content *htmlc, css_unit_ctx *units, css_media *media)
{
    css_fixed device_dpi = nscss_screen_dpi;
    unsigned f_size;
    unsigned f_min;
    unsigned w = 0;
    unsigned h = 0;
    union content_msg_data msg_data = {
        .getdims =
            {
                .viewport_width = &w,
                .viewport_height = &h,
            },
    };

    content_broadcast(&htmlc->base, CONTENT_MSG_GETDIMS, &msg_data);


    w = css_unit_device2css_px(INTTOFIX(w), device_dpi);
    h = css_unit_device2css_px(INTTOFIX(h), device_dpi);

    media->width = w;
    media->height = h;
    units->viewport_width = w;
    units->viewport_height = h;
    units->container_width = w;
    units->container_height = h;
    units->device_dpi = device_dpi;

    NSLOG(wisp, DEEPDEBUG, "DIAG: html_get_dimensions: media.width=%u media.height=%u (CSS px)", FIXTOINT(w),
        FIXTOINT(h));

    /** \todo Change nsoption font sizes to px. */
    f_size = FDIV(FMUL(F_96, FDIV(INTTOFIX(nsoption_int(font_size)), F_10)), F_72);
    f_min = FDIV(FMUL(F_96, FDIV(INTTOFIX(nsoption_int(font_min_size)), F_10)), F_72);

    units->font_size_default = f_size;
    units->font_size_minimum = f_min;
}

void html_get_dimensions(html_content *html)
{
    html_get_viewport_context(html, &html->unit_len_ctx, &html->media);
}
