/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <QFont>
#include <QJsonObject>
#include <QPoint>
#include <QStringList>
#include <memory>

struct hlcache_handle;
struct plot_font_style;

QFont nsqt_capture_font(const struct plot_font_style *style);
QJsonObject nsqt_capture_inspect(struct hlcache_handle *content, const QStringList &ids, const QPoint &point);
