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
#include <string.h>

#include <libcss/font_face.h>
#include <libcss/libcss.h>
#include <libcss/select.h>

#include <neosurf/content/handlers/html/private.h>
#include <neosurf/content/llcache.h>
#include <neosurf/desktop/gui_internal.h>
#include <neosurf/layout.h>
#include <neosurf/neosurf.h>
#include <neosurf/utils/errors.h>
#include <neosurf/utils/log.h>
#include <neosurf/utils/nsurl.h>

#include "content/handlers/html/font_face.h"

/** Maximum number of concurrent font downloads */
#define MAX_FONT_DOWNLOADS 32

/** Structure to track a font download */
struct font_download {
    char *family_name; /**< Font family name */
    llcache_handle *handle; /**< Fetch handle */
    bool in_use; /**< Whether this slot is in use */
};

/** Global array of font downloads */
static struct font_download font_downloads[MAX_FONT_DOWNLOADS];

/** Set of loaded font family names (simple linked list) */
struct loaded_font {
    char *family_name;
    struct loaded_font *next;
};

static struct loaded_font *loaded_fonts = NULL;

/** Count of pending font downloads */
static int pending_font_count = 0;

/** Callback to invoke when all fonts finish downloading */
static html_font_face_done_cb font_done_callback = NULL;

/** Current HTML content waiting for fonts (for proceed_to_done callback) */
static struct html_content *font_waiting_content = NULL;

/* Forward declaration */
extern void html_finish_conversion(struct html_content *htmlc);

/**
 * Check if all fonts have finished and invoke callback if set.
 */
static void check_fonts_done(void)
{
    if (pending_font_count == 0 && font_done_callback != NULL) {
        NSLOG(netsurf, INFO, "All font downloads complete, invoking callback");
        font_done_callback();
    }
    /* Notify content that fonts are done so it can continue box conversion */
    if (pending_font_count == 0 && font_waiting_content != NULL) {
        struct html_content *c = font_waiting_content;
        NSLOG(netsurf, INFO, "All fonts loaded, resuming box conversion for %p", c);
        html_finish_conversion(c);
    }
}

/**

 * Mark a font family as loaded
 */
static void mark_font_loaded(const char *family_name)
{
    struct loaded_font *entry;

    /* Check if already in list */
    for (entry = loaded_fonts; entry != NULL; entry = entry->next) {
        if (strcasecmp(entry->family_name, family_name) == 0) {
            return; /* Already loaded */
        }
    }

    /* Add to list */
    entry = malloc(sizeof(struct loaded_font));
    if (entry != NULL) {
        entry->family_name = strdup(family_name);
        entry->next = loaded_fonts;
        loaded_fonts = entry;
        NSLOG(netsurf, INFO, "Marked font '%s' as loaded", family_name);
    }
}

/**
 * Find a free download slot
 */
static struct font_download *find_free_slot(void)
{
    for (int i = 0; i < MAX_FONT_DOWNLOADS; i++) {
        if (!font_downloads[i].in_use) {
            return &font_downloads[i];
        }
    }
    return NULL;
}

/**
 * Callback for font file fetch (llcache)
 */
static nserror font_fetch_callback(llcache_handle *handle, const llcache_event *event, void *pw)
{
    struct font_download *dl = pw;

    switch (event->type) {
    case LLCACHE_EVENT_DONE: {
        /* Font download complete */
        const uint8_t *data;
        size_t size;

        data = llcache_handle_get_source_data(handle, &size);
        if (data != NULL && size > 0) {
            NSLOG(netsurf, INFO, "Font '%s' downloaded (%zu bytes)", dl->family_name, size);

            /* Load the font into the system via frontend table */
            nserror err = NSERROR_NOT_IMPLEMENTED;
            if (guit != NULL && guit->layout != NULL && guit->layout->load_font_data != NULL) {
                err = guit->layout->load_font_data(dl->family_name, data, size);
            }
            if (err == NSERROR_OK) {
                mark_font_loaded(dl->family_name);
            }
        }

        /* Clean up */
        llcache_handle_release(handle);
        free(dl->family_name);
        dl->family_name = NULL;
        dl->handle = NULL;
        dl->in_use = false;

        /* Decrement pending count and check if all done */
        pending_font_count--;
        check_fonts_done();
        break;
    }

    case LLCACHE_EVENT_ERROR:
        NSLOG(netsurf, WARNING, "Failed to download font '%s': %s", dl->family_name, event->data.error.msg);
        llcache_handle_release(handle);
        free(dl->family_name);
        dl->family_name = NULL;
        dl->handle = NULL;
        dl->in_use = false;

        /* Decrement pending count and check if all done */
        pending_font_count--;
        check_fonts_done();
        break;

    default:
        break;
    }

    return NSERROR_OK;
}

/**
 * Start downloading a font from a URL (using llcache for raw bytes)
 *
 * \param family_name  Font family name
 * \param font_url     Absolute URL of font file
 * \param base_url     Base URL for referer header (may be NULL)
 */
