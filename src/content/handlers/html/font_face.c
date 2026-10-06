/*
 * Copyright 2024 NeoSurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 *
 * NeoSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NeoSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Web font (font-face) loading implementation.
 */

#include <stdlib.h>

#include <libcss/font_face.h>
#include <libcss/libcss.h>
#include <libcss/select.h>

#include <wisp/content/handlers/html/private.h>
#include <wisp/content/llcache.h>
#include <wisp/layout.h>
#include <wisp/utils/errors.h>
#include <wisp/utils/nsurl.h>

#include "content/handlers/html/font_face.h"

/* CSS Fonts 3 §4.1: downloaded faces are available only to documents which
 * reference them. The URL cache may share bytes, never CSS family bindings. */
struct font_download {
    struct html_content *owner;
    const css_font_face *face; /* Owned by the document's selection context. */
    struct font_variant_id variant;
    llcache_handle *handle;
    uint32_t next_src;
};

struct html_font_face_set {
    uint32_t count;
    unsigned pending;
    struct font_download faces[];
};

static uint64_t next_font_set_id;
static uint64_t font_generation;

uint64_t html_font_face_generation(void)
{
    return font_generation;
}

void html_font_face_notify_changed(void)
{
    font_generation++;
}

static nserror font_fetch_callback(llcache_handle *handle, const llcache_event *event, void *pw);

/* libcss has already resolved src URLs against the sheet containing the rule,
 * including imported sheets. Keep src order when a fetch or decoder fails. */
static bool fetch_next_source(struct font_download *dl)
{
    uint32_t count = 0;
    css_font_face_count_srcs(dl->face, &count);
    while (dl->next_src < count) {
        const css_font_face_src *src;
        lwc_string *location;
        nsurl *url;
        if (css_font_face_get_src(dl->face, dl->next_src++, &src) != CSS_OK ||
            css_font_face_src_location_type(src) != CSS_FONT_FACE_LOCATION_TYPE_URI)
            continue;
        css_font_face_format format = css_font_face_src_format(src);
        if (format != CSS_FONT_FACE_FORMAT_UNSPECIFIED && format != CSS_FONT_FACE_FORMAT_WOFF &&
            format != CSS_FONT_FACE_FORMAT_OPENTYPE)
            continue;
        if (css_font_face_src_get_location(src, &location) != CSS_OK || location == NULL ||
            nsurl_create(lwc_string_data(location), &url) != NSERROR_OK)
            continue;
        nserror err = llcache_handle_retrieve(url, 0, dl->owner->base_url, NULL, font_fetch_callback, dl, &dl->handle);
        nsurl_unref(url);
        if (err == NSERROR_OK)
            return true;
    }
    return false;
}

static nserror font_fetch_callback(llcache_handle *handle, const llcache_event *event, void *pw)
{
    struct font_download *dl = pw;
    html_content *owner = dl->owner;
    nserror error = NSERROR_INVALID;

    if (event->type != LLCACHE_EVENT_DONE && event->type != LLCACHE_EVENT_ERROR)
        return NSERROR_OK;
    if (event->type == LLCACHE_EVENT_DONE) {
        size_t size;
        const uint8_t *data = llcache_handle_get_source_data(handle, &size);
        if (data != NULL && size != 0)
            error = owner->font_func->load_font_data(&dl->variant, data, size);
        if (error == NSERROR_OK)
            html_font_face_notify_changed();
    }
    llcache_handle_release(handle);
    dl->handle = NULL;
    if (error != NSERROR_OK && fetch_next_source(dl))
        return NSERROR_OK;

    owner->font_faces->pending--;
    /* Clear the download before resuming: conversion can destroy its owner. */
    if (owner->font_faces->pending == 0 && owner->font_wait_start_ms != 0)
        html_finish_conversion(owner);
    return NSERROR_OK;
}

nserror html_font_face_init(html_content *c, css_select_ctx *select_ctx)
{
    css_select_font_faces_results *selected = NULL;
    if (c->font_set_id != 0)
        return NSERROR_OK;
    c->font_set_id = ++next_font_set_id;
    if (c->font_func == NULL || c->font_func->load_font_data == NULL)
        return NSERROR_OK;
    css_error error = css_select_font_faces(select_ctx, &c->media, &c->unit_len_ctx, NULL, &selected);
    if (error != CSS_OK)
        return error == CSS_NOMEM ? NSERROR_NOMEM : NSERROR_CSS;
    if (selected == NULL)
        return NSERROR_OK;

    c->font_faces = calloc(1, sizeof(*c->font_faces) + selected->n_font_faces * sizeof(struct font_download));
    if (c->font_faces == NULL) {
        css_select_font_faces_results_destroy(selected);
        return NSERROR_NOMEM;
    }
    c->font_faces->count = selected->n_font_faces;
    for (uint32_t i = 0; i < selected->n_font_faces; i++) {
        struct font_download *dl = &c->font_faces->faces[i];
        lwc_string *family = NULL;
        dl->owner = c;
        dl->face = selected->font_faces[i];
        css_font_face_get_font_family(dl->face, &family);
        /* Family storage, like the face itself, outlives the pending fetch. */
        dl->variant.family_name = lwc_string_data(family);
        dl->variant.font_set = c->font_set_id;
        dl->variant.order = i;
        dl->variant.weight = css_font_face_font_weight(dl->face);
        dl->variant.style = css_font_face_font_style(dl->face);
        if (fetch_next_source(dl))
            c->font_faces->pending++;
    }
    css_select_font_faces_results_destroy(selected);
    return NSERROR_OK;
}

/* llcache requires a callable callback even during abort: curl can emit a
 * final progress notification synchronously while removing its transfer. */
static nserror font_fetch_discard(llcache_handle *handle, const llcache_event *event, void *pw)
{
    (void)handle;
    (void)event;
    (void)pw;
    return NSERROR_OK;
}

void html_font_face_stop(html_content *c)
{
    if (c->font_faces == NULL)
        return;
    for (uint32_t i = 0; i < c->font_faces->count; i++) {
        struct font_download *dl = &c->font_faces->faces[i];
        if (dl->handle != NULL) {
            /* Detach first. An abort must never call back into the departing
             * document; llcache preserves other users of a shared URL. */
            llcache_handle_change_callback(dl->handle, font_fetch_discard, NULL);
            llcache_handle_abort(dl->handle);
            llcache_handle_release(dl->handle);
            dl->handle = NULL;
        }
    }
    c->font_faces->pending = 0;
}

void html_font_face_fini(html_content *c)
{
    html_font_face_stop(c);
    if (c->font_set_id != 0 && c->font_func != NULL && c->font_func->release_font_set != NULL)
        c->font_func->release_font_set(c->font_set_id);
    free(c->font_faces);
    c->font_faces = NULL;
    c->font_set_id = 0;
}

unsigned html_font_face_pending_count(const html_content *c)
{
    return c->font_faces != NULL ? c->font_faces->pending : 0;
}

bool html_font_face_has_pending(const html_content *c)
{
    return html_font_face_pending_count(c) != 0;
}
