/* SPDX-License-Identifier: GPL-2.0-or-later
 * HTML image requests belong to elements, not transient layout boxes. Keep the
 * current image visible while a replacement is pending, and dispatch completion
 * from a task after resource completion, never from a cache callback or DOM mutation. */
#include "image.h"
#include "content/handlers/javascript/js.h"
#include <wisp/content.h>
#include <wisp/content/handlers/html/html.h>
#include "object.h"
#include <wisp/content/hlcache.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/log.h>
#include <wisp/utils/nsoption.h>
#include <wisp/utils/nsurl.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct image_request {
    struct html_image *image;
    hlcache_handle *handle;
    nsurl *url;
    double density;
    bool done, failed;
};

struct html_image {
    struct content_html_object object; /* Object-list ownership and box rebinding. */
    struct image_request *current, *pending;
    dom_string *src, *srcset, *sizes;
    bool dirty, queued, busy, stopped;
    bool event_pending, error_event, empty_broken;
    uint32_t revision;
};

static void image_task(void *pw);

bool html_image_is_element(dom_node *node)
{
    dom_html_element_type tag;
    return node != NULL && dom_html_element_get_tag_type(node, &tag) == DOM_NO_ERR && tag == DOM_HTML_ELEMENT_TYPE_IMG;
}

static struct html_image *image_find(const html_content *html, dom_node *node)
{
    struct html_image *image = NULL;
    dom_node_get_user_data(node, corestring_dom___ns_key_image_request, &image);
    return image != NULL && image->object.parent == &html->base ? image : NULL;
}

static bool image_document_active(struct html_image *image)
{
    html_content *html = (html_content *)image->object.parent;
    dom_document *owner = NULL;
    dom_node_get_owner_document(image->object.node, &owner);
    bool active = !html->aborted && !image->stopped && owner == html->document;
    dom_node_unref(owner);
    return active;
}

static void image_set_busy(struct html_image *image, bool busy)
{
    if (image->busy == busy)
        return;
    html_content *html = (html_content *)image->object.parent;
    if (busy) {
        html->images_active++;
        html->base.active++;
    } else {
        assert(html->images_active != 0 && html->base.active > 0);
        html->images_active--;
        html->base.active--;
    }
    image->busy = busy;
}

static nserror image_queue(struct html_image *image)
{
    if (image->queued)
        return NSERROR_OK;
    nserror err = guit->misc->schedule(0, image_task, image);
    if (err == NSERROR_OK) {
        image->queued = true;
        image_set_busy(image, true);
    }
    return err;
}

static void request_free(struct image_request *request)
{
    if (request == NULL)
        return;
    if (request->handle != NULL)
        hlcache_handle_release(request->handle);
    if (request->url != NULL)
        nsurl_unref(request->url);
    free(request);
}

static void image_unpublish(struct html_image *image)
{
    struct content_html_object *object = &image->object;
    if (object->opened && object->content != NULL)
        content_close(object->content);
    object->opened = false;
    object->content = NULL;
    if (object->box != NULL)
        object->box->object = NULL;
}

static void image_publish(struct html_image *image, bool changed)
{
    struct content_html_object *object = &image->object;
    struct image_request *current = image->current;
    hlcache_handle *content = current != NULL && !current->failed ? current->handle : NULL;
    if (object->content != content)
        image_unpublish(image);
    object->failed = current == NULL || current->failed;
    object->content = content;
    html_object_image_changed(object, changed);
}

