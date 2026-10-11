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

#include <wisp/content.h>
#include <wisp/content/content_protected.h>
#include <wisp/content/fetch.h>
#include <wisp/content/hlcache.h>
#include <wisp/content/llcache.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/utils/config.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/log.h>
#include <wisp/utils/messages.h>
#include "content/content_factory.h"
#include "content/handlers/javascript/js.h"
#include "utils/libdom.h"

#include <wisp/content/handlers/html/html.h>
#include <wisp/content/handlers/html/private.h>

#include <nsutils/time.h>

/* Performance tracing - enable via CMake: -DNEOSURF_ENABLE_PERF_TRACE=ON */
#include <wisp/utils/perf.h>

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

/* Only author sheets already discovered by the parser block classic scripts.
 * Later sheets cannot be discovered while a parser-blocking script waits. */
static bool html_script_styles_blocked(html_content *html)
{
    for (unsigned i = STYLESHEET_START; i < html->stylesheet_count; i++) {
        struct html_stylesheet *sheet = &html->stylesheets[i];
        if (sheet->unused)
            continue;
        /* Before headers arrive, the cache handle has no content yet and
         * content_get_status reports ERROR. It is still a pending fetch;
         * actual failures clear sheet->sheet in the completion callback. */
        if (sheet->modified || (sheet->sheet != NULL && content_get_status(sheet->sheet) != CONTENT_STATUS_DONE))
            return true;
    }
    return false;
}

/* Mark before entering JS: execution can navigate or append script records. */
static void html_script_run(html_content *html, unsigned index)
{
    struct html_script *script = &html->scripts[index];
    const uint8_t *data;
    size_t size;
    nsurl *url;
    script_handler_t *handler;

    script->already_started = true;
    if (html->aborted || html->jsthread == NULL)
        return;
    if (script->type == HTML_SCRIPT_INLINE) {
        lwc_string *mimetype;
        if (dom_string_intern(script->mimetype, &mimetype) != DOM_NO_ERR)
            return;
        handler = select_script_handler(content_factory_type_from_mime_type(mimetype));
        lwc_string_unref(mimetype);
        data = (const uint8_t *)dom_string_data(script->data.string);
        size = dom_string_byte_length(script->data.string);
        url = content_get_url(&html->base);
    } else {
        if (script->data.handle == NULL)
            return; /* Failed fetch: consume the entry and unblock the parser. */
        handler = select_script_handler(content_get_type(script->data.handle));
        data = content_get_source_data(script->data.handle, &size);
        url = hlcache_handle_get_url(script->data.handle);
    }
    if (handler != NULL) {
        nsurl_ref(url);
        handler(html->jsthread, data, size, nsurl_access(url));
        nsurl_unref(url);
    }
}

/* exported internal interface documented in private.h */
nserror html_script_exec(html_content *html, bool allow_defer)
{
    if (html->aborted || html->jsthread == NULL || html->scripts_running)
        return NSERROR_OK;
    html->scripts_running = true;

    /* Fetch callbacks append ready async scripts in completion order. Cached
     * callbacks also wait until the current parser callback has returned. */
    while (html->async_script_head != 0 && !html->aborted && html->jsthread != NULL) {
        unsigned index = html->async_script_head - 1;
        html->async_script_head = html->scripts[index].next_ready;
        if (html->async_script_head == 0)
            html->async_script_tail = 0;
        html_script_run(html, index);
    }

    if (html->parser_blocking_script != 0 && !html->aborted && !html_script_styles_blocked(html)) {
        unsigned index = html->parser_blocking_script - 1;
        if (html->scripts[index].ready_exec) {
            html->parser_blocking_script = 0;
            html_script_run(html, index);
            if (!html->aborted && html->parser != NULL) {
                /* Unpause consumes buffered HTML and may pause at the next
                 * script. Never keep a pointer into the reallocatable array. */
                dom_hubbub_error err = dom_hubbub_parser_pause(html->parser, false);
                if (err != DOM_HUBBUB_OK && err != DOM_HUBBUB_HUBBUB_ERR_PAUSED) {
                    html->scripts_running = false;
                    return libdom_hubbub_error_to_nserror(err);
                }
            }
        }
    }

    if (allow_defer && html->parse_completed && !html_script_styles_blocked(html)) {
        for (unsigned i = 0; i < html->scripts_count && !html->aborted && html->jsthread != NULL; i++) {
            struct html_script *script = &html->scripts[i];
            if (script->type != HTML_SCRIPT_DEFER || script->already_started)
                continue;
            /* A later ready defer must not overtake an earlier download. */
            if (!script->ready_exec)
                break;
            html_script_run(html, i);
        }
    }
    html->scripts_running = false;
    html_document_scripts_ready(html);
    return !html->aborted && html->conversion_begun ? html_proceed_to_done(html) : NSERROR_OK;
}