static nserror fetch_font_url(const char *family_name, nsurl *font_url, nsurl *base_url)
{
    struct font_download *dl;
    nserror err;

    /* Find a free slot */
    dl = find_free_slot();
    if (dl == NULL) {
        NSLOG(netsurf, WARNING, "No free font download slots");
        return NSERROR_NOMEM;
    }

    /* Set up the download */
    dl->family_name = strdup(family_name);
    if (dl->family_name == NULL) {
        return NSERROR_NOMEM;
    }
    dl->in_use = true;

    NSLOG(netsurf, INFO, "Fetching font '%s' from %s", family_name, nsurl_access(font_url));

    /* Start the fetch using llcache (raw bytes, no content handler needed)
     */
    err = llcache_handle_retrieve(font_url, 0, base_url, NULL, font_fetch_callback, dl, &dl->handle);
    if (err != NSERROR_OK) {
        free(dl->family_name);
        dl->family_name = NULL;
        dl->in_use = false;
        return err;
    }

    /* Increment pending download count */
    pending_font_count++;

    return NSERROR_OK;
}

/* Exported function documented in font_face.h */
nserror html_font_face_process(const css_font_face *font_face, const char *base_url)
{
    lwc_string *family = NULL;
    css_error css_err;
    uint32_t src_count;
    uint32_t i;
    nsurl *base = NULL;
    nserror err;

    if (font_face == NULL || base_url == NULL) {
        return NSERROR_BAD_PARAMETER;
    }

    /* Get font family name */
    css_err = css_font_face_get_font_family(font_face, &family);
    if (css_err != CSS_OK || family == NULL) {
        return NSERROR_OK; /* Skip this font */
    }

    const char *family_name = lwc_string_data(family);

    /* Check if already loaded */
    if (html_font_face_is_available(family_name)) {
        NSLOG(netsurf, DEBUG, "Font '%s' already available", family_name);
        return NSERROR_OK;
    }

    /* Get number of sources */
    css_err = css_font_face_count_srcs(font_face, &src_count);
    if (css_err != CSS_OK || src_count == 0) {
        return NSERROR_OK;
    }

    /* Parse base URL */
    err = nsurl_create(base_url, &base);
    if (err != NSERROR_OK) {
        return err;
    }

    /* Try each source until one works */
    for (i = 0; i < src_count; i++) {
        const css_font_face_src *src;
        lwc_string *location;
        css_font_face_location_type loc_type;
        css_font_face_format format;

        css_err = css_font_face_get_src(font_face, i, &src);
        if (css_err != CSS_OK) {
            continue;
        }

        loc_type = css_font_face_src_location_type(src);
        if (loc_type != CSS_FONT_FACE_LOCATION_TYPE_URI) {
            continue; /* Skip local fonts */
        }

        /* Check format - we support WOFF and OpenType/TrueType */
        format = css_font_face_src_format(src);
        if (format != CSS_FONT_FACE_FORMAT_UNSPECIFIED && format != CSS_FONT_FACE_FORMAT_WOFF &&
            format != CSS_FONT_FACE_FORMAT_OPENTYPE) {
            continue;
        }

        css_err = css_font_face_src_get_location(src, &location);
        if (css_err != CSS_OK || location == NULL) {
            continue;
        }

        /* Create absolute URL */
        nsurl *font_url;
        err = nsurl_join(base, lwc_string_data(location), &font_url);
        if (err != NSERROR_OK) {
            continue;
        }

        /* Fetch the font */
        err = fetch_font_url(family_name, font_url, base);
        nsurl_unref(font_url);

        if (err == NSERROR_OK) {
            break; /* Successfully started fetch */
        }
    }

    nsurl_unref(base);
    return NSERROR_OK;
}

/* Exported function documented in font_face.h */
nserror html_font_face_init(struct html_content *c, css_select_ctx *select_ctx)
{
    /* Store the content so we can notify it when fonts complete */
    font_waiting_content = c;
    (void)select_ctx;

    NSLOG(netsurf, INFO, "Font-face system initialized for content %p", c);
    return NSERROR_OK;
}

/* Exported function documented in font_face.h */
nserror html_font_face_fini(struct html_content *c)
{
    /* Clear the waiting content if it matches */
    if (font_waiting_content == c) {
        font_waiting_content = NULL;
    }
    return NSERROR_OK;
}

/* Exported function documented in font_face.h */
bool html_font_face_is_available(const char *family_name)
{
    struct loaded_font *entry;

    for (entry = loaded_fonts; entry != NULL; entry = entry->next) {
        if (strcasecmp(entry->family_name, family_name) == 0) {
            return true;
        }
    }

    return false;
}


/* Exported function documented in font_face.h */
void html_font_face_set_done_callback(html_font_face_done_cb cb)
{
    font_done_callback = cb;
}

/* Exported function documented in font_face.h */
bool html_font_face_has_pending(void)
{
    return pending_font_count > 0;
}

/* Get pending font count for logging */
int html_font_face_pending_count(void)
{
    return pending_font_count;
}
