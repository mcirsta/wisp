/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_QT_SOCKET_H
#define WISP_QT_SOCKET_H

extern "C" {
#include <wisp/fetch.h>
}

nserror nsqt_socket_watch(intptr_t socket, unsigned events, gui_socket_callback callback, void *pw);

#endif