void script_resume_conversion_cb(void *p)
{
    html_content *html = p;
    if (html->aborted || html->jsthread == NULL)
        return;
    nserror err = html_script_exec(html, html->parse_completed);
    if (err != NSERROR_OK) {
        content_broadcast_error(&html->base, err, NULL);
        content_set_error(&html->base);
        return;
    }
    if (html_can_begin_conversion(html))
        html_begin_conversion(html);
}

void html_script_schedule(html_content *html)
{
    if (!html->aborted && html->jsthread != NULL)
        guit->misc->schedule(0, script_resume_conversion_cb, html);
}

static struct html_script *html_process_new_script(html_content *html, dom_string *mimetype, enum html_script_type type)
{
    struct html_script *scripts = realloc(html->scripts, sizeof(*scripts) * (html->scripts_count + 1));
    if (scripts == NULL)
        return NULL;
    html->scripts = scripts;
    struct html_script *script = &scripts[html->scripts_count++];
    memset(script, 0, sizeof(*script));
    script->type = type;
    script->mimetype = dom_string_ref(mimetype);
    return script;
}

/* Complete fetching once. No JS or parser reentry from a synchronous cache hit. */
static nserror convert_script_cb(hlcache_handle *handle, const hlcache_event *event, void *pw)
{
    html_content *html = pw;
    if (event->type != CONTENT_MSG_DONE && event->type != CONTENT_MSG_ERROR)
        return NSERROR_OK;
    unsigned index;
    for (index = 0; index < html->scripts_count; index++) {
        if (html->scripts[index].type != HTML_SCRIPT_INLINE && html->scripts[index].data.handle == handle)
            break;
    }
    assert(index < html->scripts_count);
    struct html_script *script = &html->scripts[index];
    if (script->ready_exec)
        return NSERROR_OK;
    script->ready_exec = true;
    assert(html->base.active > 0 && html->scripts_active > 0);
    html->base.active--;
    html->scripts_active--;
    unsigned status = event->type == CONTENT_MSG_DONE
        ? llcache_handle_get_http_status(hlcache_handle_get_content(handle)->llcache)
        : 0;
    if (event->type == CONTENT_MSG_ERROR || (status != 0 && (status < 200 || status > 299))) {
        NSLOG(wisp, INFO, "Script fetch failed (HTTP status %u)", status);
        hlcache_handle_release(handle);
        script->data.handle = NULL;
    }
    if (script->type == HTML_SCRIPT_ASYNC) {
        if (html->async_script_tail != 0)
            html->scripts[html->async_script_tail - 1].next_ready = index + 1;
        else
            html->async_script_head = index + 1;
        html->async_script_tail = index + 1;
    }
    html_script_schedule(html);
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
    dom_hubbub_error ret = DOM_HUBBUB_OK;
    dom_exception exc; /* returned by libdom functions */

    /* src url */
    ns_error = nsurl_join(c->base_url, dom_string_data(src), &joined);
    if (ns_error != NSERROR_OK) {
        content_broadcast_error(&c->base, NSERROR_NOMEM, NULL);
        return DOM_HUBBUB_NOMEM;
    }

    NSLOG(wisp, INFO, "script %i '%s'", c->scripts_count, nsurl_access(joined));

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

    /* async/defer are HTML boolean attributes: presence means true. */
    exc = dom_element_has_attribute(node, corestring_dom_async, &async);
    if (exc != DOM_NO_ERR) {
        nsurl_unref(joined);
        return DOM_HUBBUB_OK;
    }

    if (c->parse_completed) {
        /* After parse completed, all scripts are essentially async */
        async = true;
        defer = false;
    }

    if (async) {
        /* asyncronous script */
        script_type = HTML_SCRIPT_ASYNC;

    } else {
        exc = dom_element_has_attribute(node, corestring_dom_defer, &defer);
        if (exc != DOM_NO_ERR) {
            nsurl_unref(joined);
            return DOM_HUBBUB_OK;
        }

        if (defer) {
            /* defered script */
            script_type = HTML_SCRIPT_DEFER;
        } else {
            /* syncronous script */
            script_type = HTML_SCRIPT_SYNC;
        }
    }

    nscript = html_process_new_script(c, mimetype, script_type);
    if (nscript == NULL) {
        nsurl_unref(joined);
        content_broadcast_error(&c->base, NSERROR_NOMEM, NULL);
        return DOM_HUBBUB_NOMEM;
    }

    if (script_type == HTML_SCRIPT_SYNC)
        c->parser_blocking_script = c->scripts_count;

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
    NSLOG(wisp, INFO, "DIAG: exec_src_script: content=%p active=%d scripts_active=%d (pre-retrieve)", c, c->base.active,
        c->scripts_active);

    PERF("SCRIPT FETCH START '%s' type=%s (active=%d)", nsurl_access(joined),
        script_type == HTML_SCRIPT_SYNC ? "SYNC" : (script_type == HTML_SCRIPT_ASYNC ? "ASYNC" : "DEFER"),
        c->base.active);

    ns_error = hlcache_handle_retrieve(joined, 0, content_get_url(&c->base), NULL, convert_script_cb, c, &child,
        CONTENT_SCRIPT, &nscript->data.handle);


    nsurl_unref(joined);

    if (ns_error != NSERROR_OK) {
        /* Fetch failed - decrement the counter we just incremented */
        c->base.active--;
        c->scripts_active--;
        if (script_type == HTML_SCRIPT_SYNC)
            c->parser_blocking_script = 0;
        nscript->ready_exec = true;
        /* mark duff script fetch as already started */
        nscript->already_started = true;
        NSLOG(wisp, INFO, "Fetch failed with error %d, active=%d", ns_error, c->base.active);
    } else {
        NSLOG(wisp, INFO, "%d fetches active", c->base.active);

        if (script_type == HTML_SCRIPT_SYNC) {
            /* Pause the actual DOM parser; speculative resource discovery is
             * a separate feature and cannot execute subsequent scripts. */
            ret = DOM_HUBBUB_HUBBUB_ERR_PAUSED;
        }
    }

    return ret;
}

