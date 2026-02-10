/*
 * Copyright 2013 Vincent Sanders <vince@netsurf-browser.org>
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
 * Processing for html content css operations.
 */

#include <wisp/utils/config.h>

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wisp/content.h>
#include <wisp/content/handlers/css/css.h>
#include <wisp/content/hlcache.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/log.h>
#include <wisp/utils/nsoption.h>

#include <wisp/content/handlers/html/html.h>
#include <wisp/content/handlers/html/private.h>
#include "content/handlers/html/css.h"

#include <nsutils/time.h>

/* Performance tracing - enable via CMake: -DNEOSURF_ENABLE_PERF_TRACE=ON */
#include <wisp/utils/perf.h>

static nsurl *html_default_stylesheet_url;
static nsurl *html_adblock_stylesheet_url;
static nsurl *html_quirks_stylesheet_url;
static nsurl *html_user_stylesheet_url;

/**
 * Convert css error to netsurf error.
 */
static nserror css_error_to_nserror(css_error error)
{
    switch (error) {
    case CSS_OK:
        return NSERROR_OK;

    case CSS_NOMEM:
        return NSERROR_NOMEM;

    case CSS_BADPARM:
        return NSERROR_BAD_PARAMETER;

    case CSS_INVALID:
        return NSERROR_INVALID;

    case CSS_FILENOTFOUND:
        return NSERROR_NOT_FOUND;

    case CSS_NEEDDATA:
        return NSERROR_NEED_DATA;

    case CSS_BADCHARSET:
        return NSERROR_BAD_ENCODING;

    case CSS_EOF:
    case CSS_IMPORTS_PENDING:
    case CSS_PROPERTY_NOT_SET:
    default:
        break;
    }
    return NSERROR_CSS;
}


/**
 * Callback for fetchcache() for stylesheets.
 */
static nserror html_convert_css_callback(hlcache_handle *css, const hlcache_event *event, void *pw)
{
    html_content *parent = pw;
    unsigned int i;
    struct html_stylesheet *s;

    /* Find sheet */
    for (i = 0, s = parent->stylesheets; i != parent->stylesheet_count; i++, s++) {
        if (s->sheet == css)
            break;
    }

    assert(i != parent->stylesheet_count);

    switch (event->type) {

    case CONTENT_MSG_DONE:
        PERF(
            "CSS DONE slot %d '%s' (active=%d)", i, nsurl_access(hlcache_handle_get_url(css)), parent->base.active - 1);
        NSLOG(wisp, INFO, "done stylesheet slot %d '%s'", i, nsurl_access(hlcache_handle_get_url(css)));
        CONTENT_ACTIVE_DEC(parent, "CSS callback DONE");
        break;

    case CONTENT_MSG_ERROR: {
        const char *u = nsurl_access(hlcache_handle_get_url(css));
        /* user.css and adblock.css are optional - log at INFO */
        if (u != NULL && (strcmp(u, "resource:user.css") == 0 || strcmp(u, "resource:adblock.css") == 0)) {
            NSLOG(wisp, INFO, "Optional stylesheet %s not found (this is normal)", u);
        } else if (u != NULL && strcmp(u, "resource:default.css") == 0) {
            /* default.css is critical - browser will look broken
             * without it */
            NSLOG(wisp, ERROR, "CRITICAL: default.css failed to load: %s (code %d) - pages will be unstyled!",
                event->data.errordata.errormsg, event->data.errordata.errorcode);
        } else {
            NSLOG(wisp, ERROR, "Stylesheet %s failed: %s (code %d)", u ? u : "(unknown)",
                event->data.errordata.errormsg, event->data.errordata.errorcode);
        }

        hlcache_handle_release(css);
        s->sheet = NULL;
        CONTENT_ACTIVE_DEC(parent, "CSS callback ERROR");
        break;
    }

    case CONTENT_MSG_POINTER:
        /* Really don't want this to continue after the switch */
        return NSERROR_OK;

    default:
        break;
    }

    if (html_can_begin_conversion(parent)) {
        html_begin_conversion(parent);
    }

    return NSERROR_OK;
}


