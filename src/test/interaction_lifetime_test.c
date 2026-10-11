/* Real mouse dispatch, DOM listeners, content users and HTML close/open.
 * Only the cache/network/frontend boundaries are controlled by this fixture. */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <libdom/bindings/hubbub/parser.h>
#include <wisp/browser_window.h>
#include <wisp/content/hlcache.h>
#include <wisp/content/fetch.h>
#include <wisp/content/llcache.h>
#include <wisp/content/handlers/html/html.h>
#include <wisp/content/handlers/html/interaction.h>
#include <wisp/content/handlers/html/private.h>
#include <wisp/content/handlers/html/form_internal.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/misc.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsoption.h>
#include "content/content_factory.h"
#include "content/handlers/html/box_manipulate.h"
#include "content/handlers/html/css.h"
#include "content/handlers/html/hyperlink.h"
#include "content/handlers/html/imagemap.h"
#include "desktop/browser_private.h"
#include "desktop/selection.h"
#include "desktop/scrollbar.h"
#include "utils/talloc.h"

static nserror test_mouse_action(struct content *c, struct browser_window *bw, browser_mouse_state state, int x, int y)
{
    return html_mouse_action(c, bw, &(struct browser_mouse_event){.state = state, .x = x, .y = y, .detail = 1});
}

static const content_handler *handler;
static html_content *html;
static dom_node *node;
static dom_event_listener *listener;
static struct browser_window browser;
static int user_identity;
static bool browser_user, cancel_click, close_in_navigation, close_on_status, reopen, nest, nested;
static unsigned clicks, navigations, schedules, pointer_messages, expected_users;
static struct content child;
static unsigned child_clicks;
static bool close_in_child;
static const char *expected_url, *expected_target, *expected_script;
static enum browser_window_nav_flags expected_flags;
static unsigned scripts, saved_links;
static bool close_in_target;
static const char *expected_post;
static bool expected_multipart;
static void (*edit_on_click)(void);

static struct browser_window *window(void)
{
    return &browser;
}

/* Obtain the production handler, without registering/fetching stylesheets. */
nserror html_css_init(void)
{
    return NSERROR_OK;
}

nserror content_factory_register_handler(const char *mime, const content_handler *candidate)
{
    handler = candidate;
    return NSERROR_OK;
}

nsurl *llcache_handle_get_url(const llcache_handle *handle)
{
    return html->base_url;
}

static nserror schedule(int delay, void (*callback)(void *), void *pw)
{
    if (delay >= 0)
        schedules++;
    return NSERROR_OK;
}

static void browser_message(struct content *c, content_msg msg, const union content_msg_data *data, void *pw);

static void retire_document(void)
{
    uint64_t generation = html->input_generation;
    ck_assert_int_eq(handler->close(&html->base), NSERROR_OK);
    ck_assert_uint_eq(html->input_generation, generation + 1);
    ck_assert_ptr_null(html->bw);
    if (browser_user) {
        content_remove_user(&html->base, browser_message, &user_identity);
        browser_user = false;
    }
    /* This is the cache's eviction criterion, including forced cleanup. Only
     * in-flight clicks still own the content after its browser handle leaves. */
    ck_assert_uint_eq(content_count_users(&html->base), expected_users - 1);
    ck_assert_msg(content_count_users(&html->base) != 0, "Callback made active content evictable");
    talloc_free(html->bctx);
    html->bctx = NULL;
    html->layout = NULL;
    if (reopen) {
        ck_assert(content_add_user(&html->base, browser_message, &user_identity));
        browser_user = true;
        ck_assert_int_eq(handler->open(&html->base, window(), NULL, NULL), NSERROR_OK);
    }
}

static void browser_message(struct content *c, content_msg msg, const union content_msg_data *data, void *pw)
{
    if (msg == CONTENT_MSG_SAVELINK) {
        ck_assert_str_eq(nsurl_access(data->savelink.url), expected_url);
        saved_links++;
    }
    if (msg == CONTENT_MSG_STATUS || msg == CONTENT_MSG_POINTER)
        ck_assert_uint_eq(content_count_users(c), expected_users);
    if (msg == CONTENT_MSG_STATUS && close_on_status)
        retire_document();
    if (msg == CONTENT_MSG_POINTER)
        pointer_messages++;
}

/* A controlled child content exercises the real content/HTML handoff. The
 * child can release the parent's browser handle before dispatch returns. */
struct content *hlcache_handle_get_content(const hlcache_handle *handle)
{
    ck_assert_ptr_eq(handle, (hlcache_handle *)&child);
    return &child;
}

static nserror child_mouse_action(struct content *c, struct browser_window *bw, const struct browser_mouse_event *event)
{
    ck_assert_ptr_eq(c, &child);
    ck_assert_uint_eq(content_count_users(&html->base), expected_users);
    child_clicks++;
    if (close_in_child)
        retire_document();
    return NSERROR_OK;
}

struct browser_window *
browser_window_find_target(struct browser_window *bw, const char *target, browser_mouse_state mouse)
{
    ck_assert_ptr_eq(bw, window());
    ck_assert_str_eq(target, expected_target);
    if (close_in_target) {
        retire_document();
        ck_assert_str_eq(target, expected_target); /* Must survive retiring boxes. */
    }
    return bw;
}

nserror browser_window_navigate(struct browser_window *bw, nsurl *url, nsurl *referrer,
    enum browser_window_nav_flags flags, char *post, struct fetch_multipart_data *multipart,
    struct hlcache_handle *parent)
{
    ck_assert_ptr_eq(bw, window());
    ck_assert_str_eq(nsurl_access(url), expected_url);
    ck_assert_int_eq(flags, expected_flags);
    ck_assert_str_eq(nsurl_access(referrer), "https://example.test/");
    ck_assert_uint_eq(content_count_users(&html->base), expected_users);
    if (expected_post != NULL) {
        ck_assert_ptr_nonnull(post);
        ck_assert_str_eq(post, expected_post);
    } else {
        ck_assert_ptr_null(post);
    }
    if (expected_multipart) {
        ck_assert_ptr_nonnull(multipart);
        ck_assert_str_eq(fetch_multipart_data_find(multipart, "q"), "old");
        ck_assert_str_eq(fetch_multipart_data_find(multipart, "go"), "yes");
        ck_assert_ptr_null(fetch_multipart_data_find(multipart, "other"));
    } else {
        ck_assert_ptr_null(multipart);
    }
    navigations++;
    if (close_in_navigation)
        retire_document();
    ck_assert_str_eq(nsurl_access(url), expected_url);
    if (expected_post != NULL)
        ck_assert_str_eq(post, expected_post); /* Owned across frontend retirement. */
    return NSERROR_OK;
}

