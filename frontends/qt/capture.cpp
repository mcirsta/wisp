/* SPDX-License-Identifier: GPL-2.0-only */
#include "qt/capture.h"
#include "qt/capture_inspect.h"
#include "qt/widget.cls.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFontInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QRegion>
#include <QSaveFile>
#include <QSettings>
#include <QScreen>
#include <QWidget>

extern "C" {
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "wisp/browser.h"
#include "wisp/browser_window.h"
#include "wisp/keypress.h"
#include "wisp/content.h"
#include "wisp/content/content.h"
#include "wisp/content/hlcache.h"
}

NS_Capture *NS_Capture::s_active = nullptr;

/* Viewport rectangles, separated by semicolons, for native damage tests. */
static QRegion repaintRegion(const QString &value, bool *ok)
{
    QRegion region;
    *ok = false;
    for (const QString &item : value.split(';')) {
        const auto parts = item.split(',');
        if (parts.size() != 4)
            return {};
        int pixels[4];
        for (int i = 0; i < 4; ++i) {
            bool number;
            pixels[i] = parts[i].toInt(&number);
            if (!number || pixels[i] < (i < 2 ? 0 : 1) || pixels[i] > 8192)
                return {};
        }
        region += QRect(pixels[0], pixels[1], pixels[2], pixels[3]);
    }
    *ok = !region.isEmpty();
    return region;
}

NS_Capture *NS_Capture::active()
{
    return s_active;
}

QString NS_Capture::profilePath() const
{
    return m_profile->path();
}

