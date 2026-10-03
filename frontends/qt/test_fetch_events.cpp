/* SPDX-License-Identifier: GPL-2.0-only */
/* Production fetch core, curl backend and Qt socket adapter. Only scheduling and
 * resource lookup are supplied by this small, window-free event-loop harness. */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unistd.h>

extern "C" {
#include <wisp/utils/inet.h>
#include <wisp/content/fetch.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/fetch.h>
#include <wisp/misc.h>
#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsoption.h>
#include <wisp/utils/nsurl.h>
#include "content/fetchers.h"
#include "content/fetchers/curl.h"
#include "content/fetchers/data.h"
}
#include "qt/socket.h"

#define REQUIRE(expr)                                                                                                  \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr);                                                    \
            std::exit(EXIT_FAILURE);                                                                                   \
        }                                                                                                              \
    } while (0)

using TimerKey = std::pair<void (*)(void *), void *>;
static std::map<TimerKey, QTimer *> timers;
static std::set<intptr_t> watched;
static unsigned timer_calls, socket_calls;
static bool reject_watch;
static std::string origin;

static nserror schedule(int delay, void (*callback)(void *), void *pw)
{
    TimerKey key(callback, pw);
    auto found = timers.find(key);
    if (found != timers.end()) {
        found->second->stop();
        delete found->second;
        timers.erase(found);
    }
    if (delay < 0)
        return NSERROR_OK;
    auto *timer = new QTimer;
    timer->setSingleShot(true);
    timers[key] = timer;
    QObject::connect(timer, &QTimer::timeout, [=] {
        timers.erase(key);
        timer->deleteLater();
        timer_calls++;
        callback(pw);
    });
    timer->start(delay);
    return NSERROR_OK;
}

static void ready(intptr_t socket, unsigned events, void *pw)
{
    socket_calls++;
    reinterpret_cast<gui_socket_callback>(pw)(socket, events, nullptr);
}

static nserror watch(intptr_t socket, unsigned events, gui_socket_callback callback, void *pw)
{
    REQUIRE(pw == nullptr);
    if (events && reject_watch) {
        reject_watch = false;
        return NSERROR_NOMEM;
    }
    if (events)
        watched.insert(socket);
    else
        watched.erase(socket);
    return nsqt_socket_watch(socket, events, ready, reinterpret_cast<void *>(callback));
}

static void wait_until(const std::function<bool()> &done, int timeout = 4000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!done() && elapsed.elapsed() < timeout)
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
    REQUIRE(done());
}

static void wait_ms(int delay)
{
    QEventLoop loop;
    QTimer::singleShot(delay, &loop, &QEventLoop::quit);
    loop.exec();
}

struct Request {
    fetch *handle = nullptr;
    bool done = false, has_data = false, aborted = false;
    fetch_msg_type result = FETCH_PROGRESS;
    std::string body;
    std::function<void(const fetch_msg *)> on_message;
};

static void start(Request &request, const std::string &url)
{
    nsurl *address;
    const char *headers[] = {nullptr};
    const fetch_postdata postdata = {FETCH_POSTDATA_NONE, {nullptr}};
    REQUIRE(nsurl_create(url.c_str(), &address) == NSERROR_OK);
    REQUIRE(fetch_start(
                address, nullptr,
                [](const fetch_msg *message, void *pw) {
                    auto &request = *static_cast<Request *>(pw);
                    REQUIRE(!request.aborted);
                    if (message->type == FETCH_DATA) {
                        request.has_data = true;
                        request.body.append(reinterpret_cast<const char *>(message->data.header_or_data.buf),
                            message->data.header_or_data.len);
                    }
                    if (message->type >= FETCH_MIN_FINISHED_MSG) {
                        request.done = true;
                        request.handle = nullptr;
                        request.result = message->type;
                    }
                    if (request.on_message)
                        request.on_message(message);
                },
                &request, true, &postdata, true, false, headers, &request.handle) == NSERROR_OK);
    nsurl_unref(address);
}

static void abort_request(Request &request)
{
    REQUIRE(request.handle != nullptr);
    fetch_abort(request.handle);
    request.handle = nullptr;
    request.aborted = request.done = true;
}