static nserror html_stylesheet_from_domnode(html_content *c, dom_node *node, hlcache_handle **sheet)
{
    hlcache_child_context child;
    dom_string *style;
    nsurl *url;
    dom_exception exc;
    nserror error;
    uint32_t key;
    char urlbuf[64];

    child.charset = c->encoding;
    child.quirks = c->base.quirks;

    exc = dom_node_get_text_content(node, &style);
    if ((exc != DOM_NO_ERR) || (style == NULL)) {
        NSLOG(wisp, INFO, "No text content");
        return NSERROR_OK;
    }

    error = html_css_fetcher_add_item(style, c->base_url, &key);
    if (error != NSERROR_OK) {
        dom_string_unref(style);
        return error;
    }

    dom_string_unref(style);

    snprintf(urlbuf, sizeof(urlbuf), "x-ns-css:%" PRIu32 "", key);

    error = nsurl_create(urlbuf, &url);
    if (error != NSERROR_OK) {
        return error;
    }

    CONTENT_ACTIVE_INC(c, "inline CSS fetch start");

    error = hlcache_handle_retrieve(
        url, 0, content_get_url(&c->base), NULL, html_convert_css_callback, c, &child, CONTENT_CSS, sheet);
    if (error != NSERROR_OK) {
        CONTENT_ACTIVE_DEC(c, "inline CSS fetch error");
        nsurl_unref(url);
        return error;
    }

    nsurl_unref(url);

    return NSERROR_OK;
}


/**
 * Process an inline stylesheet in the document.
 *
 * \param  c      content structure
 * \param  style  xml node of style element
 * \return  true on success, false if an error occurred
 */
static struct html_stylesheet *html_create_style_element(html_content *c, dom_node *style)
{
    dom_string *val;
    dom_exception exc;
    struct html_stylesheet *stylesheets;

    /* type='text/css', or not present (invalid but common) */
    exc = dom_element_get_attribute(style, corestring_dom_type, &val);
    if (exc == DOM_NO_ERR && val != NULL) {
        if (!dom_string_caseless_lwc_isequal(val, corestring_lwc_text_css)) {
            dom_string_unref(val);
            return NULL;
        }
        dom_string_unref(val);
    }

    /* media contains 'screen' or 'all' or not present */
    exc = dom_element_get_attribute(style, corestring_dom_media, &val);
    if (exc == DOM_NO_ERR && val != NULL) {
        if (strcasestr(dom_string_data(val), "screen") == NULL && strcasestr(dom_string_data(val), "all") == NULL) {
            dom_string_unref(val);
            return NULL;
        }
        dom_string_unref(val);
    }

    /* Extend array */
    stylesheets = realloc(c->stylesheets, sizeof(struct html_stylesheet) * (c->stylesheet_count + 1));
    if (stylesheets == NULL) {

        content_broadcast_error(&c->base, NSERROR_NOMEM, NULL);
        return false;
    }
    c->stylesheets = stylesheets;

    c->stylesheets[c->stylesheet_count].node = dom_node_ref(style);
    c->stylesheets[c->stylesheet_count].sheet = NULL;
    c->stylesheets[c->stylesheet_count].modified = false;
    c->stylesheets[c->stylesheet_count].unused = false;
    c->stylesheet_count++;

    return c->stylesheets + (c->stylesheet_count - 1);
}


static bool html_css_process_modified_style(html_content *c, struct html_stylesheet *s)
{
    hlcache_handle *sheet = NULL;
    nserror error;

    error = html_stylesheet_from_domnode(c, s->node, &sheet);
    if (error != NSERROR_OK) {
        NSLOG(wisp, INFO, "Failed to update sheet");
        content_broadcast_error(&c->base, error, NULL);
        return false;
    }

    if (sheet != NULL) {
        NSLOG(wisp, INFO, "Updating sheet %p with %p", s->sheet, sheet);

        if (s->sheet != NULL) {
            switch (content_get_status(s->sheet)) {
            case CONTENT_STATUS_DONE:
                break;
            default:
                hlcache_handle_abort(s->sheet);
                CONTENT_ACTIVE_DEC(c, "aborting modified style");
            }
            hlcache_handle_release(s->sheet);
        }
        s->sheet = sheet;
    }

    s->modified = false;

    return true;
}


