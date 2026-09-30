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
void nsqt_capture_configure_restyle(struct hlcache_handle *content, bool force, bool restart, bool css_only, bool fail);
struct content;
struct content *nsqt_capture_watch_content(struct hlcache_handle *handle);
void nsqt_capture_unwatch_content(struct content *content);
QJsonObject nsqt_capture_restyle_lifecycle(struct content *content, bool slow);
bool nsqt_capture_click(struct browser_window *bw, const QString &id, bool label);
void nsqt_capture_scroll(struct hlcache_handle *content, int x, int y);
bool nsqt_capture_scroll_element(struct hlcache_handle *content, const QString &id, int x, int y);
