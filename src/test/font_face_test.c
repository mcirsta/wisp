/* SPDX-License-Identifier: GPL-2.0-only */
/* Exercise the real parser/selector and loader with deterministic fetch events. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libcss/libcss.h>
#include <wisp/content/handlers/html/private.h>
#include "content/handlers/css/internal.h"
#include <wisp/utils/nsurl.h>
#include <wisp/content/llcache.h>
#include <wisp/layout.h>
#include "content/handlers/html/font_face.h"

#define REQUIRE(e)                                                                                                     \
    do {                                                                                                               \
        if (!(e)) {                                                                                                    \
            fprintf(stderr, "%d: %s\n", __LINE__, #e);                                                                 \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

struct llcache_handle {
    llcache_handle_callback callback;
    void *pw;
    char *url;
    bool released, aborted;
};
static llcache_handle requests[128];
static unsigned request_count, loaded_count, release_count;
static uint64_t loaded_set, released_set;
static uint32_t loaded_order;
static html_content *resumed;

nserror llcache_handle_retrieve(nsurl *url, uint32_t flags, nsurl *referer, const llcache_post_data *post,
    llcache_handle_callback callback, void *pw, llcache_handle **result)
{
    (void)flags;
    (void)referer;
    (void)post;
    REQUIRE(request_count < 128);
    llcache_handle *h = &requests[request_count++];
    *h = (llcache_handle){.callback = callback, .pw = pw, .url = strdup(nsurl_access(url))};
    REQUIRE(h->url != NULL);
    *result = h;
    return NSERROR_OK;
}

nserror llcache_handle_release(llcache_handle *handle)
{
    REQUIRE(!handle->released);
    handle->released = true;
    handle->callback = NULL;
    free(handle->url);
    handle->url = NULL;
    return NSERROR_OK;
}

nserror llcache_handle_change_callback(llcache_handle *handle, llcache_handle_callback callback, void *pw)
{
    handle->callback = callback;
    handle->pw = pw;
    return NSERROR_OK;
}

nserror llcache_handle_abort(llcache_handle *handle)
{
    REQUIRE(handle->pw == NULL); /* Owner detached before cancellation. */
    llcache_event progress = {.type = LLCACHE_EVENT_PROGRESS};
    REQUIRE(handle->callback(handle, &progress, handle->pw) == NSERROR_OK);
    handle->aborted = true;
    return NSERROR_OK;
}

const uint8_t *llcache_handle_get_source_data(const llcache_handle *handle, size_t *size)
{
    (void)handle;
    *size = 1;
    return (const uint8_t *)"x";
}

void html_finish_conversion(html_content *html)
{
    REQUIRE(!html_font_face_has_pending(html));
    resumed = html;
}

static nserror load_font(const struct font_variant_id *id, const uint8_t *data, size_t size)
{
    REQUIRE(size == 1 && data[0] == 'x');
    REQUIRE(strcmp(id->family_name, "Shared") == 0);
    loaded_count++;
    loaded_set = id->font_set;
    loaded_order = id->order;
    return NSERROR_OK;
}

static void release_set(uint64_t id)
{
    release_count++;
    released_set = id;
}

static const struct gui_layout_table fonts = {.load_font_data = load_font, .release_font_set = release_set};

static css_stylesheet *sheet(const char *text)
{
    css_stylesheet *s;
    css_stylesheet_params params = {.params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_DEFAULT,
        .charset = "UTF-8",
        .url = "https://example.test/css/main.css",
        .resolve = nscss_resolve_url};
    REQUIRE(css_stylesheet_create(&params, &s) == CSS_OK);
    css_error error = css_stylesheet_append_data(s, (const uint8_t *)text, strlen(text));
    REQUIRE(error == CSS_OK || error == CSS_NEEDDATA);
    REQUIRE(css_stylesheet_data_done(s) == CSS_OK);
    return s;
}

static void init(html_content *c, css_stylesheet *s)
{
    memset(c, 0, sizeof(*c));
    c->font_func = &fonts;
    c->media.type = CSS_MEDIA_SCREEN;
    c->unit_len_ctx.device_dpi = INTTOFIX(96);
    REQUIRE(nsurl_create("https://example.test/page", &c->base_url) == NSERROR_OK);
    REQUIRE(css_select_ctx_create(&c->select_ctx) == CSS_OK);
    REQUIRE(css_select_ctx_append_sheet(c->select_ctx, s, CSS_ORIGIN_AUTHOR, "screen") == CSS_OK);
    REQUIRE(html_font_face_init(c, c->select_ctx) == NSERROR_OK);
    c->font_wait_start_ms = 1;
}

