/* SPDX-License-Identifier: GPL-2.0-only */
#include <glib.h>
#include <limits.h>
#include "gtk/socket.h"

struct socket_watch {
    struct socket_watch *next;
    intptr_t socket;
    guint source;
    gui_socket_callback callback;
    void *pw;
};

static struct socket_watch *watches;

static gboolean socket_ready(GIOChannel *channel, GIOCondition condition, gpointer data)
{
    struct socket_watch *watch = data;
    unsigned events = 0;
    (void)channel;
    if (condition & (G_IO_IN | G_IO_HUP))
        events |= GUI_SOCKET_READ;
    if (condition & G_IO_OUT)
        events |= GUI_SOCKET_WRITE;
    if (condition & (G_IO_ERR | G_IO_NVAL))
        events |= GUI_SOCKET_ERROR;
    watch->callback(watch->socket, events, watch->pw);
    /* The callback may have removed/replaced this source and freed watch. */
    return G_SOURCE_CONTINUE;
}

nserror nsgtk_socket_watch(intptr_t socket, unsigned events, gui_socket_callback callback, void *pw)
{
    struct socket_watch **link = &watches;
    while (*link != NULL && (*link)->socket != socket)
        link = &(*link)->next;
    if (events != 0 &&
        (socket < 0 || socket > INT_MAX || callback == NULL || (events & ~(GUI_SOCKET_READ | GUI_SOCKET_WRITE)) != 0))
        return NSERROR_BAD_PARAMETER;

    struct socket_watch *watch = NULL;
    if (events != 0) {
        watch = g_try_new0(struct socket_watch, 1);
        if (watch == NULL)
            return NSERROR_NOMEM;
    }
    if (*link != NULL) {
        struct socket_watch *old = *link;
        *link = old->next;
        /* Destroying the source also suppresses readiness already queued in
         * this main-context iteration, including descriptor reuse. */
        g_source_remove(old->source);
    }
    if (events == 0)
        return NSERROR_OK;

#ifdef G_OS_WIN32
    GIOChannel *channel = g_io_channel_win32_new_socket((gint)socket);
#else
    GIOChannel *channel = g_io_channel_unix_new((gint)socket);
#endif
    g_io_channel_set_close_on_unref(channel, FALSE);
    GIOCondition condition = G_IO_ERR | G_IO_HUP | G_IO_NVAL;
    if (events & GUI_SOCKET_READ)
        condition |= G_IO_IN;
    if (events & GUI_SOCKET_WRITE)
        condition |= G_IO_OUT;
    watch->socket = socket;
    watch->callback = callback;
    watch->pw = pw;
    watch->next = *link;
    *link = watch;
    watch->source = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, condition, socket_ready, watch, g_free);
    g_io_channel_unref(channel);
    return NSERROR_OK;
}
