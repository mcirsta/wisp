/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <QFont>
#include <QJsonObject>
#include <QPoint>
#include <QStringList>
#include <memory>

struct hlcache_handle;
struct plot_font_style;

qreal nsqt_capture_font_scale(const struct plot_font_style *style);
QFont nsqt_capture_font(const struct plot_font_style *style);
QJsonObject nsqt_capture_inspect(struct hlcache_handle *content, const QStringList &ids, const QPoint &point);

struct browser_window;
bool nsqt_capture_settled(struct hlcache_handle *content);
bool nsqt_capture_click(struct browser_window *bw, const QString &id, bool label);
void nsqt_capture_scroll(struct hlcache_handle *content, int x, int y);