static void fini(html_content *c)
{
    html_font_face_fini(c);
    css_select_ctx_destroy(c->select_ctx);
    nsurl_unref(c->base_url);
}

static void finish(unsigned index, llcache_event_type type)
{
    llcache_handle *h = &requests[index];
    llcache_event event = {.type = type};
    REQUIRE(h->callback != NULL && !h->released);
    REQUIRE(h->callback(h, &event, h->pw) == NSERROR_OK);
    REQUIRE(h->released);
}

int main(void)
{
    html_content a, b, shared, empty;
    css_stylesheet *sa = sheet("@font-face {font-family:Shared;src:url(../a.ttf)}");
    css_stylesheet *sb = sheet("@font-face {font-family:Shared;src:url(../b.ttf)}");
    css_stylesheet *se = sheet("body {color:red}");
    init(&a, sa);
    init(&b, sb);
    init(&shared, sa); /* Same cached stylesheet, separate document binding. */
    init(&empty, se);
    REQUIRE(request_count == 3);
    REQUIRE(a.font_set_id != b.font_set_id && a.font_set_id != shared.font_set_id);
    REQUIRE(!html_font_face_has_pending(&empty));
    REQUIRE(strcmp(requests[0].url, "https://example.test/a.ttf") == 0);
    REQUIRE(strcmp(requests[1].url, "https://example.test/b.ttf") == 0);
    REQUIRE(strcmp(requests[2].url, requests[0].url) == 0);
    REQUIRE(html_font_face_init(&a, a.select_ctx) == NSERROR_OK && request_count == 3);
    finish(1, LLCACHE_EVENT_DONE); /* Reverse completion must resume only B. */
    REQUIRE(resumed == &b && loaded_set == b.font_set_id);
    REQUIRE(html_font_face_has_pending(&a) && html_font_face_has_pending(&shared));
    uint64_t old_set = a.font_set_id;
    fini(&a);
    REQUIRE(requests[0].aborted && requests[0].released && requests[0].callback == NULL);
    REQUIRE(released_set == old_set && resumed == &b);
    finish(2, LLCACHE_EVENT_DONE); /* Cancelling A leaves shared's download alive. */
    REQUIRE(resumed == &shared && loaded_set == shared.font_set_id);
    fini(&b);
    fini(&shared);
    fini(&empty);
    REQUIRE(release_count == 4);

    unsigned start = request_count;
    css_stylesheet *grouped = sheet("@media print {@font-face {font-family:Shared;src:url(print.ttf)}}"
                                    "@media screen {@font-face {font-family:Shared;src:url(first.ttf),url(retry.ttf)}}"
                                    "@font-face {font-family:Shared;src:url(last.ttf)}");
    init(&a, grouped);
    REQUIRE(request_count == start + 2 && html_font_face_pending_count(&a) == 2);
    REQUIRE(strstr(requests[start].url, "first.ttf") != NULL);
    finish(start + 1, LLCACHE_EVENT_DONE);
    REQUIRE(loaded_order == 1 && html_font_face_has_pending(&a));
    finish(start, LLCACHE_EVENT_ERROR);
    REQUIRE(request_count == start + 3 && html_font_face_pending_count(&a) == 1);
    REQUIRE(strstr(requests[start + 2].url, "retry.ttf") != NULL);
    finish(start + 2, LLCACHE_EVENT_DONE);
    REQUIRE(loaded_order == 0 && resumed == &a);
    fini(&a);

    char many[5000] = "";
    for (unsigned i = 0; i < 40; i++) {
        char rule[100];
        snprintf(rule, sizeof(rule), "@font-face{font-family:Shared;src:url(font%u.ttf)}", i);
        strcat(many, rule);
    }
    css_stylesheet *sm = sheet(many);
    start = request_count;
    init(&a, sm);
    REQUIRE(html_font_face_pending_count(&a) == 40); /* No global 32-slot limit. */
    html_font_face_stop(&a);
    REQUIRE(!html_font_face_has_pending(&a));
    for (unsigned i = start; i < request_count; i++)
        REQUIRE(requests[i].released && requests[i].aborted);
    fini(&a);
    css_stylesheet_destroy(sa);
    css_stylesheet_destroy(sb);
    css_stylesheet_destroy(se);
    css_stylesheet_destroy(grouped);
    css_stylesheet_destroy(sm);
    REQUIRE(loaded_count == 4);
    puts("Document font ownership, shared sheets, completion, cancellation, src fallback and media passed.");
    return 0;
}
