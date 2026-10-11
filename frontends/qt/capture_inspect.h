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
QJsonObject nsqt_capture_text_cache_stats();
void nsqt_capture_text_cache_limit(qsizetype bytes);
QJsonObject nsqt_capture_bitmap_cache_stats();
void nsqt_capture_bitmap_cache_limit(qsizetype bytes);
extern bool nsqt_capture_disable_scroll_blit;
QJsonObject nsqt_capture_inspect(struct hlcache_handle *content, const QStringList &ids, const QPoint &point);

class QWidget;
struct browser_window;
bool nsqt_capture_settled(struct hlcache_handle *content);
void nsqt_capture_configure_restyle(struct hlcache_handle *content, bool force, bool restart, bool css_only, bool fail,
    bool no_intrinsic_reuse, bool no_inline_layout_reuse);
struct content;
struct content *nsqt_capture_watch_content(struct hlcache_handle *handle);
void nsqt_capture_unwatch_content(struct content *content);
QJsonObject nsqt_capture_restyle_lifecycle(struct content *content, bool slow);
bool nsqt_capture_click(QWidget *widget, struct browser_window *bw, const QString &id, bool label);
void nsqt_capture_click_point(QWidget *widget, struct browser_window *bw, int x, int y);
bool nsqt_capture_scroll_point(struct browser_window *bw, int x, int y, int dx, int dy);
void nsqt_capture_drag(struct browser_window *bw, int x1, int y1, int x2, int y2);
void nsqt_capture_scroll(struct hlcache_handle *content, int x, int y);
bool nsqt_capture_scroll_element(struct hlcache_handle *content, const QString &id, int x, int y);