bool html_exec(struct content *content, const char *source, size_t length)
{
    ck_assert_ptr_eq(content, &html->base);
    ck_assert_ptr_nonnull(expected_script);
    ck_assert_uint_eq(length, strlen(expected_script));
    ck_assert_int_eq(memcmp(source, expected_script, length), 0);
    scripts++;
    return true;
}

static void click(dom_event *event, void *pw)
{
    clicks++;
    ck_assert_uint_eq(content_count_users(&html->base), expected_users);
    if (edit_on_click != NULL)
        edit_on_click();
    if (nest && !nested) {
        nested = true;
        expected_users++;
        ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
        expected_users--;
        /* Inner dispatch has released just its own pin, even after closure. */
        ck_assert_uint_eq(content_count_users(&html->base), browser_user ? 2 : 1);
    } else if (pw != NULL) {
        retire_document();
    }
    if (cancel_click)
        ck_assert_int_eq(dom_event_prevent_default(event), DOM_NO_ERR);
}

static dom_node *by_id(const char *id)
{
    dom_string *key;
    dom_node *result = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)id, strlen(id), &key), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_get_element_by_id(html->document, key, &result), DOM_NO_ERR);
    dom_string_unref(key);
    ck_assert_ptr_nonnull(result);
    return result;
}

static void target(const char *id, bool retire)
{
    node = by_id(id);
    html->bctx = talloc_new(NULL);
    ck_assert_ptr_nonnull(html->bctx);
    nsurl *url = NULL;
    if (strcmp(id, "link") == 0 || strcmp(id, "link-text") == 0)
        ck_assert_int_eq(nsurl_create("https://example.test/next", &url), NSERROR_OK);
    html->layout = box_create(NULL, NULL, false, url, "_self", NULL, NULL, html->bctx);
    if (url != NULL)
        nsurl_unref(url);
    ck_assert_ptr_nonnull(html->layout);
    html->layout->node = dom_node_ref(node);
    html->layout->type = BOX_BLOCK;
    html->layout->width = html->layout->height = 50;
    if (strcmp(id, "check") == 0) {
        html->layout->gadget = form_new_control(node, GADGET_CHECKBOX);
        ck_assert_ptr_nonnull(html->layout->gadget);
        html->layout->gadget->html = html;
        html->layout->gadget->box = html->layout;
    }
    if (strcmp(id, "submit") == 0 || strcmp(id, "imgsubmit") == 0) {
        dom_node *form = by_id("form");
        html->forms = form_new(form);
        dom_node_unref(form);
        ck_assert_ptr_nonnull(html->forms);
        html->layout->gadget = form_new_control(node, strcmp(id, "imgsubmit") == 0 ? GADGET_IMAGE : GADGET_SUBMIT);
        ck_assert_ptr_nonnull(html->layout->gadget);
        html->layout->gadget->html = html;
        html->layout->gadget->box = html->layout;
        form_add_control(html->forms, html->layout->gadget);
    }
    ck_assert_int_eq(dom_event_listener_create(click, retire ? html : NULL, &listener), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_target_add_event_listener(node, corestring_dom_click, listener, false), DOM_NO_ERR);
}

static void setup(void)
{
    memset(&browser, 0, sizeof(browser));
    browser.browser_window_type = BROWSER_WINDOW_NORMAL;
    browser.scale = 1.0f;
    html = calloc(1, sizeof(*html));
    ck_assert_ptr_nonnull(html);
    html->base.handler = handler;
    html->base.llcache = (llcache_handle *)&user_identity;
    html->base.user_list = calloc(1, sizeof(struct content_user));
    ck_assert_ptr_nonnull(html->base.user_list);
    html->base.status = CONTENT_STATUS_DONE;
    ck_assert_int_eq(nsurl_create("https://example.test/", &html->base_url), NSERROR_OK);
    html->bw = window();
    html->sel = selection_create(&html->base);
    ck_assert_ptr_nonnull(html->sel);
    const char source[] = "<!doctype html><head><base id=base><base id=second-base></head>"
                          "<a id=link href=/next target=_self><span id=link-text>link</span></a>"
                          "<a id=other-link href=/other></a><input id=check type=checkbox>"
                          "<object id=object></object>"
                          "<map id=map><area id=area shape=default href=/next target=_self></map>"
                          "<img id=image usemap='#map'>"
                          "<form id=form action=/send><fieldset id=fieldset><legend id=legend>Legend</legend>"
                          "<input id=field name=q value=old>"
                          "<button id=submit name=go value=yes><span id=submit-text>Send</span></button>"
                          "<input id=other-submit type=submit name=other value=no>"
                          "<input id=imgsubmit type=image name=pic></fieldset></form>"
                          "<form id=otherform action=/other-send><input name=z value=two></form>"
                          "<details id=details><summary id=summary>summary</summary></details>";
    dom_hubbub_parser *parser;
    dom_hubbub_parser_params params = {.enc = "UTF-8", .fix_enc = true};
    ck_assert_int_eq(dom_hubbub_parser_create(&params, &parser, &html->document), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)source, sizeof(source) - 1), DOM_HUBBUB_OK);
    ck_assert_int_eq(dom_hubbub_parser_completed(parser), DOM_HUBBUB_OK);
    dom_hubbub_parser_destroy(parser);
    ck_assert(content_add_user(&html->base, browser_message, &user_identity));
    browser_user = true;
    cancel_click = close_in_navigation = close_on_status = reopen = nest = nested = false;
    clicks = navigations = schedules = pointer_messages = 0;
    child_clicks = 0;
    close_in_child = false;
    expected_users = 2;
    expected_url = "https://example.test/next";
    expected_target = "_self";
    expected_flags = BW_NAVIGATE_HISTORY;
    expected_script = NULL;
    scripts = saved_links = 0;
    edit_on_click = NULL;
    close_in_target = false;
    expected_post = NULL;
    expected_multipart = false;
    node = NULL;
    listener = NULL;
}

static void teardown(void)
{
    if (html->bw != NULL)
        ck_assert_int_eq(handler->close(&html->base), NSERROR_OK);
    if (browser_user)
        content_remove_user(&html->base, browser_message, &user_identity);
    ck_assert_uint_eq(content_count_users(&html->base), 0);
    if (listener != NULL) {
        ck_assert_int_eq(
            dom_event_target_remove_event_listener(node, corestring_dom_click, listener, false), DOM_NO_ERR);
        dom_event_listener_unref(listener);
    }
    talloc_free(html->bctx);
    while (html->forms != NULL) {
        struct form *prev = html->forms->prev;
        form_free(html->forms);
        html->forms = prev;
    }
    imagemap_destroy(html);
    selection_destroy(html->sel);
    dom_node_unref(node);
    dom_node_unref(html->document);
    nsurl_unref(html->base_url);
    free(html->base_target);
    free(html->base.user_list);
    free(html);
}