int NS_Capture::prepare(int &argc, char **argv)
{
    int remaining = 1;
    bool requested = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--capture-help") {
            fprintf(stdout,
                "Usage: wisp-qt --capture-dir DIR [capture options] [core options] URL-or-file\n"
                "  --capture-size WIDTHxHEIGHT  Content viewport (default 1280x900)\n"
                "  --capture-resize WIDTHxHEIGHT Resize after settling (repeatable action)\n"
                "  --capture-settle MS          Quiet period after load (default 500)\n"
                "  --capture-timeout MS         Overall deadline (default 30000)\n"
                "  --capture-text-cache-bytes N Text cache budget (0 disables caching for comparison)\n"
                "  --capture-bitmap-cache-bytes N Scaled bitmap cache budget (0 disables caching)\n"
                "  --capture-no-scroll-blit     Repaint the full viewport for scroll comparisons\n"
                "  --capture-repaint X,Y,W,H[;...] Repaint viewport rectangles after settling\n"
                "  --capture-screen            Grab displayed pixels (requires screen capture support)\n"
                "  --capture-id ID              Inspect an element (repeatable)\n"
                "  --capture-click-id ID        Click an element after settling (repeatable)\n"
                "  --capture-click-label FOR    Click a label associated with this input ID\n"
                "  --capture-click-point X,Y    Click viewport CSS pixels without auto-scrolling\n"
                "  --capture-scroll-point X,Y,DX,DY Scroll content/frame under viewport CSS pixels\n"
                "  --capture-drag X1,Y1,X2,Y2   Drag between viewport CSS pixel positions\n"
                "  --capture-type TEXT           Type into the focused control\n"
                "  --capture-scroll X,Y          Scroll before the next action\n"
                "  --capture-scroll-element ID,X,Y Set an element's existing scrollbar offsets\n"
                "  --capture-point X,Y          Inspect a point in document layout pixels\n"
                "  --capture-force-full-restyle Force the full-refresh reference path\n"
                "  --capture-css-only-restyle   Reuse CSS but reconstruct all boxes\n"
                "  --capture-no-intrinsic-reuse Remeasure all retained inline containers\n"
                "  --capture-no-inline-layout-reuse Reflow all retained text lines\n"
                "  --capture-reuse-inline-layout Use default completed text-only lines\n"
                "  --capture-reuse-inline-intrinsics Use default text-only intrinsic reuse\n"
                "  --capture-retain-boxes       Use default eligible retained-region refresh\n"
                "  --capture-fail-retained-layout Test error handling after region publication\n"
                "  --capture-restart-partial-restyle Test invalidation during partial construction\n"
                "  --capture-interrupt-restyle MODE Test close, navigate, shutdown or shutdown-full\n"
                "DIR must be new or empty. Uses a temporary profile, 96 DPI, JS and animation off\n"
                "by default. Core options such as --enable_javascript=1 can override defaults.\n"
                "Writes page.png, dom.txt, boxes.txt and capture.json, then exits.\n"
                "Exit: 0 complete, 1 timeout/load/output failure, 2 invalid arguments/init failure.\n");
            return 1;
        }
        if (!arg.startsWith("--capture-")) {
            argv[remaining++] = argv[i];
            continue;
        }
        requested = true;
        if (arg == "--capture-no-scroll-blit") {
            nsqt_capture_disable_scroll_blit = true;
            continue;
        }
        if (arg == "--capture-screen") {
            m_screen_capture = true;
            continue;
        }
        if (arg == "--capture-fail-retained-layout") {
#ifdef WISP_PROFILE_RESTYLE
            m_fail_retained_layout = true;
            m_css_only_restyle = false;
            continue;
#else
            fprintf(stderr, "Retained failure injection requires restyle profiling\n");
            return -1;
#endif
        }
        if (arg == "--capture-retain-boxes") {
            m_css_only_restyle = false;
            continue;
        }
        if (arg == "--capture-css-only-restyle") {
            m_css_only_restyle = true;
            continue;
        }
        if (arg == "--capture-reuse-inline-layout" || arg == "--capture-no-inline-layout-reuse") {
            m_no_inline_layout_reuse = arg == "--capture-no-inline-layout-reuse";
            continue;
        }
        if (arg == "--capture-reuse-inline-intrinsics" || arg == "--capture-no-intrinsic-reuse") {
            m_no_intrinsic_reuse = arg == "--capture-no-intrinsic-reuse";
            continue;
        }
        if (arg == "--capture-restart-partial-restyle") {
            m_restart_partial_restyle = true;
            continue;
        }
        if (arg == "--capture-force-full-restyle") {
            m_force_full_restyle = true;
            continue;
        }
        if (++i == argc) {
            fprintf(stderr, "Missing value for %s\n", qPrintable(arg));
            return -1;
        }
        const QString value = QString::fromLocal8Bit(argv[i]);
        bool ok = true;
        if (arg == "--capture-dir") {
            m_directory = value;
            ok = !value.isEmpty();
        } else if (arg == "--capture-text-cache-bytes") {
            const int bytes = value.toInt(&ok);
            ok = ok && bytes >= 0;
            if (ok)
                nsqt_capture_text_cache_limit(bytes);
        } else if (arg == "--capture-bitmap-cache-bytes") {
            const int bytes = value.toInt(&ok);
            ok = ok && bytes >= 0;
            if (ok)
                nsqt_capture_bitmap_cache_limit(bytes);
        } else if (arg == "--capture-interrupt-restyle") {
#ifdef WISP_PROFILE_RESTYLE
            ok = value == "close" || value == "navigate" || value == "shutdown" || value == "shutdown-full";
            m_interrupt_restyle = value;
            if (value == "shutdown-full")
                m_force_full_restyle = true;
#else
            ok = false;
#endif
        } else if (arg == "--capture-id") {
            m_inspect_ids.append(value);
            ok = !value.isEmpty();
        } else if (arg == "--capture-repaint") {
            repaintRegion(value, &ok);
            if (ok)
                m_actions.append({arg, value});
        } else if (arg == "--capture-resize") {
            const auto parts = value.split('x');
            const int width = parts.value(0).toInt(), height = parts.value(1).toInt();
            ok = parts.size() == 2 && width > 0 && width <= 8192 && height > 0 && height <= 8192;
            if (ok)
                m_actions.append({arg, value});
        } else if (arg == "--capture-scroll-point" || arg == "--capture-drag") {
            const auto parts = value.split(',');
            ok = parts.size() == 4;
            for (int part = 0; part < parts.size(); ++part) {
                bool number;
                const int pixels = parts[part].toInt(&number);
                ok = ok && number && ((arg == "--capture-scroll-point" && part >= 2) || pixels >= 0);
            }
            if (ok)
                m_actions.append({arg, value});
        } else if (arg == "--capture-click-id" || arg == "--capture-click-label" || arg == "--capture-type" ||
            arg == "--capture-scroll" || arg == "--capture-scroll-element" || arg == "--capture-click-point") {
            m_actions.append({arg, value});
            ok = !value.isEmpty();
            if (arg == "--capture-scroll" || arg == "--capture-scroll-element" || arg == "--capture-click-point") {
                const auto parts = value.split(',');
                const int first = arg == "--capture-scroll-element" ? 1 : 0;
                bool x_ok, y_ok;
                const int x = parts.value(first).toInt(&x_ok), y = parts.value(first + 1).toInt(&y_ok);
                ok = parts.size() == first + 2 && !parts.value(0).isEmpty() && x_ok && y_ok && x >= 0 && y >= 0;
            }
        } else if (arg == "--capture-point") {
            const auto parts = value.split(',');
            bool x_ok, y_ok;
            const int x = parts.value(0).toInt(&x_ok);
            const int y = parts.value(1).toInt(&y_ok);
            ok = parts.size() == 2 && x_ok && y_ok && x >= 0 && y >= 0;
            m_inspect_point = QPoint(x, y);
        } else if (arg == "--capture-size") {
            const auto parts = value.split('x');
            const int width = parts.value(0).toInt();
            const int height = parts.value(1).toInt();
            ok = parts.size() == 2 && width > 0 && width <= 8192 && height > 0 && height <= 8192;
            m_size = QSize(width, height);
        } else if (arg == "--capture-timeout" || arg == "--capture-settle") {
            const int ms = value.toInt(&ok);
            ok = ok && ms >= 50 && ms <= 600000;
            (arg == "--capture-timeout" ? m_timeout_ms : m_settle_ms) = ms;
        } else {
            ok = false;
        }
        if (!ok) {
            fprintf(stderr, "Invalid capture option: %s %s\n", qPrintable(arg), qPrintable(value));
            return -1;
        }
    }
    argc = remaining;
    argv[argc] = nullptr;
    if (!requested) {
        return 0;
    }
    if (m_directory.isEmpty() || argc < 2 || argv[argc - 1][0] == '-') {
        fprintf(stderr, "Capture requires --capture-dir DIR and a URL or file as the last argument\n");
        return -1;
    }
    for (int i = 1; i < argc - 1; ++i) {
        if (argv[i][0] != '-') {
            fprintf(stderr, "Capture accepts one URL/file; use --option=value for core options\n");
            return -1;
        }
    }
    m_requested_url = QString::fromLocal8Bit(argv[argc - 1]);
    QDir output(m_directory);
    if ((output.exists() &&
            !output.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty()) ||
        !output.mkpath(".")) {
        fprintf(stderr, "Capture directory must be writable and new or empty: %s\n", qPrintable(m_directory));
        return -1;
    }
    m_directory = output.absolutePath();
    m_profile = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/wisp-capture-XXXXXX");
    if (!m_profile->isValid()) {
        fprintf(stderr, "Could not create a temporary capture profile\n");
        return -1;
    }
    // Includes scaffold geometry as well as browser options, on every Qt platform.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profilePath());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profilePath());
    s_active = this;
    if (!m_interrupt_restyle.isEmpty())
        QApplication::setQuitOnLastWindowClosed(false);
    return 0;
}

