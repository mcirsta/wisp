/* SPDX-License-Identifier: GPL-2.0-only */
#include <QCoreApplication>
#include <QHash>
#include <QSocketNotifier>
#include <new>

#include "qt/socket.h"

struct SocketWatch {
    QSocketNotifier *read = nullptr;
    QSocketNotifier *write = nullptr;
};

static QHash<qintptr, SocketWatch> watches;

static void remove_notifier(QSocketNotifier *notifier)
{
    if (notifier == nullptr)
        return;
    // Removal may happen from the notifier's own signal. Disable/disconnect now
    // so closing and reusing the descriptor cannot deliver an old notification.
    notifier->setEnabled(false);
    notifier->disconnect();
    notifier->deleteLater();
}

nserror nsqt_socket_watch(intptr_t socket, unsigned events, gui_socket_callback callback, void *pw)
{
    SocketWatch old = watches.take(socket);
    remove_notifier(old.read);
    remove_notifier(old.write);
    if (events == 0)
        return NSERROR_OK;

    SocketWatch watch;
    auto create = [=](QSocketNotifier::Type type, unsigned event) {
        auto *notifier = new (std::nothrow) QSocketNotifier(socket, type, QCoreApplication::instance());
        if (notifier != nullptr) {
            QObject::connect(notifier, &QSocketNotifier::activated, notifier,
                [=](QSocketDescriptor, QSocketNotifier::Type) { callback(socket, event, pw); });
        }
        return notifier;
    };
    if (events & GUI_SOCKET_READ)
        watch.read = create(QSocketNotifier::Read, GUI_SOCKET_READ);
    if (events & GUI_SOCKET_WRITE)
        watch.write = create(QSocketNotifier::Write, GUI_SOCKET_WRITE);
    if (((events & GUI_SOCKET_READ) && watch.read == nullptr) ||
        ((events & GUI_SOCKET_WRITE) && watch.write == nullptr)) {
        remove_notifier(watch.read);
        remove_notifier(watch.write);
        return NSERROR_NOMEM;
    }
    watches.insert(socket, watch);
    return NSERROR_OK;
}
