/*
 * Copyright 2020 Vincent Sanders <vince@netsurf-browser.org>
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
 * HTML content object interface
 */

#ifndef WISP_HTML_OBJECT_H
#define WISP_HTML_OBJECT_H

struct html_content;
struct browser_window;
struct box;
struct nsurl;
struct dom_node;

enum html_object_state {
    HTML_OBJECT_UNKNOWN,
    HTML_OBJECT_PENDING,
    HTML_OBJECT_AVAILABLE,
    HTML_OBJECT_FAILED
};

/** Read-only resource state, also available to search without boxes. A changed
 * URL is independent of a previous request. This does not start a fetch. */
enum html_object_state html_object_get_state(
    const struct html_content *html, struct dom_node *node, const struct nsurl *url);

/**
 * Start a fetch for an object required by a page.
 *
 * The created content object is added to the HTML content which is
 *  updated as the fetch progresses. The box (if any) is updated when
 *  the object content becomes done.
 *
 * \param c content of type CONTENT_HTML
 * \param url URL of object to fetch
 * \param box box that will contain the object or NULL if none
 * \param permitted_types bitmap of acceptable types
 * \param background this is a background image
 * \return true on success, false on memory exhaustion
 */
bool html_fetch_background(
    struct html_content *c, struct nsurl *url, struct box *box, content_type permitted_types, uint32_t layer);

bool html_fetch_object(
    struct html_content *c, struct nsurl *url, struct box *box, content_type permitted_types, bool background);

/**
 * release memory of content objects associated with a HTML content
 *
 * The content objects contents should have been previously closed
 *  with html_object_close_objects().
 *
 * \param html The html content to release the objects from.
 * \return NSERROR_OK on success else appropriate error code.
 */
nserror html_object_free_objects(struct html_content *html);

/**
 * close content of content objects associated with a HTML content
 *
 * \param html The html content to close the objects from.
 * \return NSERROR_OK on success else appropriate error code.
 */
nserror html_object_close_objects(struct html_content *html);


/**
 * open content of content objects associated with a HTML content
 *
 * \param html The html content to open the objects from.
 * \param bw Browser window handle to open contents with.
 * \return NSERROR_OK on success else appropriate error code.
 */
nserror html_object_open_objects(struct html_content *html, struct browser_window *bw);


/**
 * abort any content objects that have not completed fetching.
 *
 * \param html The html content to abort the objects from.
 * \return NSERROR_OK on success else appropriate error code.
 */
nserror html_object_abort_objects(struct html_content *html);

/**
 * callback for deferred reformat
 *
 * \param p The html content
 */
void html_deferred_reformat(void *p);

/**
 * Create a content object from a raw data buffer.
 *
 * Like html_fetch_object, but takes raw data instead of a URL.
 * Uses hlcache_handle_retrieve_buffer internally for content-hash
 * deduplication and pinned cache semantics.
 *
 * \param c               content of type CONTENT_HTML
 * \param data            Raw data buffer
 * \param len             Byte length of data
 * \param mime_type       MIME type string
 * \param box             box that will contain the object
 * \param permitted_types bitmap of acceptable types
 * \return true on success, false on memory exhaustion
 */
bool html_fetch_object_buffer(struct html_content *c, const uint8_t *data, size_t len, const char *mime_type,
    struct box *box, content_type permitted_types);

struct box_construct_region;
struct html_object_binding_plan;

/** Prepare reuse of settled image resources for a detached candidate. No live
 * bindings, callbacks, cache handles or counters are changed. old_root may be
 * NULL for a previously hidden region. New/changed requests, pending resources
 * and embedded documents return NOT_IMPLEMENTED for full-refresh fallback.
 * The document, old tree and candidate must outlive the plan; candidate is
 * immutable until apply/discard. owner is a talloc allocation parent. */
nserror html_object_prepare_bindings(struct html_content *html, struct box *old_root,
    const struct box_construct_region *candidate, void *owner, struct html_object_binding_plan **result);

/** Read-only preflight for coordinated resource/form publication. */
nserror html_object_check_bindings(const struct html_object_binding_plan *plan);

/** Revalidate then apply every prepared binding without allocation or callbacks.
 * Must run inside publication with rendering/input excluded (locked/restyling).
 * This does not splice boxes or publish DOM/form/iframe links. On success the
 * caller must publish the candidate before yielding; it cannot just discard it.
 * A stale plan returns INVALID before changing any binding. Single use. */
nserror html_object_apply_bindings(struct html_object_binding_plan *plan);
/** Allocation-free write phase after all coordinated plans pass preflight.
 * Requires locked/restyling publication, with no intervening callbacks. */
void html_object_commit_bindings(struct html_object_binding_plan *plan);

/** Release only preparation storage, never resource handles. */
void html_object_discard_bindings(struct html_object_binding_plan *plan);

#endif