START_TEST(test_navigation_and_cancellation)
{
    cancel_click = (_i & 1) != 0;
    close_in_navigation = (_i & 2) != 0;
    target("link", false);
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(clicks, 1);
    ck_assert_uint_eq(navigations, cancel_click ? 0 : 1);
    ck_assert_uint_eq(content_count_users(&html->base), browser_user ? 1 : 0);
}
END_TEST

START_TEST(test_retired_click)
{
    reopen = (_i & 1) != 0;
    nest = (_i & 2) != 0;
    cancel_click = (_i & 4) != 0;
    target("link", true);
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(clicks, nest ? 2 : 1);
    ck_assert_uint_eq(navigations, 0);
    ck_assert_uint_eq(content_count_users(&html->base), reopen ? 1 : 0);
}
END_TEST

START_TEST(test_retired_activation)
{
    const bool checkbox = _i == 0;
    cancel_click = checkbox;
    target(checkbox ? "check" : "summary", true);
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(clicks, 1);
    if (checkbox) {
        bool checked = true;
        ck_assert_int_eq(dom_html_input_element_get_checked((dom_html_input_element *)node, &checked), DOM_NO_ERR);
        ck_assert(!checked);
        ck_assert_uint_eq(schedules, 1); /* Preactivation only, not rollback after close. */
    } else {
        dom_node *details = by_id("details");
        bool open = true;
        ck_assert_int_eq(dom_element_has_attribute(details, corestring_dom_open, &open), DOM_NO_ERR);
        ck_assert(!open);
        dom_node_unref(details);
        ck_assert_uint_eq(schedules, 0);
    }
    ck_assert_uint_eq(content_count_users(&html->base), 0);
}
END_TEST

START_TEST(test_frontend_retires_before_dispatch)
{
    target("link", false);
    close_on_status = true;
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(clicks, 0);
    ck_assert_uint_eq(pointer_messages, 0);
    ck_assert_uint_eq(navigations, 0);
    ck_assert_uint_eq(content_count_users(&html->base), 0);
}
END_TEST

