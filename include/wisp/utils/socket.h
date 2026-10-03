/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_UTILS_SOCKET_H
#define WISP_UTILS_SOCKET_H

#include <wisp/utils/inet.h>
#include <wisp/fetch.h>

/* Native readiness adapter for frontends with their own wait loop. All calls
 * and callbacks belong to that frontend's engine thread. Sockets remain owned
 * by the caller; removing a watch suppresses its pending readiness snapshot. */
nserror wisp_socket_watch(intptr_t socket, unsigned events, gui_socket_callback callback, void *pw);

#ifdef _WIN32
/* A single Windows event covers every watched socket, with no wait-handle cap.
 * Reacquire it before each wait; NULL means there are no registered sockets. */
void *wisp_socket_event(void);
void wisp_socket_dispatch(void);
#else
/* These only prepare a blocking wait and dispatch its returned readiness.
 * They never call the fetcher to check for work or install a periodic timer. */
int wisp_socket_fdset(fd_set *read, fd_set *write, fd_set *error);
void wisp_socket_ready(const fd_set *read, const fd_set *write, const fd_set *error);
#endif

#endif