static nserror image_callback(hlcache_handle *handle, const hlcache_event *event, void *pw)
{
    struct image_request *request = pw;
    struct html_image *image = request->image;
    html_content *html = (html_content *)image->object.parent;
    switch (event->type) {
    case CONTENT_MSG_GETTHREAD:
        *event->data.jsthread = NULL;
        return NSERROR_OK;
    case CONTENT_MSG_GETDIMS:
        *event->data.getdims.viewport_width = html->base.available_width;
        *event->data.getdims.viewport_height = html->base.available_height;
        return NSERROR_OK;
    case CONTENT_MSG_READY:
        request->handle = handle;
        if (request == image->current && image_document_active(image))
            image_publish(image, true);
        return NSERROR_OK;
    case CONTENT_MSG_DONE:
    case CONTENT_MSG_ERROR:
        /* Deferred pixel decoding can fail after headers reported DONE. */
        if (request->done && (event->type != CONTENT_MSG_ERROR || request->failed))
            return NSERROR_OK;
        request->done = true;
        request->failed = event->type == CONTENT_MSG_ERROR;
        if (!image_document_active(image)) {
            /* Release the retired element's load delay through the task too;
             * the old document may still be waiting for its last resource. */
            if (image_queue(image) != NSERROR_OK)
                image_set_busy(image, false);
            return NSERROR_OK;
        }
        /* The handle is supplied before retrieve returns on a cache hit. The
         * request stays alive until the queued task or a later source update. */
        request->handle = handle;
        if (request == image->pending) {
            image_unpublish(image);
            request_free(image->current);
            image->current = request;
            image->pending = NULL;
        }
        if (request == image->current) {
            image_publish(image, true);
            image->event_pending = true;
            image->error_event = request->failed;
            if (image_queue(image) != NSERROR_OK)
                image_set_busy(image, false);
        }
        return NSERROR_OK;
    case CONTENT_MSG_REDRAW:
        if (request == image->current && image->object.box != NULL && html->had_initial_layout)
            html__redraw_a_box(html, image->object.box);
        return NSERROR_OK;
    default:
        return NSERROR_OK;
    }
}

static nserror image_update(struct html_image *image)
{
    html_content *html = (html_content *)image->object.parent;
    nsurl *url = NULL;
    double density;
    if (!html_image_select_source(image->object.node, html, &url, &density))
        return NSERROR_NOMEM;
    image->dirty = false;
    struct image_request *latest = image->pending != NULL ? image->pending : image->current;
    if (url != NULL && latest != NULL && nsurl_compare(url, latest->url, NSURL_COMPLETE) && !latest->failed) {
        nsurl_unref(url);
        if (latest->density != density) {
            latest->density = density;
            if (latest == image->current)
                image_publish(image, true);
        }
        if (latest->done) {
            image->event_pending = true;
            image->error_event = false;
        }
        return NSERROR_OK;
    }
    image->event_pending = false;
    request_free(image->pending);
    image->pending = NULL;
    if (image->object.url != NULL)
        nsurl_unref(image->object.url);
    image->object.url = url != NULL ? nsurl_ref(url) : NULL;
    if (url == NULL) {
        image_unpublish(image);
        request_free(image->current);
        image->current = NULL;
        image->empty_broken = nsoption_bool(foreground_images);
        image_publish(image, true);
        /* Omitted src and srcset produce no error event; an explicitly empty
         * or unusable source does. Disabled images remain unavailable. */
        image->event_pending = nsoption_bool(foreground_images) && (image->src != NULL || image->srcset != NULL);
        image->error_event = true;
        return NSERROR_OK;
    }
    struct image_request *request = calloc(1, sizeof(*request));
    if (request == NULL) {
        nsurl_unref(url);
        return NSERROR_NOMEM;
    }
    request->image = image;
    request->url = url;
    request->density = density;
    if (image->current == NULL || image->current->failed || !image->current->done) {
        image_unpublish(image);
        request_free(image->current);
        image->current = request;
        image_publish(image, true);
    } else {
        image->pending = request;
    }
    image_set_busy(image, true);
    hlcache_child_context child = {.charset = html->encoding, .quirks = html->base.quirks};
    nserror err = hlcache_handle_retrieve(url, HLCACHE_RETRIEVE_SNIFF_TYPE, content_get_url(&html->base), NULL,
        image_callback, request, &child, CONTENT_IMAGE, &request->handle);
    if (err != NSERROR_OK) {
        request->done = request->failed = true;
        if (image->pending == request) {
            image_unpublish(image);
            request_free(image->current);
            image->current = request;
            image->pending = NULL;
        }
        image_publish(image, true);
        image->event_pending = image->error_event = true;
    } else if (image->current == request && !request->failed) {
        image->object.content = request->handle;
    }
    return err == NSERROR_NOMEM ? err : NSERROR_OK;
}