START_TEST(test_pointer_motion_does_not_pin)
{
    target("link", false);
    expected_users = 1;
    ck_assert_int_eq(html_mouse_track(&html->base, window(), 0, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(clicks, 0);
    ck_assert_uint_eq(content_count_users(&html->base), 1);
}
END_TEST

START_TEST(test_secondary_click_lifetime)
{
    target("link", false);
    close_in_navigation = true;
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_2, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(clicks, 0); /* No primary click dispatched for this button. */
    ck_assert_uint_eq(navigations, 1);
    ck_assert_uint_eq(content_count_users(&html->base), 0);
}
END_TEST

START_TEST(test_embedded_document_owns_click)
{
    content_handler child_handler = {.type = handler->type, .mouse_action = child_mouse_action};
    child.handler = &child_handler;
    target("object", false);
    html->layout->object = (hlcache_handle *)&child;
    close_in_child = _i != 0;
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(child_clicks, 1);
    ck_assert_uint_eq(clicks, 0); /* A child-document click does not bubble into its host. */
    ck_assert_uint_eq(content_count_users(&html->base), close_in_child ? 0 : 1);
}
END_TEST

static void attribute(dom_node *element, dom_string *name, const char *text)
{
    if (text == NULL) {
        ck_assert_int_eq(dom_element_remove_attribute(element, name), DOM_NO_ERR);
    } else {
        dom_string *value;
        ck_assert_int_eq(dom_string_create((const uint8_t *)text, strlen(text), &value), DOM_NO_ERR);
        ck_assert_int_eq(dom_element_set_attribute(element, name, value), DOM_NO_ERR);
        dom_string_unref(value);
    }
}

static void remove_node(dom_node *element)
{
    dom_node *parent = NULL, *removed = NULL;
    ck_assert_int_eq(dom_node_get_parent_node(element, &parent), DOM_NO_ERR);
    ck_assert_ptr_nonnull(parent);
    ck_assert_int_eq(dom_node_remove_child(parent, element, &removed), DOM_NO_ERR);
    dom_node_unref(removed);
    dom_node_unref(parent);
}

static void discard_presentation(void)
{
    talloc_free(html->bctx);
    html->bctx = NULL;
    html->layout = NULL;
    while (html->forms != NULL) {
        struct form *prev = html->forms->prev;
        form_free(html->forms);
        html->forms = prev;
    }
    imagemap_destroy(html);
}

enum link_edit {
    LINK_HREF,
    LINK_TARGET,
    LINK_NO_HREF,
    LINK_ADD_HREF,
    LINK_REMOVE,
    LINK_REPLACE,
    LINK_MOVE_CLICKED,
    LINK_BOXES,
    LINK_TO_SCRIPT,
    LINK_FROM_SCRIPT,
    LINK_EMPTY_HREF,
    LINK_SCRIPT_DISABLED,
    LINK_BASE_TARGET,
    LINK_REMOVE_BASE,
    LINK_EMPTY_TARGET,
    LINK_UNSAFE_TARGET,
    LINK_CANCEL,
    LINK_EDITS
};
static enum link_edit link_edit;

static void edit_link(void)
{
    dom_node *link = by_id("link");
    switch (link_edit) {
    case LINK_HREF:
    case LINK_ADD_HREF:
    case LINK_FROM_SCRIPT:
    case LINK_CANCEL:
        attribute(link, corestring_dom_href, "/changed");
        break;
    case LINK_TARGET:
        attribute(link, corestring_dom_target, "news-frame");
        break;
    case LINK_NO_HREF:
        attribute(link, corestring_dom_href, NULL);
        break;
    case LINK_REMOVE:
        remove_node(link);
        break;
    case LINK_REPLACE: {
        dom_node *replacement = NULL, *parent = NULL, *old = NULL;
        ck_assert_int_eq(dom_document_create_element(html->document, corestring_dom_a, &replacement), DOM_NO_ERR);
        attribute(replacement, corestring_dom_href, "/replacement");
        attribute(replacement, corestring_dom_id, "link");
        ck_assert_int_eq(dom_node_get_parent_node(link, &parent), DOM_NO_ERR);
        ck_assert_int_eq(dom_node_replace_child(parent, replacement, link, &old), DOM_NO_ERR);
        dom_node_unref(old);
        dom_node_unref(parent);
        dom_node_unref(replacement);
        break;
    }
    case LINK_MOVE_CLICKED: {
        dom_node *other = by_id("other-link"), *result = NULL;
        ck_assert_int_eq(dom_node_append_child(other, node, &result), DOM_NO_ERR);
        dom_node_unref(result);
        dom_node_unref(other);
        break;
    }
    case LINK_TO_SCRIPT:
        attribute(link, corestring_dom_href, "javascript:void(42)");
        break;
    case LINK_EMPTY_HREF:
        attribute(link, corestring_dom_href, "");
        break;
    case LINK_BASE_TARGET:
    case LINK_REMOVE_BASE: {
        dom_node *base = by_id("base"), *second = by_id("second-base");
        attribute(link, corestring_dom_target, NULL);
        attribute(base, corestring_dom_target, "first-frame");
        attribute(second, corestring_dom_target, "second-frame");
        if (link_edit == LINK_REMOVE_BASE)
            remove_node(base);
        dom_node_unref(second);
        dom_node_unref(base);
        break;
    }
    case LINK_EMPTY_TARGET:
        attribute(link, corestring_dom_target, "");
        break;
    case LINK_UNSAFE_TARGET:
        attribute(link, corestring_dom_target, "frame<\nname");
        break;
    default:
        break;
    }
    dom_node_unref(link);
    /* This is not a restyle simulation: explicitly retire all storage which the
     * old hit test borrowed. DOM changes and click dispatch above remain real. */
    discard_presentation();
}

START_TEST(test_link_uses_original_identity_and_live_attributes)
{
    link_edit = _i;
    target("link-text", false);
    dom_node *link = by_id("link");
    if (link_edit == LINK_ADD_HREF) {
        attribute(link, corestring_dom_href, NULL);
        nsurl_unref(html->layout->href);
        html->layout->href = NULL;
    }
    if (link_edit == LINK_FROM_SCRIPT || link_edit == LINK_SCRIPT_DISABLED) {
        attribute(link, corestring_dom_href, "javascript:open('https://example.test/guessed')");
        nsurl_unref(html->layout->href);
        dom_string *href = NULL;
        ck_assert_int_eq(dom_element_get_attribute(link, corestring_dom_href, &href), DOM_NO_ERR);
        ck_assert_int_eq(html_link_url(href, html->base_url, &html->layout->href), NSERROR_OK);
        lwc_string *scheme = nsurl_get_component(html->layout->href, NSURL_SCHEME);
        ck_assert_ptr_eq(scheme, corestring_lwc_javascript);
        lwc_string_unref(scheme);
        dom_string_unref(href);
    }
    dom_node_unref(link);
    unsigned want_navigation = 1;
    switch (link_edit) {
    case LINK_HREF:
    case LINK_ADD_HREF:
    case LINK_FROM_SCRIPT:
    case LINK_CANCEL:
        expected_url = "https://example.test/changed";
        if (link_edit == LINK_CANCEL) {
            cancel_click = true;
            want_navigation = 0;
        }
        break;
    case LINK_NO_HREF:
    case LINK_SCRIPT_DISABLED:
        want_navigation = 0;
        break;
    case LINK_TARGET:
        expected_target = "news-frame";
        break;
    case LINK_TO_SCRIPT:
        html->enable_scripting = true;
        expected_script = "void(42)";
        want_navigation = 0;
        break;
    case LINK_EMPTY_HREF:
        expected_url = "https://example.test/";
        break;
    case LINK_BASE_TARGET:
        expected_target = "first-frame";
        break;
    case LINK_REMOVE_BASE:
        expected_target = "second-frame";
        break;
    case LINK_UNSAFE_TARGET:
        expected_target = "_blank";
        break;
    default:
        break;
    }
    /* The live resolver must not fall back to this obsolete cached value. */
    html->base_target = strdup("stale-frame");
    edit_on_click = edit_link;
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_msg(navigations == want_navigation, "link edit %d: expected %u navigations, got %u", link_edit,
        want_navigation, navigations);
    ck_assert_uint_eq(scripts, link_edit == LINK_TO_SCRIPT ? 1 : 0);
    ck_assert_uint_eq(clicks, 1);
    ck_assert_uint_eq(content_count_users(&html->base), 1);
}
END_TEST

static bool detach_area;
static void edit_area(void)
{
    attribute(node, corestring_dom_href, "/changed-map");
    attribute(node, corestring_dom_target, "map-frame");
    if (detach_area)
        remove_node(node);
    discard_presentation();
}

START_TEST(test_image_map_keeps_area_identity)
{
    target("area", false);
    dom_node_unref(html->layout->node);
    html->layout->node = by_id("image");
    html->layout->usemap = talloc_strdup(html->bctx, "map");
    ck_assert_int_eq(imagemap_extract(html), NSERROR_OK);
    detach_area = _i == 1;
    cancel_click = _i == 2;
    edit_on_click = edit_area;
    expected_url = "https://example.test/changed-map";
    expected_target = "map-frame";
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(clicks, 1);
    ck_assert_uint_eq(navigations, _i == 0 ? 1 : 0);
}
END_TEST

START_TEST(test_target_lookup_can_retire_document)
{
    target("link", false);
    close_in_target = true;
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(clicks, 1);
    ck_assert_uint_eq(navigations, 0);
    ck_assert_uint_eq(content_count_users(&html->base), 0);
}
END_TEST

START_TEST(test_download_uses_updated_link)
{
    target("link", false);
    link_edit = LINK_HREF;
    edit_on_click = edit_link;
    expected_url = "https://example.test/changed";
    expected_flags = BW_NAVIGATE_DOWNLOAD;
    ck_assert_int_eq(
        test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1 | BROWSER_MOUSE_MOD_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(navigations, 1);
}
END_TEST

START_TEST(test_save_link_does_not_navigate)
{
    target("link", false);
    ck_assert_int_eq(
        test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_2 | BROWSER_MOUSE_MOD_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(saved_links, 1);
    ck_assert_uint_eq(clicks, 0);
    ck_assert_uint_eq(navigations, 0);
}
END_TEST

START_TEST(test_scrollbar_does_not_activate_link)
{
    target("link", false);
    ck_assert_int_eq(scrollbar_create(false, 50, 100, 50, NULL, NULL, &html->layout->scroll_y), NSERROR_OK);
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 45, 5), NSERROR_OK);
    ck_assert_uint_eq(navigations, 0);
}
END_TEST

enum submit_edit {
    SUBMIT_UNCHANGED, SUBMIT_ACTION, SUBMIT_OVERRIDE, SUBMIT_EMPTY_OVERRIDE,
    SUBMIT_REMOVE_ACTION, SUBMIT_TARGET, SUBMIT_TARGET_OVERRIDE, SUBMIT_EMPTY_TARGET,
    SUBMIT_BASE_TARGET, SUBMIT_POST, SUBMIT_MULTIPART, SUBMIT_EMPTY_METHOD,
    SUBMIT_EMPTY_ENCTYPE, SUBMIT_MOVED, SUBMIT_FORM_ATTRIBUTE, SUBMIT_REMOVED,
    SUBMIT_FORM_REMOVED, SUBMIT_REPLACED, SUBMIT_TYPE_BUTTON, SUBMIT_TYPE_INVALID,
    SUBMIT_DISABLED, SUBMIT_FIELDSET_DISABLED, SUBMIT_FIRST_LEGEND,
    SUBMIT_VALUE, SUBMIT_CANCEL, SUBMIT_CLOSE, SUBMIT_CLOSE_TARGET,
    SUBMIT_CLOSE_NAVIGATION, SUBMIT_COMMAND, SUBMIT_DIALOG, SUBMIT_PLAIN,
    SUBMIT_EDITS
};
static enum submit_edit submit_edit;

static void append_node(dom_node *parent, dom_node *child)
{
    dom_node *result = NULL;
    ck_assert_int_eq(dom_node_append_child(parent, child, &result), DOM_NO_ERR);
    dom_node_unref(result);
}

static void edit_submit(void)
{
    dom_node *button = by_id("submit"), *form = by_id("form"), *field = by_id("field");
    switch (submit_edit) {
    case SUBMIT_ACTION: attribute(form, corestring_dom_action, "/changed"); break;
    case SUBMIT_OVERRIDE: attribute(button, corestring_dom_formaction, "/override"); break;
    case SUBMIT_EMPTY_OVERRIDE: attribute(button, corestring_dom_formaction, ""); break;
    case SUBMIT_REMOVE_ACTION: attribute(form, corestring_dom_action, NULL); break;
    case SUBMIT_TARGET: attribute(form, corestring_dom_target, "news"); break;
    case SUBMIT_TARGET_OVERRIDE: attribute(button, corestring_dom_formtarget, "chosen"); break;
    case SUBMIT_EMPTY_TARGET:
        attribute(form, corestring_dom_target, "must-not-use");
        attribute(button, corestring_dom_formtarget, "");
        break;
    case SUBMIT_BASE_TARGET: {
        dom_node *base = by_id("base");
        attribute(base, corestring_dom_target, "base-frame");
        dom_node_unref(base);
        break;
    }
    case SUBMIT_POST:
        attribute(button, corestring_dom_formmethod, "PoSt");
        break;
    case SUBMIT_MULTIPART:
        attribute(form, corestring_dom_method, "post");
        attribute(button, corestring_dom_formenctype, "MULTIPART/FORM-DATA");
        break;
    case SUBMIT_EMPTY_METHOD:
        attribute(form, corestring_dom_method, "post");
        attribute(button, corestring_dom_formmethod, "");
        break;
    case SUBMIT_EMPTY_ENCTYPE:
        attribute(form, corestring_dom_method, "post");
        attribute(form, corestring_dom_enctype, "multipart/form-data");
        attribute(button, corestring_dom_formenctype, "");
        break;
    case SUBMIT_MOVED: {
        dom_node *other = by_id("otherform");
        append_node(other, button);
        dom_node_unref(other);
        break;
    }
    case SUBMIT_FORM_ATTRIBUTE:
        attribute(button, corestring_dom_form, "otherform");
        break;
    case SUBMIT_REMOVED: remove_node(button); break;
    case SUBMIT_FORM_REMOVED: remove_node(form); break;
    case SUBMIT_REPLACED: {
        dom_node *replacement = NULL;
        ck_assert_int_eq(dom_node_clone_node(button, true, &replacement), DOM_NO_ERR);
        remove_node(button);
        append_node(form, replacement);
        dom_node_unref(replacement);
        break;
    }
    case SUBMIT_TYPE_BUTTON: attribute(button, corestring_dom_type, "button"); break;
    case SUBMIT_TYPE_INVALID: attribute(button, corestring_dom_type, "unknown"); break;
    case SUBMIT_DISABLED: attribute(button, corestring_dom_disabled, ""); break;
    case SUBMIT_FIELDSET_DISABLED:
    case SUBMIT_FIRST_LEGEND: {
        dom_node *fieldset = by_id("fieldset");
        attribute(fieldset, corestring_dom_disabled, "");
        if (submit_edit == SUBMIT_FIRST_LEGEND) {
            dom_node *legend = by_id("legend");
            append_node(legend, button);
            dom_node_unref(legend);
        }
        dom_node_unref(fieldset);
        break;
    }
    case SUBMIT_VALUE:
        attribute(button, corestring_dom_name, "chosen");
        attribute(button, corestring_dom_value, "new");
        attribute(field, corestring_dom_value, "fresh");
        break;
    case SUBMIT_COMMAND:
        attribute(button, corestring_dom_command, "--action");
        break;
    case SUBMIT_DIALOG: attribute(form, corestring_dom_method, "dialog"); break;
    case SUBMIT_PLAIN:
        attribute(form, corestring_dom_method, "post");
        attribute(form, corestring_dom_enctype, "text/plain");
        break;
    default: break;
    }
    dom_node_unref(button);
    dom_node_unref(form);
    dom_node_unref(field);
    /* Destroy both gadgets and cached form records inside the click listener.
     * The DOM remains live, as it does when a listener triggers reconstruction. */
    discard_presentation();
}

START_TEST(test_submit_reads_live_dom_after_click)
{
    submit_edit = _i;
    target("submit", submit_edit == SUBMIT_CLOSE);
    edit_on_click = edit_submit;
    cancel_click = submit_edit == SUBMIT_CANCEL;
    close_in_target = submit_edit == SUBMIT_CLOSE_TARGET;
    close_in_navigation = submit_edit == SUBMIT_CLOSE_NAVIGATION;
    expected_url = "https://example.test/send?q=old&go=yes";
    switch (submit_edit) {
    case SUBMIT_ACTION: expected_url = "https://example.test/changed?q=old&go=yes"; break;
    case SUBMIT_OVERRIDE: expected_url = "https://example.test/override?q=old&go=yes"; break;
    case SUBMIT_EMPTY_OVERRIDE:
    case SUBMIT_REMOVE_ACTION: expected_url = "https://example.test/?q=old&go=yes"; break;
    case SUBMIT_TARGET: expected_target = "news"; break;
    case SUBMIT_TARGET_OVERRIDE: expected_target = "chosen"; break;
    case SUBMIT_BASE_TARGET: expected_target = "base-frame"; break;
    case SUBMIT_POST:
    case SUBMIT_EMPTY_ENCTYPE:
        expected_url = "https://example.test/send";
        expected_post = "q=old&go=yes";
        break;
    case SUBMIT_MULTIPART:
        expected_url = "https://example.test/send";
        expected_multipart = true;
        break;
    case SUBMIT_MOVED: expected_url = "https://example.test/other-send?z=two&go=yes"; break;
    case SUBMIT_FORM_ATTRIBUTE: expected_url = "https://example.test/other-send?go=yes&z=two"; break;
    case SUBMIT_FIRST_LEGEND: expected_url = "https://example.test/send?go=yes"; break;
    case SUBMIT_VALUE: expected_url = "https://example.test/send?q=fresh&chosen=new"; break;
    default: break;
    }
    bool submits = submit_edit != SUBMIT_REMOVED && submit_edit != SUBMIT_FORM_REMOVED &&
        submit_edit != SUBMIT_REPLACED && submit_edit != SUBMIT_TYPE_BUTTON && submit_edit != SUBMIT_DISABLED &&
        submit_edit != SUBMIT_FIELDSET_DISABLED && submit_edit != SUBMIT_CANCEL && submit_edit != SUBMIT_CLOSE &&
        submit_edit != SUBMIT_CLOSE_TARGET && submit_edit != SUBMIT_COMMAND && submit_edit != SUBMIT_DIALOG &&
        submit_edit != SUBMIT_PLAIN;
    nserror result = test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5);
    ck_assert_msg(result == (submit_edit == SUBMIT_PLAIN ? NSERROR_NOT_IMPLEMENTED : NSERROR_OK),
        "submit variant %d returned %d", _i, result);
    ck_assert_uint_eq(clicks, 1);
    ck_assert_msg(navigations == submits, "submit variant %d navigated %u times", _i, navigations);
    ck_assert_uint_eq(content_count_users(&html->base), browser_user ? 1 : 0);
}
END_TEST

enum submit_callback_edit {
    CALLBACK_UNCHANGED,
    CALLBACK_CANCEL,
    CALLBACK_VALUES,
    CALLBACK_REMOVE_FORM,
    CALLBACK_REINSERT_FORM,
    CALLBACK_REMOVE_BUTTON,
    CALLBACK_MOVE_BUTTON,
    CALLBACK_TYPE_BUTTON,
    CALLBACK_DISABLE_BUTTON,
    CALLBACK_CLOSE,
    CALLBACK_REOPEN,
    CALLBACK_REBUILD,
    CALLBACK_NEST_SAME,
    CALLBACK_NEST_OTHER,
    CALLBACK_EDITS
};
static enum submit_callback_edit submit_callback_edit;
static unsigned submissions;

static void submitting(dom_event *event, void *pw)
{
    submissions++;
    ck_assert_msg(submissions == 1, "variant %d recursively dispatched submit", submit_callback_edit);
    dom_node *form = pw, *button = by_id("submit"), *field = by_id("field");
    bool bubbles = false, cancelable = false;
    ck_assert_int_eq(dom_event_get_bubbles(event, &bubbles), DOM_NO_ERR);
    ck_assert_int_eq(dom_event_get_cancelable(event, &cancelable), DOM_NO_ERR);
    ck_assert(bubbles && cancelable);
    switch (submit_callback_edit) {
    case CALLBACK_CANCEL:
        dom_event_prevent_default(event);
        break;
    case CALLBACK_VALUES:
        attribute(field, corestring_dom_value, "new");
        attribute(button, corestring_dom_value, "edited");
        attribute(button, corestring_dom_formaction, "/edited");
        attribute(button, corestring_dom_formmethod, "post");
        attribute(form, corestring_dom_target, "result");
        break;
    case CALLBACK_REMOVE_FORM:
        remove_node(form);
        break;
    case CALLBACK_REINSERT_FORM: {
        dom_node *parent = NULL;
        ck_assert_int_eq(dom_node_get_parent_node(form, &parent), DOM_NO_ERR);
        remove_node(form);
        append_node(parent, form);
        dom_node_unref(parent);
        break;
    }
    case CALLBACK_REMOVE_BUTTON:
        remove_node(button);
        break;
    case CALLBACK_MOVE_BUTTON: {
        dom_node *other = by_id("otherform");
        append_node(other, button);
        dom_node_unref(other);
        break;
    }
    case CALLBACK_TYPE_BUTTON:
        attribute(button, corestring_dom_type, "button");
        attribute(button, corestring_dom_formaction, "/ignored");
        attribute(button, corestring_dom_formmethod, "post");
        break;
    case CALLBACK_DISABLE_BUTTON:
        attribute(button, corestring_dom_disabled, "");
        break;
    case CALLBACK_CLOSE:
        retire_document();
        break;
    case CALLBACK_REOPEN:
        reopen = true;
        retire_document();
        break;
    case CALLBACK_REBUILD:
        discard_presentation();
        break;
    case CALLBACK_NEST_SAME:
        discard_presentation();
        ck_assert_int_eq(form_submit(html, button, 0), NSERROR_OK);
        ck_assert_int_eq(form_submit(html, button, 0), NSERROR_OK);
        ck_assert_uint_eq(navigations, 0);
        dom_event_prevent_default(event);
        break;
    case CALLBACK_NEST_OTHER:
        attribute(button, corestring_dom_form, "otherform");
        ck_assert_int_eq(form_submit(html, button, 0), NSERROR_OK);
        dom_event_prevent_default(event);
        break;
    default:
        break;
    }
    dom_node_unref(field);
    dom_node_unref(button);
}

START_TEST(test_submission_callbacks)
{
    submit_callback_edit = _i;
    submissions = 0;
    target("submit", false);
    dom_node *form = by_id("form");
    dom_event_listener *submit_listener = NULL;
    ck_assert_int_eq(dom_event_listener_create(submitting, form, &submit_listener), DOM_NO_ERR);
    ck_assert_int_eq(
        dom_event_target_add_event_listener(form, corestring_dom_submit, submit_listener, false), DOM_NO_ERR);
    expected_url = "https://example.test/send?q=old&go=yes";
    switch (submit_callback_edit) {
    case CALLBACK_VALUES:
        expected_url = "https://example.test/edited";
        expected_post = "q=new&go=edited";
        expected_target = "result";
        break;
    case CALLBACK_REMOVE_BUTTON:
        expected_url = "https://example.test/?q=old";
        break;
    case CALLBACK_MOVE_BUTTON:
        expected_url = "https://example.test/other-send?q=old";
        break;
    case CALLBACK_TYPE_BUTTON:
    case CALLBACK_DISABLE_BUTTON:
        expected_url = "https://example.test/send?q=old";
        break;
    case CALLBACK_NEST_OTHER:
        expected_url = "https://example.test/other-send?go=yes&z=two";
        break;
    default:
        break;
    }
    bool navigates = submit_callback_edit != CALLBACK_CANCEL && submit_callback_edit != CALLBACK_REMOVE_FORM &&
        submit_callback_edit != CALLBACK_CLOSE && submit_callback_edit != CALLBACK_REOPEN &&
        submit_callback_edit != CALLBACK_NEST_SAME;
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_msg(submissions == 1 && navigations == navigates,
        "variant %d: %u submissions, %u navigations (expected %d)", _i, submissions, navigations, navigates);
    ck_assert(dom_html_form_element_begin_submission_events((dom_html_form_element *)form));
    dom_html_form_element_end_submission_events((dom_html_form_element *)form);
    ck_assert_int_eq(
        dom_event_target_remove_event_listener(form, corestring_dom_submit, submit_listener, false), DOM_NO_ERR);
    dom_event_listener_unref(submit_listener);
    dom_node_unref(form);
}
END_TEST

START_TEST(test_submit_original_button_ancestor)
{
    target("submit-text", false);
    submit_edit = SUBMIT_VALUE;
    edit_on_click = edit_submit;
    expected_url = "https://example.test/send?q=fresh&chosen=new";
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(navigations, 1);
}
END_TEST

START_TEST(test_submit_image_coordinates)
{
    target("imgsubmit", false);
    if (_i != 0)
        attribute(node, corestring_dom_name, _i == 1 ? "" : NULL);
    expected_url = _i == 0 ? "https://example.test/send?q=old&pic.x=5&pic.y=5" :
        "https://example.test/send?q=old&x=5&y=5";
    edit_on_click = discard_presentation;
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(navigations, 1);
}
END_TEST

START_TEST(test_implicit_submission)
{
    target("submit", _i == 3);
    cancel_click = _i == 1;
    if (_i == 2)
        attribute(node, corestring_dom_disabled, "");
    if (_i == 4) {
        submit_edit = SUBMIT_MOVED;
        edit_on_click = edit_submit;
        expected_url = "https://example.test/other-send?z=two&go=yes";
    } else {
        expected_url = "https://example.test/send?q=old&go=yes";
        edit_on_click = discard_presentation;
    }
    dom_node *field = by_id("field");
    ck_assert_int_eq(form_submit_implicit(html, field), NSERROR_OK);
    dom_node_unref(field);
    ck_assert_uint_eq(clicks, _i == 2 ? 0 : 1);
    ck_assert_uint_eq(navigations, _i == 0 || _i == 4 ? 1 : 0);
    ck_assert_uint_eq(content_count_users(&html->base), browser_user ? 1 : 0);
}
END_TEST

START_TEST(test_implicit_without_submitter)
{
    const char *ids[] = {"submit", "other-submit", "imgsubmit"};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        dom_node *button = by_id(ids[i]);
        remove_node(button);
        dom_node_unref(button);
    }
    dom_node *field = by_id("field");
    if (_i > 0) {
        dom_node *extra = NULL, *form = by_id("form");
        ck_assert_int_eq(dom_node_clone_node(field, false, &extra), DOM_NO_ERR);
        attribute(extra, corestring_dom_name, "extra");
        /* Disabled text inputs still count as blockers. Unknown types are Text;
         * checkboxes and other nonblocking states do not prevent Enter. */
        if (_i == 2)
            attribute(extra, corestring_dom_type, "unknown");
        if (_i == 3)
            attribute(extra, corestring_dom_disabled, "");
        if (_i == 4)
            attribute(extra, corestring_dom_type, "checkbox");
        append_node(form, extra);
        dom_node_unref(extra);
        dom_node_unref(form);
    }
    expected_url = "https://example.test/send?q=old";
    ck_assert_int_eq(form_submit_implicit(html, field), NSERROR_OK);
    dom_node_unref(field);
    ck_assert_uint_eq(clicks, 0);
    ck_assert_uint_eq(navigations, _i == 0 || _i == 4 ? 1 : 0);
}
END_TEST