/**
 * process a stylesheet that has been modified.
 */
static void html_css_process_modified_styles(void *pw)
{
    html_content *c = pw;
    struct html_stylesheet *s;
    unsigned int i;
    bool all_done = true;

    for (i = 0, s = c->stylesheets; i != c->stylesheet_count; i++, s++) {
        if (c->stylesheets[i].modified) {
            all_done &= html_css_process_modified_style(c, s);
        }
    }

    /* If we failed to process any sheet, schedule a retry */
    if (all_done == false) {
        guit->misc->schedule(1000, html_css_process_modified_styles, c);
    }
}


/* exported function documented in html/css.h */
bool html_css_update_style(html_content *c, dom_node *style)
{
    unsigned int i;
    struct html_stylesheet *s;

    /* Find sheet */
    for (i = 0, s = c->stylesheets; i != c->stylesheet_count; i++, s++) {
        if (s->node == style)
            break;
    }
    if (i == c->stylesheet_count) {
        s = html_create_style_element(c, style);
    }
    if (s == NULL) {
        NSLOG(wisp, INFO, "Could not find or create inline stylesheet for %p", style);
        return false;
    }

    s->modified = true;

    guit->misc->schedule(0, html_css_process_modified_styles, c);

    return true;
}


/* exported function documented in html/css.h */
bool html_css_process_style(html_content *c, dom_node *node)
{
    unsigned int i;
    dom_string *val;
    dom_exception exc;
    struct html_stylesheet *s;

    /* Find sheet */
    for (i = 0, s = c->stylesheets; i != c->stylesheet_count; i++, s++) {
        if (s->node == node)
            break;
    }

    /* Should already exist */
    if (i == c->stylesheet_count) {
        return false;
    }

    exc = dom_element_get_attribute(node, corestring_dom_media, &val);
    if (exc == DOM_NO_ERR && val != NULL) {
        if (strcasestr(dom_string_data(val), "screen") == NULL && strcasestr(dom_string_data(val), "all") == NULL) {
            s->unused = true;
        }
        dom_string_unref(val);
    }

    return true;
}