void html_image_update_at_checkpoint(html_content *html, dom_node *node, uint32_t revision)
{
    struct html_image *image = image_find(html, node);
    if (image == NULL || !image_document_active(image) || !image->dirty || image->revision != revision)
        return;
    if (image_update(image) != NSERROR_OK)
        content_broadcast_error(&html->base, NSERROR_NOMEM, NULL);
}

static void image_task(void *pw)
{
    struct html_image *image = pw;
    html_content *html = (html_content *)image->object.parent;
    image->queued = false;
    if (!image_document_active(image)) {
        html_image_stop(image);
        if (!html->aborted) {
            if (html_can_begin_conversion(html))
                html_begin_conversion(html);
            else
                html_proceed_to_done(html);
        }
        return;
    }
    bool updating = image->dirty;
    if (updating && image_update(image) != NSERROR_OK) {
        image_set_busy(image, false);
        return;
    }
    if (updating && image->event_pending) {
        if (image_queue(image) != NSERROR_OK)
            image_set_busy(image, false);
        return;
    }
    /* A broken request retains its URL/state, not the failed decoder/cache
     * consumer. Release outside the cache callback, including synchronous hits. */
    if (image->current != NULL && image->current->failed && image->current->handle != NULL) {
        hlcache_handle_release(image->current->handle);
        image->current->handle = NULL;
    }
    bool event = image->event_pending;
    bool failed = image->error_event;
    image->event_pending = false;
    if (image->pending == NULL && (image->current == NULL || image->current->done))
        image_set_busy(image, false);
    uint64_t generation = html->input_generation;
    if (!content_add_user(&html->base, NULL, &generation))
        return;
    if (event) {
        dom_string *type = failed ? corestring_dom_error : corestring_dom_load;
        if (html->jsthread != NULL)
            js_fire_element_event(html->jsthread, image->object.node, type);
        else
            fire_generic_dom_event(type, image->object.node, false, false);
    }
    /* Listeners may replace the source, remove/adopt the node or navigate.
     * Only the pinned document is accessed after dispatch. */
    if (!html->aborted && generation == html->input_generation) {
        if (html_can_begin_conversion(html))
            html_begin_conversion(html);
        else {
            if (html->had_initial_layout && html->base.active == html->scripts_active && !html->pending_reformat) {
                html->pending_reformat = true;
                guit->misc->schedule(0, html_deferred_reformat, html);
            }
            html_proceed_to_done(html);
        }
    }
    content_remove_user(&html->base, NULL, &generation);
}

/* libdom string equality treats NULL as an empty string. Presence matters for
 * image source attributes (notably omitted versus empty srcset). */
static bool image_attribute_equal(dom_string *a, dom_string *b)
{
    return (a == NULL) == (b == NULL) && dom_string_isequal(a, b);
}