static dom_node *new_child(dom_node *parent, const char *tag)
{
    dom_string *name = NULL;
    dom_node *element = NULL;
    ck_assert_int_eq(dom_string_create((const uint8_t *)tag, strlen(tag), &name), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(html->document, name, &element), DOM_NO_ERR);
    dom_string_unref(name);
    append_node(parent, element);
    return element;
}

static dom_node *selected_option(dom_node *parent, const char *value)
{
    dom_node *option = new_child(parent, "option");
    attribute(option, corestring_dom_value, value);
    ck_assert_int_eq(dom_html_option_element_set_selected((dom_html_option_element *)option, true), DOM_NO_ERR);
    return option;
}

START_TEST(test_submission_omits_datalist_descendants)
{
    target("submit", false);
    dom_node *form = by_id("form"), *field = by_id("field");
    dom_node *list = new_child(form, "datalist");
    append_node(list, field);
    dom_node *select = new_child(list, "select");
    attribute(select, corestring_dom_name, "choice");
    dom_node *option = selected_option(select, "omitted");
    dom_node *textarea = new_child(list, "textarea");
    attribute(textarea, corestring_dom_name, "note");
    dom_node *kept = new_child(form, "input");
    attribute(kept, corestring_dom_name, "keep");
    attribute(kept, corestring_dom_value, "yes");
    bool post = (_i & 1) != 0, nested_submitter = (_i & 2) != 0;
    if (nested_submitter)
        append_node(list, node);
    if (post) {
        attribute(form, corestring_dom_method, "post");
        expected_url = "https://example.test/send";
        expected_post = nested_submitter ? "keep=yes" : "go=yes&keep=yes";
    } else {
        expected_url = nested_submitter ? "https://example.test/send?keep=yes" :
            "https://example.test/send?go=yes&keep=yes";
    }
    edit_on_click = discard_presentation;
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(navigations, 1);
    dom_node_unref(kept);
    dom_node_unref(textarea);
    dom_node_unref(option);
    dom_node_unref(select);
    dom_node_unref(list);
    dom_node_unref(field);
    dom_node_unref(form);
}
END_TEST

