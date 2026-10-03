/* SPDX-License-Identifier: GPL-2.0-only */
#include <wisp/utils/socket.h>
#include <stdlib.h>

struct socket_watch {
    struct socket_watch *next;
    intptr_t socket;
    unsigned events, ready;
    gui_socket_callback callback;
    void *pw;
};

static struct socket_watch *watches;
#ifdef _WIN32
static WSAEVENT wake_event = WSA_INVALID_EVENT;
#endif

nserror wisp_socket_watch(intptr_t socket, unsigned events, gui_socket_callback callback, void *pw)
{
    struct socket_watch **link = &watches;
    while (*link != NULL && (*link)->socket != socket)
        link = &(*link)->next;
    if (events == 0) {
        if (*link != NULL) {
            struct socket_watch *old = *link;
#ifdef _WIN32
            /* Curl may already have closed the socket. Unlink even then. */
            WSAEventSelect((SOCKET)socket, NULL, 0);
#endif
            *link = old->next;
            free(old);
        }
#ifdef _WIN32
        if (watches == NULL && wake_event != WSA_INVALID_EVENT) {
            WSACloseEvent(wake_event);
            wake_event = WSA_INVALID_EVENT;
        }
#endif
        return NSERROR_OK;
    }
    if (callback == NULL || (events & ~(GUI_SOCKET_READ | GUI_SOCKET_WRITE)) != 0)
        return NSERROR_BAD_PARAMETER;
#ifndef _WIN32
    if (socket < 0 || socket >= FD_SETSIZE)
        return NSERROR_BAD_PARAMETER;
#endif
    struct socket_watch *item = *link;
    if (item == NULL) {
        item = calloc(1, sizeof(*item));
        if (item == NULL)
            return NSERROR_NOMEM;
        item->socket = socket;
    }
#ifdef _WIN32
    if (wake_event == WSA_INVALID_EVENT)
        wake_event = WSACreateEvent();
    long interests = FD_CLOSE;
    if (events & GUI_SOCKET_READ)
        interests |= FD_READ;
    if (events & GUI_SOCKET_WRITE)
        interests |= FD_WRITE | FD_CONNECT;
    if (wake_event == WSA_INVALID_EVENT || WSAEventSelect((SOCKET)socket, wake_event, interests) == SOCKET_ERROR) {
        if (*link == NULL)
            free(item);
        if (watches == NULL && wake_event != WSA_INVALID_EVENT) {
            WSACloseEvent(wake_event);
            wake_event = WSA_INVALID_EVENT;
        }
        return NSERROR_INIT_FAILED;
    }
#endif
    *link = item;
    item->events = events;
    item->ready = 0;
    item->callback = callback;
    item->pw = pw;
    return NSERROR_OK;
}

static void dispatch_ready(void)
{
    for (;;) {
        struct socket_watch *item = watches;
        while (item != NULL && item->ready == 0)
            item = item->next;
        if (item == NULL)
            break;
        unsigned events = item->ready;
        item->ready = 0;
        item->callback(item->socket, events, item->pw);
        /* The callback can remove/replace any watch, including itself. */
    }
}

#ifdef _WIN32
void *wisp_socket_event(void)
{
    return wake_event;
}

void wisp_socket_dispatch(void)
{
    /* Reset the shared wakeup once, while retrieving the first socket's event
     * record. The other per-socket records survive that reset. Arrivals during
     * enumeration signal the event again; later enumerations must not clear it. */
    WSAEVENT reset = wake_event;
    for (struct socket_watch *item = watches; item != NULL; item = item->next) {
        WSANETWORKEVENTS events;
        item->ready = 0;
        if (WSAEnumNetworkEvents((SOCKET)item->socket, reset, &events) == SOCKET_ERROR) {
            item->ready = GUI_SOCKET_ERROR;
            continue;
        }
        reset = NULL;
        if (events.lNetworkEvents & (FD_READ | FD_CLOSE))
            item->ready |= GUI_SOCKET_READ;
        if (events.lNetworkEvents & (FD_WRITE | FD_CONNECT))
            item->ready |= GUI_SOCKET_WRITE;
        for (int bit = 0; bit < FD_MAX_EVENTS; bit++) {
            if ((events.lNetworkEvents & (1L << bit)) && events.iErrorCode[bit] != 0)
                item->ready |= GUI_SOCKET_ERROR;
        }
    }
    dispatch_ready();
}
#else
int wisp_socket_fdset(fd_set *read, fd_set *write, fd_set *error)
{
    int maxfd = -1;
    FD_ZERO(read);
    FD_ZERO(write);
    FD_ZERO(error);
    for (struct socket_watch *item = watches; item != NULL; item = item->next) {
        if (item->events & GUI_SOCKET_READ)
            FD_SET(item->socket, read);
        if (item->events & GUI_SOCKET_WRITE)
            FD_SET(item->socket, write);
        FD_SET(item->socket, error);
        if (item->socket > maxfd)
            maxfd = item->socket;
    }
    return maxfd;
}

void wisp_socket_ready(const fd_set *read, const fd_set *write, const fd_set *error)
{
    /* Snapshot before calling the engine. Replacements clear their snapshot,
     * including a newly registered watch reusing a just-closed descriptor. */
    for (struct socket_watch *item = watches; item != NULL; item = item->next) {
        item->ready = (FD_ISSET(item->socket, read) ? GUI_SOCKET_READ : 0) |
            (FD_ISSET(item->socket, write) ? GUI_SOCKET_WRITE : 0) |
            (FD_ISSET(item->socket, error) ? GUI_SOCKET_ERROR : 0);
    }
    dispatch_ready();
}
#endif