void NS_Capture::attach(struct browser_window *bw, QWidget *widget)
{
    if (m_bw != nullptr || m_finished) {
        return;
    }
    m_bw = bw;
    m_widget = widget;
    widget->setFixedSize(m_size);
    widget->installEventFilter(this);
    m_elapsed.start();
    connect(&m_timer, &QTimer::timeout, this, &NS_Capture::poll);
    m_timer.start(50);
}

void NS_Capture::detach(struct browser_window *bw)
{
    if (bw == m_bw) {
        m_timer.stop();
        m_bw = nullptr;
        m_widget = nullptr;
    }
}

void NS_Capture::activity(struct browser_window *bw)
{
    if (bw == m_bw && !m_finished) {
        m_last_activity = m_elapsed.elapsed();
    }
}

void NS_Capture::windowEvent(struct browser_window *bw, enum gui_window_event event)
{
    if (bw != m_bw) {
        return;
    }
    if (event == GW_EVENT_START_THROBBER || event == GW_EVENT_STOP_THROBBER) {
        // The core aggregates loading state across child frames for these events.
        m_loading = event == GW_EVENT_START_THROBBER;
        activity(bw);
    } else if (event == GW_EVENT_UPDATE_EXTENT) {
        activity(bw);
    }
}

bool NS_Capture::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_widget && (event->type() == QEvent::Paint || event->type() == QEvent::Resize)) {
        activity(m_bw);
    }
    return false;
}

