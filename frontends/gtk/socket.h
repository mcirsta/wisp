/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_GTK_SOCKET_H
#define WISP_GTK_SOCKET_H

#include <wisp/fetch.h>

nserror nsgtk_socket_watch(intptr_t socket, unsigned events, gui_socket_callback callback, void *pw);

#endif
