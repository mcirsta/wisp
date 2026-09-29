/* SPDX-License-Identifier: GPL-2.0-only */
#include "qt/capture.h"
#include "qt/capture_inspect.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFontInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QSaveFile>
#include <QSettings>
#include <QWidget>

extern "C" {
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "wisp/browser.h"
#include "wisp/browser_window.h"
#include "wisp/keypress.h"
#include "wisp/content.h"
#include "wisp/content/content.h"
}

NS_Capture *NS_Capture::s_active = nullptr;

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
                "  --capture-settle MS          Quiet period after load (default 500)\n"
                "  --capture-timeout MS         Overall deadline (default 30000)\n"
                "  --capture-id ID              Inspect an element (repeatable)\n"
                "  --capture-click-id ID        Click an element after settling (repeatable)\n"
                "  --capture-click-label FOR    Click a label associated with this input ID\n"
                "  --capture-type TEXT           Type into the focused control\n"
                "  --capture-scroll X,Y          Scroll before the next action\n"
                "  --capture-point X,Y          Inspect a point in document layout pixels\n"
                "  --capture-force-full-restyle Force the full-refresh reference path\n"
                "  --capture-restart-partial-restyle Test invalidation during partial construction\n"
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
        } else if (arg == "--capture-id") {
            m_inspect_ids.append(value);
            ok = !value.isEmpty();
        } else if (arg == "--capture-click-id" || arg == "--capture-click-label" || arg == "--capture-type" ||
            arg == "--capture-scroll") {
            m_actions.append({arg, value});
            ok = !value.isEmpty();
            if (arg == "--capture-scroll") {
                const auto parts = value.split(',');
                bool x_ok, y_ok;
                const int x = parts.value(0).toInt(&x_ok), y = parts.value(1).toInt(&y_ok);
                ok = parts.size() == 2 && x_ok && y_ok && x >= 0 && y >= 0;
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
    if (!m_widget || m_bw == nullptr) {
        return;
    }
    auto *content = browser_window_get_content(m_bw);
    nsqt_capture_force_full_restyle(content, m_force_full_restyle, m_restart_partial_restyle);
    const bool done = content && content_get_status(content) == CONTENT_STATUS_DONE;
    if (m_elapsed.elapsed() >= m_timeout_ms) {
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
            step["action_ms"] = m_last_action_ms;
            m_steps.append(step);
        }
        if (m_action_index < m_actions.size()) {
            const auto action = m_actions.at(m_action_index++);
            QElapsedTimer action_timer;
            action_timer.start();
            bool ok = true;
            if (action.first == "--capture-type") {
                for (uint32_t key : action.second.toUcs4())
                    ok = browser_window_key_press(m_bw, key) && ok;
            } else if (action.first == "--capture-scroll") {
                const auto xy = action.second.split(',');
                nsqt_capture_scroll(content, xy[0].toInt(), xy[1].toInt());
            } else {
                ok = nsqt_capture_click(m_bw, action.second, action.first == "--capture-click-label");
            }
            m_last_action_ms = action_timer.nsecsElapsed() / 1e6;
            activity(m_bw);
            if (!ok)
                finish("action_error");
        } else {
            finish(url.startsWith("about:query/") ? "load_error" : "complete");
        }
    }
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
    if (ready) {
        // QWidget::grab uses the normal paintEvent, plotters and font metrics.
        // No thumbnail scaling, browser chrome or extra layout pass is involved.
        const QPixmap picture = m_widget->grab();
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
    };
    QSaveFile file(QDir(m_directory).filePath("capture.json"));
    const QByteArray json = QJsonDocument(metadata).toJson();
    const bool metadata_saved = file.open(QIODevice::WriteOnly) && file.write(json) == json.size() && file.commit();
    const int exit_code = outcome == "complete" && image_saved && metadata_saved ? 0 : 1;
    fprintf(stderr, "Capture %s: %s%s\n", qPrintable(outcome), qPrintable(m_directory),
        metadata_saved ? "" : " (failed to write capture.json)");
    // Close browser windows before tearing down the core and its options.
    QApplication::closeAllWindows();
    QCoreApplication::exit(exit_code);
}