/* exported function documented in html/css.h */
bool html_css_process_link(html_content *htmlc, dom_node *node)
{
    dom_string *rel, *type_attr, *media, *href;
    struct html_stylesheet *stylesheets;
    nsurl *joined;
    dom_exception exc;
    nserror ns_error;
    hlcache_child_context child;

    /* rel=<space separated list, including 'stylesheet'> */
    exc = dom_element_get_attribute(node, corestring_dom_rel, &rel);
    if (exc != DOM_NO_ERR || rel == NULL)
        return true;

    if (strcasestr(dom_string_data(rel), "stylesheet") == NULL) {
        NSLOG(wisp, INFO, "DEBUG html_css_process_link: Rejected - rel='%s' does not contain 'stylesheet'",
            dom_string_data(rel));
        dom_string_unref(rel);
        return true;
    } else if (strcasestr(dom_string_data(rel), "alternate") != NULL) {
        /* Ignore alternate stylesheets */
        NSLOG(
            wisp, INFO, "DEBUG html_css_process_link: Rejected - rel='%s' contains 'alternate'", dom_string_data(rel));
        dom_string_unref(rel);
        return true;
    }
    dom_string_unref(rel);

    if (nsoption_bool(author_level_css) == false) {
        NSLOG(wisp, INFO, "DEBUG html_css_process_link: Rejected - author_level_css is false");
        return true;
    }

    /* type='text/css' or not present */
    exc = dom_element_get_attribute(node, corestring_dom_type, &type_attr);
    if (exc == DOM_NO_ERR && type_attr != NULL) {
        if (!dom_string_caseless_lwc_isequal(type_attr, corestring_lwc_text_css)) {
            NSLOG(wisp, INFO, "DEBUG html_css_process_link: Rejected - type='%s' is not text/css",
                dom_string_data(type_attr));
            dom_string_unref(type_attr);
            return true;
        }
        dom_string_unref(type_attr);
    }

    /* media contains 'screen' or 'all' or not present or empty string
     * Note: empty media attribute is valid and equivalent to "all" per HTML spec */
    exc = dom_element_get_attribute(node, corestring_dom_media, &media);
    if (exc == DOM_NO_ERR && media != NULL) {
        if (dom_string_length(media) > 0 && strcasestr(dom_string_data(media), "screen") == NULL &&
            strcasestr(dom_string_data(media), "all") == NULL) {
            NSLOG(wisp, INFO, "DEBUG html_css_process_link: Rejected - media='%s' doesn't contain screen/all",
                dom_string_data(media));
            dom_string_unref(media);
            return true;
        }
        dom_string_unref(media);
    }

    /* href='...' */
    exc = dom_element_get_attribute(node, corestring_dom_href, &href);
    if (exc != DOM_NO_ERR || href == NULL)
        return true;

    /* TODO: only the first preferred stylesheets (ie.
     * those with a title attribute) should be loaded
     * (see HTML4 14.3) */

    ns_error = nsurl_join(htmlc->base_url, dom_string_data(href), &joined);
    if (ns_error != NSERROR_OK) {
        dom_string_unref(href);
        NSLOG(wisp, ERROR, "nsurl_join failed (err: %d) - jumping to no_memory", ns_error);
        goto no_memory;
    }
    dom_string_unref(href);

    NSLOG(wisp, INFO, "linked stylesheet %i '%s'", htmlc->stylesheet_count, nsurl_access(joined));

    /* extend stylesheets array to allow for new sheet */
    stylesheets = realloc(htmlc->stylesheets, sizeof(struct html_stylesheet) * (htmlc->stylesheet_count + 1));
    if (stylesheets == NULL) {
        nsurl_unref(joined);
        ns_error = NSERROR_NOMEM;
        NSLOG(wisp, ERROR, "realloc stylesheets failed - jumping to no_memory");
        goto no_memory;
    }

    htmlc->stylesheets = stylesheets;
    htmlc->stylesheets[htmlc->stylesheet_count].node = NULL;
    htmlc->stylesheets[htmlc->stylesheet_count].modified = false;
    htmlc->stylesheets[htmlc->stylesheet_count].unused = false;

    /* start fetch - increment count BEFORE retrieve to prevent race with sync callback */
    htmlc->stylesheet_count++;

    child.charset = htmlc->encoding;
    child.quirks = htmlc->base.quirks;

    CONTENT_ACTIVE_INC(htmlc, "linked CSS fetch start");
    PERF("CSS FETCH START '%s' (active=%d)", nsurl_access(joined), htmlc->base.active);
    ns_error = hlcache_handle_retrieve(joined, 0, content_get_url(&htmlc->base), NULL, html_convert_css_callback, htmlc,
        &child, CONTENT_CSS, &htmlc->stylesheets[htmlc->stylesheet_count - 1].sheet);

    nsurl_unref(joined);

    if (ns_error != NSERROR_OK) {
        /* Retrieval failed, decrement count to clean up */
        htmlc->stylesheet_count--;
        CONTENT_ACTIVE_DEC(htmlc, "linked CSS fetch error");
        NSLOG(wisp, ERROR, "hlcache_handle_retrieve failed (err: %d)", ns_error);
        goto no_memory;
    }

    /* active count already logged by CONTENT_ACTIVE_INC */

    return true;

no_memory:
    content_broadcast_error(&htmlc->base, ns_error, NULL);
    return false;
}


/* exported interface documented in html/html.h */
struct html_stylesheet *html_get_stylesheets(hlcache_handle *h, unsigned int *n)
{
    html_content *c = (html_content *)hlcache_handle_get_content(h);

    assert(c != NULL);
    assert(n != NULL);

    *n = c->stylesheet_count;

    return c->stylesheets;
}