void NS_Capture::poll()
{
    if (m_screen_capture && m_widget) {
        // Scrollbars can appear after loading. Keep the fixed viewport clear
        // of the surrounding controls when capturing actual displayed pixels.
        QWidget *window = m_widget->window();
        QSize needed = window->size().expandedTo(window->minimumSizeHint());
        if (window->size() != needed) {
            window->resize(needed);
            activity(m_bw);
            return;
        }
    }
    if (m_interrupted) {
        if (nsqt_capture_restyle_lifecycle(m_interrupted, false)["settled"].toBool() ||
            m_elapsed.elapsed() >= m_timeout_ms)
            finishInterruption(m_elapsed.elapsed() >= m_timeout_ms);
        return;
    }
    if (!m_widget || m_bw == nullptr) {
        return;
    }
    auto *content = browser_window_get_content(m_bw);
    if (m_fail_retained_layout && m_failure_watch == nullptr && content)
        m_failure_watch = nsqt_capture_watch_content(content);
    if (m_failure_watch && nsqt_capture_restyle_lifecycle(m_failure_watch, false)["error"].toBool()) {
        finish("content_error");
        return;
    }
    nsqt_capture_configure_restyle(content, m_force_full_restyle, m_restart_partial_restyle,
        m_css_only_restyle, m_fail_retained_layout, m_no_intrinsic_reuse, m_no_inline_layout_reuse);
    if (!m_interrupt_restyle.isEmpty())
        nsqt_capture_restyle_lifecycle(hlcache_handle_get_content(content), true);
    const bool done = content && content_get_status(content) == CONTENT_STATUS_DONE;
    if (content && content_get_status(content) == CONTENT_STATUS_ERROR) {
        finish("content_error");
    } else if (m_elapsed.elapsed() >= m_timeout_ms) {
        finish("timeout");
    } else if (done && !m_loading && browser_window_redraw_ready(m_bw) && nsqt_capture_settled(content) &&
        m_elapsed.elapsed() - m_last_activity >= m_settle_ms) {
        const QString url = QString::fromUtf8(nsurl_access(hlcache_handle_get_url(content)));
        if (!m_actions.isEmpty()) {
            QJsonObject step = nsqt_capture_inspect(content, m_inspect_ids, m_inspect_point);
            int sx, sy;
            browser_window_get_scroll(m_bw, &sx, &sy);
            step["scroll"] = QJsonObject{{"x", sx}, {"y", sy}};
            step["elapsed_ms"] = m_elapsed.elapsed();
            step["widget_paint"] = static_cast<NS_Widget *>(m_widget.data())->capturePaintStats();
            step["bitmap_cache"] = nsqt_capture_bitmap_cache_stats();
            step["action_ms"] = m_last_action_ms;
            m_steps.append(step);
        }
        if (m_action_index < m_actions.size()) {
            const auto action = m_actions.at(m_action_index++);
            QElapsedTimer action_timer;
            action_timer.start();
            bool ok = true;
            if (action.first == "--capture-resize") {
                const auto parts = action.second.split('x');
                m_size = QSize(parts[0].toInt(), parts[1].toInt());
                // Exercise the widget's normal scheduled reformat path.
                m_widget->setFixedSize(m_size);
            } else if (action.first == "--capture-repaint") {
                m_widget->update(repaintRegion(action.second, &ok));
            } else if (action.first == "--capture-type") {
                for (uint32_t key : action.second.toUcs4())
                    ok = browser_window_key_press(m_bw, key) && ok;
            } else if (action.first == "--capture-scroll") {
                const auto xy = action.second.split(',');
                nsqt_capture_scroll(content, xy[0].toInt(), xy[1].toInt());
            } else if (action.first == "--capture-scroll-element") {
                const auto parts = action.second.split(',');
                ok = nsqt_capture_scroll_element(content, parts[0], parts[1].toInt(), parts[2].toInt());
            } else if (action.first == "--capture-click-point") {
                const auto xy = action.second.split(',');
                nsqt_capture_click_point(m_bw, xy[0].toInt(), xy[1].toInt());
            } else if (action.first == "--capture-scroll-point") {
                const auto parts = action.second.split(',');
                ok = nsqt_capture_scroll_point(
                    m_bw, parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
            } else if (action.first == "--capture-drag") {
                const auto parts = action.second.split(',');
                nsqt_capture_drag(m_bw, parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
            } else {
                ok = nsqt_capture_click(m_bw, action.second, action.first == "--capture-click-label");
            }
            m_last_action_ms = action_timer.nsecsElapsed() / 1e6;
            activity(m_bw);
            if (!ok)
                finish("action_error");
        } else {
            finish(!m_interrupt_restyle.isEmpty()    ? "interruption_not_reached"
                    : url.startsWith("about:query/") ? "load_error"
                                                     : "complete");
        }
    }
}

/* Called only after the renderer's callback has returned to the scheduler.
 * The slow capture slices make real window/navigation events deterministic;
 * never close a window reentrantly from inside box construction. */
void NS_Capture::afterScheduledCallback()
{
    if (m_interrupt_restyle.isEmpty() || m_finished)
        return;
    if (!m_interrupted && m_bw) {
        auto *content = browser_window_get_content(m_bw);
        const auto state = nsqt_capture_restyle_lifecycle(hlcache_handle_get_content(content), true);
        const bool started = m_interrupt_restyle == "shutdown-full"
            ? state["building"].toBool() && state["styled_elements"].toInt() > 0
            : !m_css_only_restyle
            ? state["retained_build"].toBool() && state["styled_elements"].toInt() > 0
            : state["partial"].toBool() && state["reused_styles"].toInt() > 0;
        if (!started || !state["conversion"].toBool())
            return;
        m_interrupted = nsqt_capture_watch_content(content);
        if (!m_interrupted) {
            finish("interruption_error");
            return;
        }
        m_before_interruption = state;
        if (m_interrupt_restyle != "navigate") {
            m_widget->window()->close();
            // detach() stopped the ordinary capture timer; watch cached content.
            m_timer.start(50);
        } else {
            struct nsurl *url = nullptr;
            nserror error = nsurl_create("about:blank", &url);
            if (error == NSERROR_OK) {
                error = browser_window_navigate(m_bw, url, nullptr, BW_NAVIGATE_HISTORY, nullptr, nullptr, nullptr);
                nsurl_unref(url);
            }
            if (error != NSERROR_OK) {
                finishInterruption(true);
                return;
            }
        }
    }
    if (m_interrupted) {
        const auto state = nsqt_capture_restyle_lifecycle(m_interrupted, true);
        if (state["building"].toBool() && !state["window_attached"].toBool())
            m_closed_during_build = true;
        if (m_interrupt_restyle.startsWith("shutdown"))
            finishInterruption(false);
    }
}

void NS_Capture::finishInterruption(bool timeout)
{
    const auto state = nsqt_capture_restyle_lifecycle(m_interrupted, false);
    bool ok = !timeout && m_closed_during_build && !state["building"].toBool() && !state["locked"].toBool() &&
        !state["conversion"].toBool() && !state["old_tree"].toBool() && state["table_entries"].toInt() == 0 &&
        state["active_roots"].toInt() == 0 && state["pending_roots"].toInt() == 0 && state["restarts"].toInt() == 1;
    if (m_interrupt_restyle.startsWith("shutdown"))
        ok = !timeout && m_closed_during_build && state["building"].toBool() && state["conversion"].toBool();
    QString current_url;
    if (m_bw && browser_window_get_content(m_bw))
        current_url = QString::fromUtf8(nsurl_access(hlcache_handle_get_url(browser_window_get_content(m_bw))));
    if (m_interrupt_restyle == "navigate")
        ok = ok && current_url == "about:blank";
    auto *interrupted = m_interrupted;
    nsqt_capture_unwatch_content(interrupted);
    m_interrupted = nullptr;
    // Releasing the last observer cancels an abandoned build synchronously;
    // cache destruction happens later, after returning to the event loop.
    const auto released = nsqt_capture_restyle_lifecycle(interrupted, false);
    if (m_interrupt_restyle.startsWith("shutdown"))
        ok = ok && !released["building"].toBool() && !released["locked"].toBool() && !released["conversion"].toBool() &&
            released["table_entries"].toInt() == 0 && released["error"].toBool();
    const QJsonObject report{{"result", ok ? "complete" : "interruption_error"}, {"mode", m_interrupt_restyle},
        {"closed_during_build", m_closed_during_build}, {"before", m_before_interruption}, {"after", state},
        {"current_url", current_url}, {"released", released}};
    QSaveFile file(QDir(m_directory).filePath("interruption.json"));
    const QByteArray data = QJsonDocument(report).toJson();
    ok = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit() && ok;
    m_finished = true;
    m_timer.stop();
    QApplication::closeAllWindows();
    QCoreApplication::exit(ok ? 0 : 1);
}

bool NS_Capture::dump(const char *name, bool boxes)
{
    const QByteArray path = QFile::encodeName(QDir(m_directory).filePath(name));
    FILE *file = fopen(path.constData(), "w");
    if (!file) {
        return false;
    }
    const nserror error = browser_window_debug_dump(m_bw, file, boxes ? CONTENT_DEBUG_RENDER : CONTENT_DEBUG_DOM);
    const bool written = ferror(file) == 0;
    const bool closed = fclose(file) == 0;
    return error == NSERROR_OK && written && closed;
}

void NS_Capture::finish(const QString &result)
{
    m_finished = true;
    m_timer.stop();
    const qint64 elapsed = m_elapsed.elapsed();
    auto *content = browser_window_get_content(m_bw);
    const bool done = content && content_get_status(content) == CONTENT_STATUS_DONE;
    const bool ready = content && browser_window_redraw_ready(m_bw) &&
        (done || content_get_status(content) == CONTENT_STATUS_READY);
    bool output_ok = true;
    bool image_saved = false;
    bool dom_saved = false;
    bool boxes_saved = false;
    QSize pixels(0, 0);
    const auto widget_paint = static_cast<NS_Widget *>(m_widget.data())->capturePaintStats();
    if (ready) {
        // QWidget::grab uses the normal paintEvent, plotters and font metrics.
        // No thumbnail scaling, browser chrome or extra layout pass is involved.
        QPixmap picture;
        if (m_screen_capture) {
            // A QWidget grab repaints, hiding stale pixels left by a bad blit.
            const QPoint offset = m_widget->mapTo(m_widget->window(), QPoint());
            picture = m_widget->screen()->grabWindow(
                m_widget->window()->winId(), offset.x(), offset.y(), m_widget->width(), m_widget->height());
        } else {
            picture = m_widget->grab();
        }
        pixels = picture.size();
        image_saved = picture.save(QDir(m_directory).filePath("page.png"), "PNG");
        output_ok = image_saved;
        if (content_get_type(content) == CONTENT_HTML) {
            dom_saved = dump("dom.txt", false);
            boxes_saved = dump("boxes.txt", true);
            output_ok = output_ok && dom_saved && boxes_saved;
        }
    }
    int width = 0, height = 0, document_width = 0, document_height = 0;
    browser_window_get_dimensions(m_bw, &width, &height);
    browser_window_get_extents(m_bw, true, &document_width, &document_height);
    QJsonObject artifacts{{"page.png", image_saved}, {"dom.txt", dom_saved}, {"boxes.txt", boxes_saved}};
    if (!m_inspect_ids.isEmpty() || m_inspect_point.x() >= 0) {
        const QJsonObject inspection = nsqt_capture_inspect(ready ? content : nullptr, m_inspect_ids, m_inspect_point);
        QSaveFile inspection_file(QDir(m_directory).filePath("inspection.json"));
        const QByteArray data = QJsonDocument(inspection).toJson();
        const bool saved = inspection_file.open(QIODevice::WriteOnly) && inspection_file.write(data) == data.size() &&
            inspection_file.commit();
        artifacts["inspection.json"] = saved;
        output_ok = output_ok && saved;
    }
    if (!m_actions.isEmpty()) {
        QSaveFile steps_file(QDir(m_directory).filePath("steps.json"));
        const QByteArray data = QJsonDocument(m_steps).toJson();
        const bool saved = steps_file.open(QIODevice::WriteOnly) && steps_file.write(data) == data.size() &&
            steps_file.commit();
        artifacts["steps.json"] = saved;
        output_ok = output_ok && saved;
    }
    const bool viewport_ok = QSize(width, height) == m_size && m_widget->size() == m_size;
    QString outcome = result;
    if (!output_ok) {
        outcome = "output_error";
    } else if (!viewport_ok) {
        outcome = "viewport_error";
    }
    QJsonObject fonts;
    const char *names[] = {"sans", "serif", "mono", "cursive", "fantasy"};
    const char *families[] = {nsoption_charp(font_sans), nsoption_charp(font_serif), nsoption_charp(font_mono),
        nsoption_charp(font_cursive), nsoption_charp(font_fantasy)};
    for (int i = 0; i < 5; ++i) {
        const QString family = QString::fromUtf8(families[i]);
        fonts[names[i]] = QJsonObject{{"configured", family}, {"resolved", QFontInfo(QFont(family)).family()}};
    }
    const QJsonObject metadata{
        {"schema_version", 1},
        {"result", outcome},
        {"requested_url", m_requested_url},
        {"content_url", content ? QString::fromUtf8(nsurl_access(hlcache_handle_get_url(content))) : QString()},
        {"title", QString::fromUtf8(browser_window_get_title(m_bw))},
        {"load_complete", done && !m_loading},
        {"status", content ? QString::fromUtf8(content_get_status_message(content)) : QString()},
        {"elapsed_ms", elapsed},
        {"quiet_ms", elapsed - m_last_activity},
        {"text_cache", nsqt_capture_text_cache_stats()},
        {"bitmap_cache", nsqt_capture_bitmap_cache_stats()},
        {"widget_paint", widget_paint},
        {"settle_ms", m_settle_ms},
        {"timeout_ms", m_timeout_ms},
        {"viewport", QJsonObject{{"width", width}, {"height", height}}},
        {"requested_viewport", QJsonObject{{"width", m_size.width()}, {"height", m_size.height()}}},
        {"image_pixels", QJsonObject{{"width", pixels.width()}, {"height", pixels.height()}}},
        {"document_extent", QJsonObject{{"width", document_width}, {"height", document_height}}},
        {"device_pixel_ratio", m_widget->devicePixelRatioF()},
        {"dpi", browser_get_dpi()},
        {"zoom", browser_window_get_scale(m_bw)},
        {"javascript", nsoption_bool(enable_javascript)},
        {"animate_images", nsoption_bool(animate_images)},
        {"font_size_tenths_pt", nsoption_int(font_size)},
        {"font_min_size_tenths_pt", nsoption_int(font_min_size)},
        {"fonts", fonts},
        {"qt_version", qVersion()},
        {"qt_platform", QGuiApplication::platformName()},
        {"artifacts", artifacts},
        {"restyle_state",
            m_fail_retained_layout ? nsqt_capture_restyle_lifecycle(m_failure_watch, false) : QJsonObject()},
    };
    QSaveFile file(QDir(m_directory).filePath("capture.json"));
    const QByteArray json = QJsonDocument(metadata).toJson();
    const bool metadata_saved = file.open(QIODevice::WriteOnly) && file.write(json) == json.size() && file.commit();
    const int exit_code = outcome == "complete" && image_saved && metadata_saved ? 0 : 1;
    fprintf(stderr, "Capture %s: %s%s\n", qPrintable(outcome), qPrintable(m_directory),
        metadata_saved ? "" : " (failed to write capture.json)");
    if (m_failure_watch != nullptr) {
        nsqt_capture_unwatch_content(m_failure_watch);
        m_failure_watch = nullptr;
    }
    // Close browser windows before tearing down the core and its options.
    QApplication::closeAllWindows();
    QCoreApplication::exit(exit_code);
}
