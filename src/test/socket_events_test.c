/* SPDX-License-Identifier: GPL-2.0-only */
#include <wisp/utils/socket.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef WISP_TEST_GLIB
#include <glib.h>
#include "gtk/socket.h"
#include "gtk/schedule.h"
#define watch_socket nsgtk_socket_watch
#else
#define watch_socket wisp_socket_watch
#endif

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                                            \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

#ifdef _WIN32
typedef SOCKET test_socket;
#define BAD_SOCKET INVALID_SOCKET
#else
typedef int test_socket;
#define BAD_SOCKET (-1)
#endif

static void socket_pair(test_socket pair[2])
{
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    test_socket listener = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(listener != BAD_SOCKET);
    CHECK(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t size = sizeof(address);
    CHECK(getsockname(listener, (struct sockaddr *)&address, &size) == 0);
    CHECK(listen(listener, 1) == 0);
    pair[1] = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(pair[1] != BAD_SOCKET);
    CHECK(connect(pair[1], (struct sockaddr *)&address, size) == 0);
    pair[0] = accept(listener, NULL, NULL);
    CHECK(pair[0] != BAD_SOCKET);
    ns_close_socket(listener);
}

#ifdef WISP_TEST_GLIB
static gboolean wait_expired(gpointer unused)
{
    (void)unused;
    CHECK(!"socket readiness timed out");
    return G_SOURCE_REMOVE;
}
#endif

static void wait_ready(const char *phase)
{
    (void)phase;
#ifdef WISP_TEST_GLIB
    guint timeout = g_timeout_add(2000, wait_expired, NULL);
    CHECK(g_main_context_iteration(NULL, TRUE));
    g_source_remove(timeout);
#elif defined(_WIN32)
    DWORD result = WaitForSingleObject(wisp_socket_event(), 2000);
    if (result != WAIT_OBJECT_0)
        fprintf(stderr, "%s: wait returned %lu, Windows error %lu\n", phase, result, GetLastError());
    CHECK(result == WAIT_OBJECT_0);
    wisp_socket_dispatch();
#else
    fd_set read, write, error;
    int maxfd = wisp_socket_fdset(&read, &write, &error);
    struct timeval timeout = {2, 0};
    CHECK(select(maxfd + 1, &read, &write, &error, &timeout) > 0);
    wisp_socket_ready(&read, &write, &error);
#endif
}

static unsigned calls, received;
static void one_shot(intptr_t socket, unsigned events, void *pw)
{
    CHECK(pw == &calls);
    calls++;
    received = events;
    CHECK(watch_socket(socket, 0, NULL, NULL) == NSERROR_OK);
}

static test_socket victims[2];
static unsigned original_calls, replacement_calls;
static void replacement(intptr_t socket, unsigned events, void *pw)
{
    (void)pw;
    CHECK(events & GUI_SOCKET_READ);
    replacement_calls++;
    CHECK(watch_socket(socket, 0, NULL, NULL) == NSERROR_OK);
}

static void replace_other(intptr_t socket, unsigned events, void *pw)
{
    (void)pw;
    CHECK(events & GUI_SOCKET_READ);
    original_calls++;
    test_socket other = victims[socket == (intptr_t)victims[0] ? 1 : 0];
    CHECK(watch_socket(socket, 0, NULL, NULL) == NSERROR_OK);
    CHECK(watch_socket(other, 0, NULL, NULL) == NSERROR_OK);
    /* Reusing the same descriptor must discard its old readiness snapshot. */
    CHECK(watch_socket(other, GUI_SOCKET_READ, replacement, NULL) == NSERROR_OK);
}

#ifdef WISP_TEST_GLIB
static unsigned scheduled_calls;
static void scheduled(void *pw)
{
    CHECK(pw == &scheduled_calls);
    scheduled_calls++;
}

static int next_deadline(void)
{
    GMainContext *context = g_main_context_default();
    gint priority, timeout;
    CHECK(g_main_context_acquire(context));
    g_main_context_prepare(context, &priority);
    g_main_context_query(context, priority, &timeout, NULL, 0);
    g_main_context_release(context);
    return timeout;
}

static void check_schedule(void)
{
    CHECK(nsgtk_schedule(60000, scheduled, &scheduled_calls) == NSERROR_OK);
    CHECK(next_deadline() > 0);
    CHECK(nsgtk_schedule(-1, scheduled, &scheduled_calls) == NSERROR_OK);
    CHECK(next_deadline() == -1); /* Cancellation leaves no timeout wakeup. */
    CHECK(nsgtk_schedule(60000, scheduled, &scheduled_calls) == NSERROR_OK);
    CHECK(nsgtk_schedule(0, scheduled, &scheduled_calls) == NSERROR_OK);
    CHECK(scheduled_calls == 0); /* Never enter the engine synchronously. */
    CHECK(g_main_context_iteration(NULL, TRUE));
    CHECK(schedule_run());
    CHECK(scheduled_calls == 1 && next_deadline() == -1);
    CHECK(nsgtk_schedule(0, scheduled, &scheduled_calls) == NSERROR_OK);
    CHECK(g_main_context_iteration(NULL, TRUE));
    CHECK(nsgtk_schedule(-1, scheduled, &scheduled_calls) == NSERROR_OK);
    CHECK(schedule_run());
    CHECK(scheduled_calls == 1 && next_deadline() == -1);
}
#endif

int main(void)
{
#ifdef _WIN32
    WSADATA data;
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
#endif
    test_socket pair[2];
    socket_pair(pair);
    CHECK(watch_socket(pair[0], GUI_SOCKET_WRITE, one_shot, &calls) == NSERROR_OK);
    CHECK(calls == 0); /* Registration never invokes an engine callback. */
    wait_ready("initial write");
    CHECK(calls == 1 && (received & GUI_SOCKET_WRITE));
    CHECK(watch_socket(pair[0], GUI_SOCKET_READ, one_shot, &calls) == NSERROR_OK);
    CHECK(send(pair[1], "x", 1, 0) == 1);
    wait_ready("read after write-interest replacement");
    CHECK(calls == 2 && (received & GUI_SOCKET_READ));
    char ch;
    CHECK(recv(pair[0], &ch, 1, 0) == 1 && ch == 'x');
    CHECK(watch_socket(pair[0], GUI_SOCKET_READ, one_shot, &calls) == NSERROR_OK);
    ns_close_socket(pair[1]);
    wait_ready("peer close");
    CHECK(calls == 3 && (received & GUI_SOCKET_READ));
    CHECK(recv(pair[0], &ch, 1, 0) == 0);
    ns_close_socket(pair[0]);

    test_socket pairs[2][2];
    for (unsigned i = 0; i < 2; i++) {
        socket_pair(pairs[i]);
        victims[i] = pairs[i][0];
        CHECK(watch_socket(victims[i], GUI_SOCKET_READ, replace_other, NULL) == NSERROR_OK);
        CHECK(send(pairs[i][1], "y", 1, 0) == 1);
    }
    wait_ready("two ready watches");
    CHECK(original_calls == 1 && replacement_calls == 0);
    wait_ready("replacement watch");
    CHECK(original_calls == 1 && replacement_calls == 1);
    for (unsigned i = 0; i < 2; i++) {
        ns_close_socket(pairs[i][0]);
        ns_close_socket(pairs[i][1]);
    }

#ifdef _WIN32
    /* One event represents all sockets, rather than a 64-handle wait array. */
    test_socket many[70][2];
    calls = 0;
    for (unsigned i = 0; i < 70; i++) {
        socket_pair(many[i]);
        CHECK(watch_socket(many[i][0], GUI_SOCKET_READ, one_shot, &calls) == NSERROR_OK);
        CHECK(send(many[i][1], "z", 1, 0) == 1);
    }
    while (calls < 70)
        wait_ready("more than 64 sockets");
    CHECK(calls == 70);
    for (unsigned i = 0; i < 70; i++) {
        ns_close_socket(many[i][0]);
        ns_close_socket(many[i][1]);
    }
    CHECK(wisp_socket_event() == NULL);
    WSACleanup();
#elif defined(WISP_TEST_GLIB)
    CHECK(!g_main_context_pending(NULL));
    check_schedule();
#else
    fd_set read, write, error;
    CHECK(wisp_socket_fdset(&read, &write, &error) == -1);
#endif
    puts("Socket readiness, replacement, close and idle cleanup passed");
    return 0;
}