START_TEST(test_submission_omits_disabled_options)
{
    target("submit", false);
    dom_node *form = by_id("form");
    dom_node *select = new_child(form, "select");
    attribute(select, corestring_dom_name, "choice");
    ck_assert_int_eq(dom_html_select_element_set_multiple((dom_html_select_element *)select, true), DOM_NO_ERR);
    dom_node *own = selected_option(select, "self");
    attribute(own, corestring_dom_disabled, "");
    dom_node *group = new_child(select, "optgroup");
    attribute(group, corestring_dom_disabled, "");
    dom_node *grouped = selected_option(group, "group");
    dom_node *enabled = selected_option(select, "ok");
    expected_url = "https://example.test/send?q=old&go=yes&choice=ok";
    if (_i == 1) {
        attribute(own, corestring_dom_disabled, NULL);
        expected_url = "https://example.test/send?q=old&go=yes&choice=self&choice=ok";
    } else if (_i == 2) {
        attribute(group, corestring_dom_disabled, NULL);
        expected_url = "https://example.test/send?q=old&go=yes&choice=group&choice=ok";
    } else if (_i == 3) {
        /* Customizable select permits wrappers: disabledness is not limited
         * to the option's immediate parent. */
        dom_node *wrapper = new_child(group, "div");
        append_node(wrapper, grouped);
        dom_node_unref(wrapper);
    } else if (_i == 4) {
        attribute(select, corestring_dom_disabled, "");
        expected_url = "https://example.test/send?q=old&go=yes";
    }
    edit_on_click = discard_presentation;
    ck_assert_int_eq(test_mouse_action(&html->base, window(), BROWSER_MOUSE_CLICK_1, 5, 5), NSERROR_OK);
    ck_assert_uint_eq(navigations, 1);
    dom_node_unref(enabled);
    dom_node_unref(grouped);
    dom_node_unref(group);
    dom_node_unref(own);
    dom_node_unref(select);
    dom_node_unref(form);
}
END_TEST