nserror html_image_schedule(html_content *html, dom_node *node, bool attribute_changed)
{
    if (!html_image_is_element(node) || html->aborted)
        return NSERROR_OK;
    dom_string *src = NULL, *srcset = NULL, *sizes = NULL;
    dom_exception exc = dom_element_get_attribute(node, corestring_dom_src, &src);
    if (exc == DOM_NO_ERR)
        exc = dom_element_get_attribute(node, corestring_dom_srcset, &srcset);
    if (exc == DOM_NO_ERR)
        exc = dom_element_get_attribute(node, corestring_dom_sizes, &sizes);
    nserror result = NSERROR_NOMEM;
    if (exc != DOM_NO_ERR)
        goto out;
    struct html_image *image = image_find(html, node);
    if (image != NULL && !attribute_changed && image_attribute_equal(src, image->src) &&
        image_attribute_equal(srcset, image->srcset) && image_attribute_equal(sizes, image->sizes)) {
        result = NSERROR_OK;
        goto out;
    }
    if (image == NULL) {
        image = calloc(1, sizeof(*image));
        if (image == NULL)
            goto out;
        void *old;
        if (dom_node_set_user_data(node, corestring_dom___ns_key_image_request, image, NULL, &old) != DOM_NO_ERR) {
            free(image);
            goto out;
        }
        /* The object list owns the record and retains the node. User data is
         * only an O(1) borrowed lookup, is not cloned, and is cleared on fini.
         * Adoption may replace another document's lookup without owning it. */
        image->object.parent = &html->base;
        image->object.image = image;
        image->object.node = dom_node_ref(node);
        image->object.permitted_types = CONTENT_IMAGE;
        image->object.layout_resource = true;
        image->object.next = html->object_list;
        html->object_list = &image->object;
        html->num_objects++;
    }
    dom_string_unref(image->src);
    dom_string_unref(image->srcset);
    dom_string_unref(image->sizes);
    image->src = dom_string_ref(src);
    image->srcset = dom_string_ref(srcset);
    image->sizes = dom_string_ref(sizes);
    image->dirty = true;
    image->revision++;
    image->event_pending = false;
    image->stopped = false;
    result = image_queue(image);
    /* JS source writes join the same FIFO as promise/queueMicrotask jobs. The
     * task also covers parser discovery and a checkpoint discarded on timeout. */
    if (result == NSERROR_OK && html->jsthread != NULL)
        js_queue_image_update(html->jsthread, node, image->revision);
out:
    dom_string_unref(src);
    dom_string_unref(srcset);
    dom_string_unref(sizes);
    return result;
}

nserror html_image_bind(html_content *html, dom_node *node, struct box *box)
{
    nserror err = html_image_schedule(html, node, false);
    struct html_image *image = image_find(html, node);
    if (err != NSERROR_OK || image == NULL)
        return err;
    /* A geometry read must not run an image-update microtask early. */
    if (image->dirty && !html->layout_syncing && (err = image_update(image)) != NSERROR_OK)
        return err;
    image->object.box = box;
    image_publish(image, false);
    return NSERROR_OK;
}

/* Bound hostile 0x/tiny-density dimensions to the CSS engine's finite length
 * range before integer conversion. Infinite density (sizes=0) yields zero. */
static unsigned image_natural_dimension(const struct image_request *request, int pixels)
{
    if (pixels <= 0)
        return 0;
    double value = request->density > 0 ? pixels / request->density : INFINITY;
    return (unsigned)fmin(value, INT_MAX / F_1);
}

bool html_image_dimensions(const struct box *box, int *width, int *height)
{
    /* Only IMG requests have a selected-source density. The decoded cache can
     * be shared by elements with different source sets, so do not modify it. */
    if (!html_image_is_element(box->node))
        return false;
    struct html_image *image = NULL;
    dom_node_get_user_data(box->node, corestring_dom___ns_key_image_request, &image);
    if (image == NULL || image->current == NULL || image->current->handle != box->object || image->current->failed)
        return false;
    /* A ratio-only SVG still needs the default object-size algorithm. Zero
     * produced by density correction is different from missing source lengths. */
    bool have_size = *width > 0 && *height > 0;
    *width = image_natural_dimension(image->current, *width);
    *height = image_natural_dimension(image->current, *height);
    return have_size;
}

