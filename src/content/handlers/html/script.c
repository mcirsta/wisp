/*
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
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
 * implementation of content handling for text/html scripts.
 */

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <neosurf/content.h>
#include <neosurf/content/content_protected.h>
#include <neosurf/content/fetch.h>
#include <neosurf/content/hlcache.h>
#include <neosurf/desktop/gui_internal.h>
#include <neosurf/misc.h>
#include <neosurf/utils/config.h>
#include <neosurf/utils/corestrings.h>
#include <neosurf/utils/log.h>
#include <neosurf/utils/messages.h>
#include "content/content_factory.h"
#include "content/handlers/javascript/js.h"

#include <neosurf/content/handlers/html/html.h>
#include <neosurf/content/handlers/html/private.h>

#include <nsutils/time.h>

/* Lightweight performance timing macro */
#define PERF_ENABLED 1
#if PERF_ENABLED
#define PERF(fmt, ...)                                                                                                 \
    do {                                                                                                               \
        uint64_t _ms;                                                                                                  \
        nsu_getmonotonic_ms(&_ms);                                                                                     \
        fprintf(stderr, "PERF[%6lu.%03lu] " fmt "\n", (unsigned long)(_ms / 1000), (unsigned long)(_ms % 1000),        \
            ##__VA_ARGS__);                                                                                            \
        fflush(stderr);                                                                                                \
    } while (0)
#else
#define PERF(fmt, ...) ((void)0)
#endif

typedef bool(script_handler_t)(struct jsthread *jsthread, const uint8_t *data, size_t size, const char *name);


static script_handler_t *select_script_handler(content_type ctype)
{
    switch (ctype) {
    case CONTENT_JS:
        return js_exec;
    default:
        return NULL;
    }
}

void script_resume_conversion_cb(void *p)
{
    html_content *htmlc = p;
    html_begin_conversion(htmlc);
}


/* exported internal interface documented in html/html_internal.h */
nserror html_script_exec(html_content *c, bool allow_defer)
{
    unsigned int i;
    struct html_script *s;
    script_handler_t *script_handler;
    bool have_run_something = false;

    if (c->jsthread == NULL) {
        return NSERROR_BAD_PARAMETER;
    }

    for (i = 0, s = c->scripts; i != c->scripts_count; i++, s++) {
        if (s->already_started) {
            continue;
        }

        if ((s->type == HTML_SCRIPT_ASYNC) || (allow_defer && (s->type == HTML_SCRIPT_DEFER))) {
            /* ensure script content is present */
            if (s->data.handle == NULL)
                continue;

            /* ensure script content fetch status is not an error */
            if (content_get_status(s->data.handle) == CONTENT_STATUS_ERROR)
                continue;

            /* ensure script handler for content type */
            script_handler = select_script_handler(content_get_type(s->data.handle));
            if (script_handler == NULL)
                continue; /* unsupported type */

            if (content_get_status(s->data.handle) == CONTENT_STATUS_DONE) {
                /* external script is now available */
                const uint8_t *data;
                size_t size;
                data = content_get_source_data(s->data.handle, &size);
                script_handler(c->jsthread, data, size, nsurl_access(hlcache_handle_get_url(s->data.handle)));
                have_run_something = true;
                /* We have to re-acquire this here since the
                 * c->scripts array may have been reallocated
                 * as a result of executing this script.
                 */
                s = &(c->scripts[i]);

                s->already_started = true;
            }
        }
    }

    if (have_run_something) {
        return html_proceed_to_done(c);
    }

    return NSERROR_OK;
}

/* create new html script entry */
static struct html_script *html_process_new_script(html_content *c, dom_string *mimetype, enum html_script_type type)
{
    struct html_script *nscript;
    /* add space for new script entry */
    nscript = realloc(c->scripts, sizeof(struct html_script) * (c->scripts_count + 1));
    if (nscript == NULL) {
        return NULL;
    }

    c->scripts = nscript;

    /* increment script entry count */
    nscript = &c->scripts[c->scripts_count];
    c->scripts_count++;

    nscript->already_started = false;
    nscript->parser_inserted = false;
    nscript->force_async = true;
    nscript->ready_exec = false;
    nscript->async = false;
    nscript->defer = false;

    nscript->type = type;

    nscript->mimetype = dom_string_ref(mimetype); /* reference mimetype */

    return nscript;
}

/**
 * Callback for asyncronous scripts
 */
static nserror convert_script_async_cb(hlcache_handle *script, const hlcache_event *event, void *pw)
{
    html_content *parent = pw;
    unsigned int i;
    struct html_script *s;

    /* Find script */
    for (i = 0, s = parent->scripts; i != parent->scripts_count; i++, s++) {
        if (s->type == HTML_SCRIPT_ASYNC && s->data.handle == script)
            break;
    }

    assert(i != parent->scripts_count);

    switch (event->type) {
    case CONTENT_MSG_LOADING:
        break;

    case CONTENT_MSG_READY:
        break;

    case CONTENT_MSG_DONE:
        PERF("SCRIPT ASYNC DONE %d '%s' (active=%d)", i, nsurl_access(hlcache_handle_get_url(script)),
            parent->base.active - 1);
        NSLOG(neosurf, INFO, "script %d done '%s'", i, nsurl_access(hlcache_handle_get_url(script)));
        if (parent->base.active == 0) {
            NSLOG(neosurf, CRITICAL,
                "ACTIVE UNDERFLOW! async_cb DONE decrement when 0 "
                "[content=%p script=%s]",
                parent, nsurl_access(hlcache_handle_get_url(script)));
        }
        parent->base.active--;
        parent->scripts_active--;
        NSLOG(neosurf, INFO, "%d fetches active", parent->base.active);

        break;

    case CONTENT_MSG_ERROR:
        NSLOG(neosurf, INFO, "script %s failed: %s", nsurl_access(hlcache_handle_get_url(script)),
            event->data.errordata.errormsg);

        hlcache_handle_release(script);
        s->data.handle = NULL;
        if (parent->base.active == 0) {
            NSLOG(neosurf, CRITICAL,
                "ACTIVE UNDERFLOW! async_cb ERROR decrement when 0 "
                "[content=%p script=%s]",
                parent, nsurl_access(hlcache_handle_get_url(script)));
        }
        parent->base.active--;
        parent->scripts_active--;
        NSLOG(neosurf, INFO, "%d fetches active", parent->base.active);

        break;

    default:
        break;
    }

    /* if there are no active fetches remaining begin post parse
     * conversion
     */
    if (html_can_begin_conversion(parent)) {
        guit->misc->schedule(0, script_resume_conversion_cb, parent);
    }

    /* if we have already started converting though, then we can handle the
     * scripts as they come in.
     */
    else if (parent->conversion_begun) {
        return html_script_exec(parent, false);
    }

    return NSERROR_OK;
}

/**
 * Callback for defer scripts
 */
static nserror convert_script_defer_cb(hlcache_handle *script, const hlcache_event *event, void *pw)
{
    html_content *parent = pw;
    unsigned int i;
    struct html_script *s;

    /* Find script */
    for (i = 0, s = parent->scripts; i != parent->scripts_count; i++, s++) {
        if (s->type == HTML_SCRIPT_DEFER && s->data.handle == script)
            break;
    }

    assert(i != parent->scripts_count);

    switch (event->type) {

    case CONTENT_MSG_DONE:
        PERF("SCRIPT DEFER DONE %d '%s' (active=%d)", i, nsurl_access(hlcache_handle_get_url(script)),
            parent->base.active - 1);
        NSLOG(neosurf, INFO, "script %d done '%s'", i, nsurl_access(hlcache_handle_get_url(script)));
        if (parent->base.active == 0) {
            NSLOG(neosurf, CRITICAL,
                "ACTIVE UNDERFLOW! defer_cb DONE decrement when 0 "
                "[content=%p script=%s]",
                parent, nsurl_access(hlcache_handle_get_url(script)));
        }
        parent->base.active--;
        parent->scripts_active--;
        NSLOG(neosurf, INFO, "%d fetches active", parent->base.active);

        break;

    case CONTENT_MSG_ERROR:
        NSLOG(neosurf, INFO, "script %s failed: %s", nsurl_access(hlcache_handle_get_url(script)),
            event->data.errordata.errormsg);

        hlcache_handle_release(script);
        s->data.handle = NULL;
        if (parent->base.active == 0) {
            NSLOG(neosurf, CRITICAL,
                "ACTIVE UNDERFLOW! defer_cb ERROR decrement when 0 "
                "[content=%p script=%s]",
                parent, nsurl_access(hlcache_handle_get_url(script)));
        }
        parent->base.active--;
        parent->scripts_active--;
        NSLOG(neosurf, INFO, "%d fetches active", parent->base.active);

        break;

    default:
        break;
    }

    /* if there are no active fetches remaining begin post parse
     * conversion
     */
    if (html_can_begin_conversion(parent)) {
        guit->misc->schedule(0, script_resume_conversion_cb, parent);
    }

    return NSERROR_OK;
}

/**
 * Callback for syncronous scripts
 */
static nserror convert_script_sync_cb(hlcache_handle *script, const hlcache_event *event, void *pw)
{
    html_content *parent = pw;
    unsigned int i;
    struct html_script *s;
    script_handler_t *script_handler;
    dom_hubbub_error err;
    unsigned int active_sync_scripts = 0;

    /* Count sync scripts which have yet to complete (other than us) */
    for (i = 0, s = parent->scripts; i != parent->scripts_count; i++, s++) {
        if (s->type == HTML_SCRIPT_SYNC && s->data.handle != script && s->already_started == false) {
            active_sync_scripts++;
        }
    }

    /* Find script */
    for (i = 0, s = parent->scripts; i != parent->scripts_count; i++, s++) {
        if (s->type == HTML_SCRIPT_SYNC && s->data.handle == script)
            break;
    }

    assert(i != parent->scripts_count);

    switch (event->type) {
    case CONTENT_MSG_DONE:
        PERF("SCRIPT SYNC DONE %d '%s' (active=%d)", i, nsurl_access(hlcache_handle_get_url(script)),
            parent->base.active - 1);
        NSLOG(neosurf, INFO, "script %d done '%s'", i, nsurl_access(hlcache_handle_get_url(script)));
        NSLOG(neosurf, INFO, "DIAG: sync_cb DONE: parent=%p active=%d->%d", parent, parent->base.active,
            parent->base.active - 1);
        if (parent->base.active == 0) {
            NSLOG(neosurf, CRITICAL,
                "ACTIVE UNDERFLOW! sync_cb DONE decrement when 0 "
                "[content=%p script=%s]",
                parent, nsurl_access(hlcache_handle_get_url(script)));
        }
        parent->base.active--;
        parent->scripts_active--;
        NSLOG(neosurf, INFO, "%d fetches active", parent->base.active);

        s->already_started = true;

        /* attempt to execute script */
        script_handler = select_script_handler(content_get_type(s->data.handle));
        if (script_handler != NULL && parent->jsthread != NULL) {
            /* script has a handler */
            const uint8_t *data;
            size_t size;
            data = content_get_source_data(s->data.handle, &size);
            script_handler(parent->jsthread, data, size, nsurl_access(hlcache_handle_get_url(s->data.handle)));
        }

        /* continue parse */
        if (parent->parser != NULL && active_sync_scripts == 0) {
            err = dom_hubbub_parser_pause(parent->parser, false);
            if (err != DOM_HUBBUB_OK) {
                NSLOG(neosurf, INFO, "unpause returned 0x%x", err);
            }
        }

        break;

    case CONTENT_MSG_ERROR:
        NSLOG(neosurf, INFO, "script %s failed: %s", nsurl_access(hlcache_handle_get_url(script)),
            event->data.errordata.errormsg);

        hlcache_handle_release(script);
        s->data.handle = NULL;
        if (parent->base.active == 0) {
            NSLOG(neosurf, CRITICAL,
                "ACTIVE UNDERFLOW! sync_cb ERROR decrement when 0 "
                "[content=%p script=%s]",
                parent, nsurl_access(hlcache_handle_get_url(script)));
        }
        parent->base.active--;
        parent->scripts_active--;

        NSLOG(neosurf, INFO, "%d fetches active", parent->base.active);

        s->already_started = true;

        /* continue parse */
        if (parent->parser != NULL && active_sync_scripts == 0) {
            err = dom_hubbub_parser_pause(parent->parser, false);
            if (err != DOM_HUBBUB_OK) {
                NSLOG(neosurf, INFO, "unpause returned 0x%x", err);
            }
        }

        break;

    default:
        break;
    }

    /* if there are no active fetches remaining begin post parse
     * conversion
     */
    if (html_can_begin_conversion(parent)) {
        guit->misc->schedule(0, script_resume_conversion_cb, parent);
    }

    /* if we have already started converting, execute pending scripts
     * and proceed to done state if all scripts have finished
     */
    else if (parent->conversion_begun) {
        return html_script_exec(parent, false);
    }

    return NSERROR_OK;
}

/**
 * process a script with a src tag
 */
static dom_hubbub_error exec_src_script(html_content *c, dom_node *node, dom_string *mimetype, dom_string *src)
{
    nserror ns_error;
    nsurl *joined;
    hlcache_child_context child;
    struct html_script *nscript;
    bool async;
    bool defer;
    enum html_script_type script_type;
    hlcache_handle_callback script_cb;
    dom_hubbub_error ret = DOM_HUBBUB_OK;
    dom_exception exc; /* returned by libdom functions */

    /* src url */
    ns_error = nsurl_join(c->base_url, dom_string_data(src), &joined);
    if (ns_error != NSERROR_OK) {
        content_broadcast_error(&c->base, NSERROR_NOMEM, NULL);
        return DOM_HUBBUB_NOMEM;
    }

    NSLOG(neosurf, INFO, "script %i '%s'", c->scripts_count, nsurl_access(joined));

    /* there are three ways to process the script tag at this point:
     *
     * Syncronously  pause the parent parse and continue after
     *                 the script has downloaded and executed. (default)
     * Async         Start the script downloading and execute it when it
     *                 becomes available.
     * Defered       Start the script downloading and execute it when
     *                 the page has completed parsing, may be set along
     *                 with async where it is ignored.
     */

    /* we interpret the presence of the async and defer attribute
     * as true and ignore its value, technically only the empty
     * value or the attribute name itself are valid. However
     * various browsers interpret this in various ways the most
     * compatible approach is to be liberal and accept any
     * value. Note setting the values to "false" still makes them true!
     */
    exc = dom_element_has_attribute(node, corestring_dom_async, &async);
    if (exc != DOM_NO_ERR) {
        return DOM_HUBBUB_OK; /* dom error */
    }

    if (c->parse_completed) {
        /* After parse completed, all scripts are essentially async */
        async = true;
        defer = false;
    }

    if (async) {
        /* asyncronous script */
        script_type = HTML_SCRIPT_ASYNC;
        script_cb = convert_script_async_cb;

    } else {
        exc = dom_element_has_attribute(node, corestring_dom_defer, &defer);
        if (exc != DOM_NO_ERR) {
            return DOM_HUBBUB_OK; /* dom error */
        }

        if (defer) {
            /* defered script */
            script_type = HTML_SCRIPT_DEFER;
            script_cb = convert_script_defer_cb;
        } else {
            /* syncronous script */
            script_type = HTML_SCRIPT_SYNC;
            script_cb = convert_script_sync_cb;
        }
    }

    nscript = html_process_new_script(c, mimetype, script_type);
    if (nscript == NULL) {
        nsurl_unref(joined);
        content_broadcast_error(&c->base, NSERROR_NOMEM, NULL);
        return DOM_HUBBUB_NOMEM;
    }

    /* set up child fetch encoding and quirks */
    child.charset = c->encoding;
    child.quirks = c->base.quirks;

    /* Increment active fetch count BEFORE hlcache_handle_retrieve.
     * This is critical because the callback can be called synchronously
     * for cached content, which would decrement active before we get
     * a chance to increment it, causing an underflow.
     */
    c->base.active++;
    c->scripts_active++; /* Track script fetches separately */
    NSLOG(neosurf, INFO, "DIAG: exec_src_script: content=%p active=%d scripts_active=%d (pre-retrieve)", c,
        c->base.active, c->scripts_active);

    PERF("SCRIPT FETCH START '%s' type=%s (active=%d)", nsurl_access(joined),
        script_type == HTML_SCRIPT_SYNC ? "SYNC" : (script_type == HTML_SCRIPT_ASYNC ? "ASYNC" : "DEFER"),
        c->base.active);

    ns_error = hlcache_handle_retrieve(
        joined, 0, content_get_url(&c->base), NULL, script_cb, c, &child, CONTENT_SCRIPT, &nscript->data.handle);


    nsurl_unref(joined);

    if (ns_error != NSERROR_OK) {
        /* Fetch failed - decrement the counter we just incremented */
        c->base.active--;
        /* mark duff script fetch as already started */
        nscript->already_started = true;
        NSLOG(neosurf, INFO, "Fetch failed with error %d, active=%d", ns_error, c->base.active);
    } else {
        NSLOG(neosurf, INFO, "%d fetches active", c->base.active);

        switch (script_type) {
        case HTML_SCRIPT_SYNC:
            /* Don't pause parser during script download.
             * This allows parallel resource discovery and download.
             * Script will execute when ready via conversion_begun path.
             * This matches modern browser behavior (speculative parsing).
             */
            break;

        case HTML_SCRIPT_ASYNC:
            break;

        case HTML_SCRIPT_DEFER:
            break;

        default:
            assert(0);
        }
    }

    return ret;
}

static dom_hubbub_error exec_inline_script(html_content *c, dom_node *node, dom_string *mimetype)
{
    dom_string *script;
    dom_exception exc; /* returned by libdom functions */
    struct lwc_string_s *lwcmimetype;
    script_handler_t *script_handler;
    struct html_script *nscript;

    /* does not appear to be a src so script is inline content */
    exc = dom_node_get_text_content(node, &script);
    if ((exc != DOM_NO_ERR) || (script == NULL)) {
        return DOM_HUBBUB_OK; /* no contents, skip */
    }

    nscript = html_process_new_script(c, mimetype, HTML_SCRIPT_INLINE);
    if (nscript == NULL) {
        dom_string_unref(script);

        content_broadcast_error(&c->base, NSERROR_NOMEM, NULL);
        return DOM_HUBBUB_NOMEM;
    }

    nscript->data.string = script;
    nscript->already_started = true;

    /* ensure script handler for content type */
    exc = dom_string_intern(mimetype, &lwcmimetype);
    if (exc != DOM_NO_ERR) {
        NSLOG(neosurf, WARNING, "exec_inline_script: dom_string_intern failed");
        return DOM_HUBBUB_DOM;
    }

    content_type ctype = content_factory_type_from_mime_type(lwcmimetype);
    NSLOG(neosurf, INFO, "exec_inline_script: lwcmimetype='%s' -> content_type=%d (CONTENT_JS=%d)",
        lwc_string_data(lwcmimetype), ctype, CONTENT_JS);

    script_handler = select_script_handler(ctype);
    lwc_string_unref(lwcmimetype);

    NSLOG(neosurf, INFO, "exec_inline_script: script_handler=%p, jsthread=%p", script_handler, c->jsthread);

    if (script_handler != NULL) {
        NSLOG(
            neosurf, INFO, "exec_inline_script: calling script_handler with %zu bytes", dom_string_byte_length(script));
        script_handler(
            c->jsthread, (const uint8_t *)dom_string_data(script), dom_string_byte_length(script), "?inline script?");
    } else {
        NSLOG(neosurf, WARNING, "exec_inline_script: script_handler is NULL, skipping execution");
    }
    return DOM_HUBBUB_OK;
}


/**
 * process script node parser callback
 *
 *
 */
dom_hubbub_error html_process_script(void *ctx, dom_node *node)
{
    html_content *c = (html_content *)ctx;
    dom_exception exc; /* returned by libdom functions */
    dom_string *src, *mimetype;
    dom_hubbub_error err = DOM_HUBBUB_OK;

    /* ensure javascript context is available */
    /* We should only ever be here if scripting was enabled for this
     * content so it's correct to make a javascript context if there
     * isn't one already. */
    if (c->jsthread == NULL) {
        union content_msg_data msg_data;

        msg_data.jsthread = &c->jsthread;
        content_broadcast(&c->base, CONTENT_MSG_GETTHREAD, &msg_data);
        NSLOG(neosurf, INFO, "javascript context %p ", c->jsthread);
        if (c->jsthread == NULL) {
            /* no context and it could not be created, abort */
            return DOM_HUBBUB_OK;
        }
    }

    NSLOG(neosurf, INFO, "html_process_script: content %p parser %p node %p", c, c->parser, node);
    NSLOG(neosurf, DEBUG, "PROFILER: START JS execute %p", node);

    exc = dom_element_get_attribute(node, corestring_dom_type, &mimetype);
    if (exc != DOM_NO_ERR || mimetype == NULL) {
        mimetype = dom_string_ref(corestring_dom_text_javascript);
    }
    NSLOG(neosurf, INFO, "html_process_script: mimetype='%s'", dom_string_data(mimetype));

    exc = dom_element_get_attribute(node, corestring_dom_src, &src);
    if (exc != DOM_NO_ERR || src == NULL) {
        NSLOG(neosurf, INFO, "html_process_script: executing INLINE script");
        err = exec_inline_script(c, node, mimetype);
    } else {
        NSLOG(neosurf, INFO, "html_process_script: fetching EXTERNAL script: %s", dom_string_data(src));
        err = exec_src_script(c, node, mimetype, src);
        dom_string_unref(src);
    }

    dom_string_unref(mimetype);
    NSLOG(neosurf, DEBUG, "PROFILER: STOP JS execute %p", node);

    return err;
}

/* exported internal interface documented in html/html_internal.h */
bool html_saw_insecure_scripts(html_content *htmlc)
{
    struct html_script *s;
    unsigned int i;

    for (i = 0, s = htmlc->scripts; i != htmlc->scripts_count; i++, s++) {
        if (s->type == HTML_SCRIPT_INLINE) {
            /* Inline scripts are no less secure than their
             * containing HTML content
             */
            continue;
        }
        if (s->data.handle == NULL) {
            /* We've not begun loading this? */
            continue;
        }
        if (content_saw_insecure_objects(s->data.handle)) {
            return true;
        }
    }

    return false;
}

/* exported internal interface documented in html/html_internal.h */
nserror html_script_free(html_content *html)
{
    unsigned int i;

    for (i = 0; i != html->scripts_count; i++) {
        if (html->scripts[i].mimetype != NULL) {
            dom_string_unref(html->scripts[i].mimetype);
        }

        switch (html->scripts[i].type) {
        case HTML_SCRIPT_INLINE:
            if (html->scripts[i].data.string != NULL) {
                dom_string_unref(html->scripts[i].data.string);
            }
            break;
        case HTML_SCRIPT_SYNC:
            /* fallthrough */
        case HTML_SCRIPT_ASYNC:
            /* fallthrough */
        case HTML_SCRIPT_DEFER:
            if (html->scripts[i].data.handle != NULL) {
                hlcache_handle_release(html->scripts[i].data.handle);
            }
            break;
        }
    }
    free(html->scripts);

    return NSERROR_OK;
}