START_TEST(test_implicit_image_not_in_public_collection)
{
    const char *ids[] = {"submit", "other-submit"};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        dom_node *button = by_id(ids[i]);
        remove_node(button);
        dom_node_unref(button);
    }
    target("imgsubmit", false);
    if (_i != 0)
        attribute(node, corestring_dom_disabled, "");
    dom_node *form = by_id("form"), *field = by_id("field");
    dom_html_collection *elements = NULL;
    ck_assert_int_eq(dom_html_form_element_get_elements((dom_html_form_element *)form, &elements), DOM_NO_ERR);
    uint32_t length = 0;
    ck_assert_int_eq(dom_html_collection_get_length(elements, &length), DOM_NO_ERR);
    bool found_field = false;
    for (uint32_t i = 0; i < length; i++) {
        dom_node *item = NULL;
        ck_assert_int_eq(dom_html_collection_item(elements, i, &item), DOM_NO_ERR);
        ck_assert_ptr_ne(item, node); /* Public elements excludes image inputs. */
        found_field |= item == field;
        dom_node_unref(item);
    }
    ck_assert(found_field);
    dom_html_collection_unref(elements);
    expected_url = "https://example.test/send?q=old&pic.x=0&pic.y=0";
    edit_on_click = discard_presentation;
    ck_assert_int_eq(form_submit_implicit(html, field), NSERROR_OK);
    ck_assert_uint_eq(clicks, _i == 0 ? 1 : 0);
    ck_assert_uint_eq(navigations, _i == 0 ? 1 : 0);
    dom_node_unref(field);
    dom_node_unref(form);
}
END_TEST