void html_image_get_state(html_content *html, dom_node *node, struct html_image_state *state)
{
    *state = (struct html_image_state){0};
    struct html_image *image = image_find(html, node);
    if (image != NULL && !image_document_active(image))
        image = NULL;
    dom_string *src = NULL, *srcset = NULL;
    dom_element_get_attribute(node, corestring_dom_src, &src);
    dom_element_get_attribute(node, corestring_dom_srcset, &srcset);
    bool empty = srcset == NULL && (src == NULL || dom_string_byte_length(src) == 0);
    dom_string_unref(src);
    dom_string_unref(srcset);
    state->complete = empty ||
        (image != NULL && image->pending == NULL &&
            (image->current != NULL ? image->current->done : image->empty_broken));
    if (image != NULL && image->current != NULL) {
        struct image_request *current = image->current;
        state->current_src = current->url;
        if (!current->failed && current->handle != NULL &&
            (content_get_status(current->handle) == CONTENT_STATUS_READY ||
                content_get_status(current->handle) == CONTENT_STATUS_DONE)) {
            state->natural_width = image_natural_dimension(current, content_get_width(current->handle));
            state->natural_height = image_natural_dimension(current, content_get_height(current->handle));
        }
    }
}

dom_exception html_image_get_dimension(html_content *html, dom_node *node,
    const struct box *box, bool height, uint32_t *value)
{
    /* https://html.spec.whatwg.org/multipage/embedded-content-other.html#dimension-attributes
     * Transforms, padding, borders and object-fit do not change the content size. */
    if (box != NULL) {
        double pixels = (height ? box->height : box->width) * 96.0 / FIXTOFLT(html->unit_len_ctx.device_dpi);
        *value = (uint32_t)fmin(UINT32_MAX, fmax(0, round(pixels)));
        return DOM_NO_ERR;
    }
    struct html_image_state state;
    html_image_get_state(html, node, &state);
    *value = height ? state.natural_height : state.natural_width;
    dom_string *attribute = NULL;
    dom_exception err = dom_element_get_attribute(node, height ? corestring_dom_height : corestring_dom_width, &attribute);
    if (err != DOM_NO_ERR || attribute == NULL)
        return err;
    /* HTML non-negative integer parsing, not C's base-detecting strtoul:
     * decimal prefix, ASCII whitespace (not VT), optional sign, and -0 allowed.
     * Accumulate the IDL unsigned-long result without signed overflow. */
    const char *p = dom_string_data(attribute), *end = p + dom_string_byte_length(attribute);
    while (p != end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f'))
        p++;
    bool negative = p != end && *p == '-';
    if (p != end && (*p == '+' || *p == '-'))
        p++;
    if (p != end && *p >= '0' && *p <= '9') {
        uint32_t parsed = 0;
        bool nonzero = false;
        while (p != end && *p >= '0' && *p <= '9') {
            unsigned digit = *p++ - '0';
            nonzero |= digit != 0;
            parsed = parsed * 10u + digit;
        }
        if (!negative || !nonzero)
            *value = parsed;
    }
    dom_string_unref(attribute);
    return DOM_NO_ERR;
}

void html_image_stop(struct html_image *image)
{
    guit->misc->schedule(-1, image_task, image);
    image->queued = image->dirty = image->event_pending = false;
    image->stopped = true;
    request_free(image->pending);
    image->pending = NULL;
    if (image->current != NULL && !image->current->done) {
        image_unpublish(image);
        request_free(image->current);
        image->current = NULL;
    }
    image_set_busy(image, false);
}

void html_image_fini(struct html_image *image)
{
    void *stored = NULL, *old;
    dom_node_get_user_data(image->object.node, corestring_dom___ns_key_image_request, &stored);
    if (stored == image)
        dom_node_set_user_data(image->object.node, corestring_dom___ns_key_image_request, NULL, NULL, &old);
    /* HTML destruction releases its box arena before its resource records. */
    image->object.box = NULL;
    html_image_stop(image);
    image_unpublish(image);
    request_free(image->current);
    dom_string_unref(image->src);
    dom_string_unref(image->srcset);
    dom_string_unref(image->sizes);
}