/* exported function documented in html/css.h */
bool html_css_saw_insecure_stylesheets(html_content *html)
{
    struct html_stylesheet *s;
    unsigned int i;

    for (i = 0, s = html->stylesheets; i < html->stylesheet_count; i++, s++) {
        if (s->sheet != NULL) {
            if (content_saw_insecure_objects(s->sheet)) {
                return true;
            }
        }
    }

    return false;
}


/* exported function documented in html/css.h */
nserror html_css_free_stylesheets(html_content *html)
{
    unsigned int i;

    guit->misc->schedule(-1, html_css_process_modified_styles, html);

    for (i = 0; i != html->stylesheet_count; i++) {
        if (html->stylesheets[i].sheet != NULL) {
            hlcache_handle_release(html->stylesheets[i].sheet);
        }
        if (html->stylesheets[i].node != NULL) {
            dom_node_unref(html->stylesheets[i].node);
        }
    }
    free(html->stylesheets);

    return NSERROR_OK;
}


/* exported function documented in html/css.h */
nserror html_css_quirks_stylesheets(html_content *c)
{
    nserror ns_error = NSERROR_OK;
    hlcache_child_context child;

    assert(c->stylesheets != NULL);

    if (c->quirks == DOM_DOCUMENT_QUIRKS_MODE_FULL) {
        child.charset = c->encoding;
        child.quirks = c->base.quirks;

        CONTENT_ACTIVE_INC(c, "quirks CSS fetch start");
        ns_error = hlcache_handle_retrieve(html_quirks_stylesheet_url, 0, content_get_url(&c->base), NULL,
            html_convert_css_callback, c, &child, CONTENT_CSS, &c->stylesheets[STYLESHEET_QUIRKS].sheet);
        if (ns_error != NSERROR_OK) {
            return ns_error;
        }


        /* active count already logged by CONTENT_ACTIVE_INC */
    }

    return ns_error;
}


/* exported function documented in html/css.h */
nserror html_css_new_stylesheets(html_content *c)
{
    nserror ns_error;
    hlcache_child_context child;

    if (c->stylesheets != NULL) {
        return NSERROR_OK; /* already initialised */
    }

    /* stylesheet 0 is the base style sheet,
     * stylesheet 1 is the quirks mode style sheet,
     * stylesheet 2 is the adblocking stylesheet,
     * stylesheet 3 is the user stylesheet */
    c->stylesheets = calloc(STYLESHEET_START, sizeof(struct html_stylesheet));
    if (c->stylesheets == NULL) {
        return NSERROR_NOMEM;
    }

    c->stylesheets[STYLESHEET_BASE].sheet = NULL;
    c->stylesheets[STYLESHEET_QUIRKS].sheet = NULL;
    c->stylesheets[STYLESHEET_ADBLOCK].sheet = NULL;
    c->stylesheets[STYLESHEET_USER].sheet = NULL;
    c->stylesheet_count = STYLESHEET_START;

    child.charset = c->encoding;
    child.quirks = c->base.quirks;

    CONTENT_ACTIVE_INC(c, "default.css fetch start");
    ns_error = hlcache_handle_retrieve(html_default_stylesheet_url, 0, content_get_url(&c->base), NULL,
        html_convert_css_callback, c, &child, CONTENT_CSS, &c->stylesheets[STYLESHEET_BASE].sheet);
    if (ns_error != NSERROR_OK) {
        CONTENT_ACTIVE_DEC(c, "default.css fetch error");
        return ns_error;
    }


    /* active count already logged by CONTENT_ACTIVE_INC */


    if (nsoption_bool(block_advertisements)) {
        CONTENT_ACTIVE_INC(c, "adblock.css fetch start");
        ns_error = hlcache_handle_retrieve(html_adblock_stylesheet_url, 0, content_get_url(&c->base), NULL,
            html_convert_css_callback, c, &child, CONTENT_CSS, &c->stylesheets[STYLESHEET_ADBLOCK].sheet);
        if (ns_error != NSERROR_OK) {
            CONTENT_ACTIVE_DEC(c, "adblock.css fetch error");
            return ns_error;
        }


        /* active count already logged by CONTENT_ACTIVE_INC */
    }

    CONTENT_ACTIVE_INC(c, "user.css fetch start");
    ns_error = hlcache_handle_retrieve(html_user_stylesheet_url, 0, content_get_url(&c->base), NULL,
        html_convert_css_callback, c, &child, CONTENT_CSS, &c->stylesheets[STYLESHEET_USER].sheet);
    if (ns_error != NSERROR_OK) {
        CONTENT_ACTIVE_DEC(c, "user.css fetch error");
        return ns_error;
    }


    /* active count already logged by CONTENT_ACTIVE_INC */

    return ns_error;
}