static void check_socket_reuse()
{
    int old[2], replacement[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, old) == 0);
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, replacement) == 0);
    unsigned calls = 0;
    auto callback = [](intptr_t fd, unsigned, void *pw) {
        ++*static_cast<unsigned *>(pw);
        REQUIRE(nsqt_socket_watch(fd, 0, nullptr, nullptr) == NSERROR_OK);
    };
    REQUIRE(nsqt_socket_watch(old[0], GUI_SOCKET_READ, callback, &calls) == NSERROR_OK);
    REQUIRE(write(old[1], "x", 1) == 1);
    REQUIRE(nsqt_socket_watch(old[0], 0, nullptr, nullptr) == NSERROR_OK);
    close(old[0]);
    REQUIRE(dup2(replacement[0], old[0]) == old[0]);
    close(replacement[0]);
    REQUIRE(nsqt_socket_watch(old[0], GUI_SOCKET_READ, callback, &calls) == NSERROR_OK);
    wait_ms(20);
    REQUIRE(calls == 0); // Old readiness cannot reach the replacement watch.
    REQUIRE(write(replacement[1], "y", 1) == 1);
    wait_until([&] { return calls == 1; });
    wait_ms(20);
    REQUIRE(calls == 1); // Callback removal also suppresses level-triggered repeats.
    close(old[0]);
    close(old[1]);
    close(replacement[1]);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    REQUIRE(argc == 3);
    check_socket_reuse();
    origin = argv[1];
    gui_misc_table misc = {};
    misc.schedule = schedule;
    gui_fetch_table frontend = {};
    frontend.filetype = [](const char *) { return "text/plain"; };
    frontend.mimetype = [](const char *) { return strdup("text/plain"); };
    frontend.socket_open = [](int domain, int type, int protocol) -> intptr_t { return socket(domain, type, protocol); };
    frontend.socket_close = [](intptr_t fd) { return close(fd); };
    frontend.socket_watch = watch;
    wisp_table table = {};
    table.misc = &misc;
    table.fetch = &frontend;
    guit = &table;
    REQUIRE(nsoption_init(nullptr, nullptr, nullptr) == NSERROR_OK);
    nsoption_set_int(max_fetchers, 2);
    nsoption_set_int(max_fetchers_per_host, 2);
    nsoption_set_uint(curl_fetch_timeout, 1);
    REQUIRE(corestrings_init() == NSERROR_OK);
    // Register only the schemes exercised here; the browser integration test
    // covers file/resource/CSS fetching through the complete frontend table.
    REQUIRE(fetch_curl_register() == NSERROR_OK);
    REQUIRE(fetch_data_register() == NSERROR_OK);

    Request parallel[8];
    for (auto &request : parallel)
        start(request, origin + "/ok");
    wait_until([&] {
        for (auto &request : parallel)
            if (!request.done)
                return false;
        return true;
    });
    for (auto &request : parallel) {
        REQUIRE(request.result == FETCH_FINISHED);
        REQUIRE(request.body == "ready");
    }

    // Starting another transfer from curl's completion callback must dispatch
    // after curl unwinds, even when no other transfer can wake the event loop.
    Request chain[12];
    for (int i = 0; i < 11; i++) {
        chain[i].on_message = [&, i](const fetch_msg *message) {
            if (message->type == FETCH_FINISHED)
                start(chain[i + 1], origin + "/ok");
        };
    }
    start(chain[0], origin + "/ok");
    wait_until([&] { return chain[11].done; });

    Request stalled, trigger, local;
    start(stalled, origin + "/stall");
    wait_until([&] { return stalled.has_data; });
    unsigned before = timer_calls;
    wait_ms(250);
    REQUIRE(timer_calls - before < 8); // Detect a recurring 10 ms network poll.
    start(local, "data:text/plain,local");
    wait_until([&] { return local.done; });
    REQUIRE(local.body == "local");
    trigger.on_message = [&](const fetch_msg *message) {
        if (message->type == FETCH_DATA)
            abort_request(stalled);
    };
    start(trigger, origin + "/ok");
    wait_until([&] { return trigger.done && timers.empty(); });
    REQUIRE(watched.empty()); // Aborted idle request cleaned without another packet.

    Request self;
    self.on_message = [&](const fetch_msg *message) {
        if (message->type == FETCH_DATA)
            abort_request(self);
    };
    start(self, origin + "/stall");
    wait_until([&] { return self.done && timers.empty(); });
    REQUIRE(watched.empty());

    Request occupied[2], queued;
    for (auto &request : occupied)
        start(request, origin + "/stall");
    wait_until([&] { return occupied[0].has_data && occupied[1].has_data; });
    start(queued, origin + "/ok");
    abort_request(queued);
    for (auto &request : occupied)
        abort_request(request);
    wait_until([&] { return timers.empty(); });
    REQUIRE(watched.empty());

    Request failed;
    start(failed, origin + "/drop");
    wait_until([&] { return failed.done; });
    REQUIRE(failed.result == FETCH_ERROR);
    // Failure to register readiness must fail the transfer, not leave the core
    // busy forever. The multi handle must accept subsequent requests again.
    Request watch_failure, recovered;
    reject_watch = true;
    start(watch_failure, origin + "/ok");
    wait_until([&] { return watch_failure.done; });
    REQUIRE(watch_failure.result == FETCH_ERROR && !reject_watch);
    start(recovered, origin + "/ok");
    wait_until([&] { return recovered.done; });
    REQUIRE(recovered.result == FETCH_FINISHED);
    Request timeout;
    start(timeout, argv[2]); // TCP connection accepts but never completes TLS.
    wait_until([&] { return timeout.done; });
    REQUIRE(timeout.result == FETCH_TIMEDOUT);

    wait_until([&] { return timers.empty(); });
    REQUIRE(watched.empty());
    before = timer_calls + socket_calls;
    wait_ms(100);
    REQUIRE(before == timer_calls + socket_calls);
    fetcher_quit();
    REQUIRE(timers.empty() && watched.empty());
    REQUIRE(corestrings_fini() == NSERROR_OK);
    nsoption_finalise(nullptr, nullptr);
    std::puts("Fetch events: queue, callback starts, local work, aborts, errors, TLS deadline and idle passed.");
    return EXIT_SUCCESS;
}