static dom_hubbub_error exec_inline_script(html_content *c, dom_node *node, dom_string *mimetype)
{
    dom_string *script;
    dom_exception exc; /* returned by libdom functions */
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
    nscript->ready_exec = true;
    if (html_script_styles_blocked(c)) {
        c->parser_blocking_script = c->scripts_count;
        /* Returning PAUSED stops the current tokeniser pass. Merely setting
         * its pause option affects entry to the next pass, not this callback. */
        return DOM_HUBBUB_HUBBUB_ERR_PAUSED;
    }
    html_script_run(c, c->scripts_count - 1);
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
    dom_string *src = NULL, *mimetype = NULL;
    dom_hubbub_error err = DOM_HUBBUB_OK;

    /* ensure javascript context is available */
    /* We should only ever be here if scripting was enabled for this
     * content so it's correct to make a javascript context if there
     * isn't one already. */
    if (c->jsthread == NULL) {
        union content_msg_data msg_data;

        msg_data.jsthread = &c->jsthread;
        content_broadcast(&c->base, CONTENT_MSG_GETTHREAD, &msg_data);
        NSLOG(wisp, INFO, "javascript context %p ", c->jsthread);
        if (c->jsthread == NULL) {
            /* no context and it could not be created, abort */
            return DOM_HUBBUB_OK;
        }
    }

    NSLOG(wisp, INFO, "html_process_script: content %p parser %p node %p", c, c->parser, node);
    NSLOG(wisp, DEBUG, "PROFILER: START JS execute %p", node);

    exc = dom_element_get_attribute(node, corestring_dom_type, &mimetype);
    if (exc != DOM_NO_ERR || mimetype == NULL || dom_string_byte_length(mimetype) == 0) {
        if (mimetype != NULL)
            dom_string_unref(mimetype);
        mimetype = dom_string_ref(corestring_dom_text_javascript);
    }
    /* Modules and data blocks must never fall through to the classic-script
     * fetch path, even if their resource has a JavaScript Content-Type. */
    lwc_string *type;
    if (dom_string_intern(mimetype, &type) != DOM_NO_ERR) {
        dom_string_unref(mimetype);
        return DOM_HUBBUB_NOMEM;
    }
    bool classic = select_script_handler(content_factory_type_from_mime_type(type)) != NULL;
    lwc_string_unref(type);
    if (!classic) {
        dom_string_unref(mimetype);
        return DOM_HUBBUB_OK;
    }
    NSLOG(wisp, INFO, "html_process_script: mimetype='%s'", dom_string_data(mimetype));

    exc = dom_element_get_attribute(node, corestring_dom_src, &src);
    if (exc != DOM_NO_ERR || src == NULL) {
        NSLOG(wisp, INFO, "html_process_script: executing INLINE script");
        err = exec_inline_script(c, node, mimetype);
    } else {
        NSLOG(wisp, INFO, "html_process_script: fetching EXTERNAL script: %s", dom_string_data(src));
        err = exec_src_script(c, node, mimetype, src);
        dom_string_unref(src);
    }

    dom_string_unref(mimetype);
    NSLOG(wisp, DEBUG, "PROFILER: STOP JS execute %p", node);

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