/* exported function documented in html/css.h */
nserror html_css_new_selection_context(html_content *c, css_select_ctx **ret_select_ctx)
{
    uint32_t i;
    css_error css_ret;
    css_select_ctx *select_ctx;

    /* check that the base stylesheet loaded; layout fails without it */
    if (c->stylesheets[STYLESHEET_BASE].sheet == NULL) {
        return NSERROR_CSS_BASE;
    }

    /* Create selection context */
    css_ret = css_select_ctx_create(&select_ctx);
    if (css_ret != CSS_OK) {
        return css_error_to_nserror(css_ret);
    }

    /* Add sheets to it */
    for (i = STYLESHEET_BASE; i != c->stylesheet_count; i++) {
        const struct html_stylesheet *hsheet = &c->stylesheets[i];
        css_stylesheet *sheet = NULL;
        css_origin origin = CSS_ORIGIN_AUTHOR;

        /* Filter out stylesheets for non-screen media. */
        /* TODO: We should probably pass the sheet in anyway, and let
         *       libcss handle the filtering.
         */
        if (hsheet->unused) {
            continue;
        }

        if (i < STYLESHEET_USER) {
            origin = CSS_ORIGIN_UA;
        } else if (i < STYLESHEET_START) {
            origin = CSS_ORIGIN_USER;
        }

        if (hsheet->sheet != NULL) {
            sheet = nscss_get_stylesheet(hsheet->sheet);
        }

        if (sheet != NULL) {
            /* TODO: Pass the sheet's full media query, instead of
             *       "screen".
             */
            css_ret = css_select_ctx_append_sheet(select_ctx, sheet, origin, "screen");
            if (css_ret != CSS_OK) {
                css_select_ctx_destroy(select_ctx);
                return css_error_to_nserror(css_ret);
            }
        }
    }

    /* return new selection context to caller */
    *ret_select_ctx = select_ctx;
    return NSERROR_OK;
}


/* exported function documented in html/css.h */
nserror html_css_init(void)
{
    nserror error;

    error = html_css_fetcher_register();
    if (error != NSERROR_OK)
        return error;

    error = nsurl_create("resource:default.css", &html_default_stylesheet_url);
    if (error != NSERROR_OK)
        return error;

    error = nsurl_create("resource:adblock.css", &html_adblock_stylesheet_url);
    if (error != NSERROR_OK)
        return error;

    error = nsurl_create("resource:quirks.css", &html_quirks_stylesheet_url);
    if (error != NSERROR_OK)
        return error;

    error = nsurl_create("resource:user.css", &html_user_stylesheet_url);

    return error;
}


/* exported function documented in html/css.h */
void html_css_fini(void)
{
    if (html_user_stylesheet_url != NULL) {
        nsurl_unref(html_user_stylesheet_url);
        html_user_stylesheet_url = NULL;
    }

    if (html_quirks_stylesheet_url != NULL) {
        nsurl_unref(html_quirks_stylesheet_url);
        html_quirks_stylesheet_url = NULL;
    }

    if (html_adblock_stylesheet_url != NULL) {
        nsurl_unref(html_adblock_stylesheet_url);
        html_adblock_stylesheet_url = NULL;
    }

    if (html_default_stylesheet_url != NULL) {
        nsurl_unref(html_default_stylesheet_url);
        html_default_stylesheet_url = NULL;
    }
}
