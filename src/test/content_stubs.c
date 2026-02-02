#include <libwapcaplet/libwapcaplet.h>
#include <wisp/content.h>
#include <wisp/content/content_protected.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/utils/errors.h>
#include <wisp/utils/nsurl.h>
#include "utils/nsurl/private.h"
#include <stdlib.h>
#include <string.h>

static struct nsurl *stub_url;

/* Stub guit for tests - layout is NULL so svg.c will use approximation */
static struct wisp_table stub_gui_table = {0};
struct wisp_table *guit = &stub_gui_table;

nserror content__init(struct content *c, const struct content_handler *handler, lwc_string *imime_type,
    const struct http_parameter *params, struct llcache_handle *llcache, const char *fallback_charset, bool quirks)
{
    (void)imime_type;
    (void)params;
    (void)llcache;
    (void)fallback_charset;
    c->handler = handler;
    c->quirks = quirks;
    c->locked = true;
    c->status = CONTENT_STATUS_LOADING;
    return NSERROR_OK;
}

nserror content__clone(const struct content *old, struct content *nc)
{
    memcpy(nc, old, sizeof(struct content));
    return NSERROR_OK;
}

void content_destroy(struct content *c)
{
    (void)c;
}

void content_set_ready(struct content *c)
{
    c->locked = false;
    c->status = CONTENT_STATUS_READY;
}

void content_set_done(struct content *c)
{
    c->status = CONTENT_STATUS_DONE;
}

void content_set_status(struct content *c, const char *status_message)
{
    if (status_message != NULL) {
        size_t l = strlen(status_message);
        if (l >= sizeof(c->status_message))
            l = sizeof(c->status_message) - 1;
        memcpy(c->status_message, status_message, l);
        c->status_message[l] = '\0';
    }
}

void content_broadcast_error(struct content *c, nserror errorcode, const char *msg)
{
    (void)c;
    (void)errorcode;
    (void)msg;
}

const uint8_t *content__get_source_data(struct content *c, size_t *size)
{
    (void)c;
    if (size)
        *size = 0;
    return NULL;
}

struct nsurl *content_get_url(struct content *c)
{
    (void)c;
    if (stub_url == NULL) {
        const char *s = "about:blank";
        size_t len = strlen(s);
        struct nsurl *u = malloc(sizeof(struct nsurl) + len + 1);
        if (u == NULL)
            return NULL;
        memset(u, 0, sizeof(struct nsurl));
        u->length = len;
        memcpy(u->string, s, len + 1);
        u->count = 1;
        stub_url = u;
    }
    return stub_url;
}

nserror content_factory_register_handler(const char *mime_type, const struct content_handler *handler)
{
    (void)mime_type;
    (void)handler;
    return NSERROR_OK;
}

const char *nsurl_access(const struct nsurl *url)
{
    if (url == NULL)
        return "";
    return url->string;
}