START_TEST(test_form_records_retain_dom_nodes)
{
    dom_document *document = NULL;
    dom_node *form_node = NULL, *input = NULL;
    ck_assert_int_eq(
        dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL, NULL, NULL, &document),
        DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(document, corestring_dom_form, &form_node), DOM_NO_ERR);
    ck_assert_int_eq(dom_document_create_element(document, corestring_dom_input, &input), DOM_NO_ERR);
    append_node((dom_node *)document, form_node);
    append_node(form_node, input);
    attribute(input, corestring_dom_value, "retained");
    struct form *record = form_new(form_node);
    struct form_control *control = form_new_control(input, GADGET_HIDDEN);
    ck_assert_ptr_nonnull(record);
    ck_assert_ptr_nonnull(control);
    form_add_control(record, control);
    if (_i != 0)
        remove_node(input);
    dom_node_unref(input);
    dom_node_unref(form_node);
    dom_node_unref(document);
    /* Only the presentation records hold references now, including a control
     * removed by script before its old presentation has been retired. */
    dom_string *value = NULL;
    ck_assert_int_eq(dom_html_input_element_get_value(control->node, &value), DOM_NO_ERR);
    ck_assert_str_eq(dom_string_data(value), "retained");
    dom_string_unref(value);
    dom_html_form_element *owner = NULL;
    ck_assert_int_eq(dom_html_input_element_get_form(control->node, &owner), DOM_NO_ERR);
    ck_assert_ptr_eq(owner, _i == 0 ? record->node : NULL);
    dom_node_unref(owner);
    form_free(record);
}
END_TEST

int main(void)
{
    static struct gui_misc_table misc = {.schedule = schedule};
    static struct wisp_table gui = {.misc = &misc};
    guit = &gui;
    if (corestrings_init() != NSERROR_OK || nsoption_init(NULL, NULL, NULL) != NSERROR_OK || html_init() != NSERROR_OK)
        return EXIT_FAILURE;
    Suite *suite = suite_create("Native input lifetime");
    TCase *tc = tcase_create("Callback retirement");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_loop_test(tc, test_navigation_and_cancellation, 0, 4);
    tcase_add_loop_test(tc, test_retired_click, 0, 8);
    tcase_add_loop_test(tc, test_retired_activation, 0, 2);
    tcase_add_test(tc, test_frontend_retires_before_dispatch);
    tcase_add_test(tc, test_pointer_motion_does_not_pin);
    tcase_add_test(tc, test_secondary_click_lifetime);
    tcase_add_loop_test(tc, test_embedded_document_owns_click, 0, 2);
    tcase_add_loop_test(tc, test_link_uses_original_identity_and_live_attributes, 0, LINK_EDITS);
    tcase_add_loop_test(tc, test_image_map_keeps_area_identity, 0, 3);
    tcase_add_test(tc, test_target_lookup_can_retire_document);
    tcase_add_test(tc, test_download_uses_updated_link);
    tcase_add_test(tc, test_save_link_does_not_navigate);
    tcase_add_test(tc, test_scrollbar_does_not_activate_link);
    tcase_add_loop_test(tc, test_submit_reads_live_dom_after_click, 0, SUBMIT_EDITS);
    tcase_add_loop_test(tc, test_submission_callbacks, 0, CALLBACK_EDITS);
    tcase_add_test(tc, test_submit_original_button_ancestor);
    tcase_add_loop_test(tc, test_submit_image_coordinates, 0, 3);
    tcase_add_loop_test(tc, test_implicit_submission, 0, 5);
    tcase_add_loop_test(tc, test_implicit_without_submitter, 0, 5);
    tcase_add_loop_test(tc, test_submission_omits_datalist_descendants, 0, 4);
    tcase_add_loop_test(tc, test_submission_omits_disabled_options, 0, 5);
    tcase_add_loop_test(tc, test_implicit_image_not_in_public_collection, 0, 2);
    suite_add_tcase(suite, tc);
    TCase *records = tcase_create("Form record ownership");
    tcase_add_loop_test(records, test_form_records_retain_dom_nodes, 0, 2);
    suite_add_tcase(suite, records);
    SRunner *runner = srunner_create(suite);
    srunner_run_all(runner, CK_NORMAL);
    int failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    nsoption_finalise(NULL, NULL);
    corestrings_fini();
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
