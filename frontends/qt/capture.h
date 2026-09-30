/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QSize>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

extern "C" {
#include "utils/errors.h"
#include "wisp/mouse.h"
#include "wisp/window.h"
}

class QWidget;
struct browser_window;
struct content;

/* Optional frontend-only diagnostics. No renderer state is accessed off-thread. */
class NS_Capture : public QObject
{
public:
    /* Strip capture arguments before Qt and the core parse their options.
     * Return 0 to continue, 1 for help, -1 for invalid arguments. */
    int prepare(int &argc, char **argv);
    static NS_Capture *active();
    QString profilePath() const;
    bool finished() const
    {
        return m_finished;
    }
    void attach(struct browser_window *bw, QWidget *widget);
    void detach(struct browser_window *bw);
    void activity(struct browser_window *bw);
    void windowEvent(struct browser_window *bw, enum gui_window_event event);
    void afterScheduledCallback();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void poll();
    void finish(const QString &result);
    bool dump(const char *name, bool boxes);
    void finishInterruption(bool timeout);

    static NS_Capture *s_active;
    QString m_directory;
    QString m_requested_url;
    QStringList m_inspect_ids;
    QVector<QPair<QString, QString>> m_actions;
    QJsonArray m_steps;
    int m_action_index = 0;
    double m_last_action_ms = 0;
    QPoint m_inspect_point{-1, -1};
    QSize m_size{1280, 900};
    int m_timeout_ms = 30000;
    int m_settle_ms = 500;
    std::unique_ptr<QTemporaryDir> m_profile;
    struct browser_window *m_bw = nullptr;
    QPointer<QWidget> m_widget;
    QElapsedTimer m_elapsed;
    qint64 m_last_activity = 0;
    QTimer m_timer;
    bool m_loading = true;
    bool m_force_full_restyle = false;
    bool m_retain_boxes = false;
    bool m_fail_retained_layout = false;
    bool m_restart_partial_restyle = false;
    bool m_finished = false;
    QString m_interrupt_restyle;
    struct content *m_interrupted = nullptr;
    struct content *m_failure_watch = nullptr;
    QJsonObject m_before_interruption;
    bool m_closed_during_build = false;
};
