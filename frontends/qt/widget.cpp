/*
 * Copyright 2023 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Widget methods for browsing context display.
 */

#include <QPaintEvent>
#include <QPainter>
#include <climits>
#ifdef WISP_QT_CAPTURE
#include "qt/capture_inspect.h"
extern "C" {
#include "utils/nsurl.h"
#include "wisp/content/hlcache.h"
}
bool nsqt_capture_disable_scroll_blit;
#endif

extern "C" {
#include "utils/errors.h"
#include "utils/log.h"
#include "utils/nsoption.h"

#include "wisp/content.h"
#include "wisp/keypress.h"
#include "wisp/plotters.h"
}

#include "qt/application.cls.h"
#include "qt/keymap.h"
#include "qt/misc.h"
#include "qt/plotters.h"
#include "qt/widget.cls.h"


#define CARET_WIDTH 1

#define CARET_FRAME_COUNT (2)

/**
 * netsurf widget class constructor
 */
NS_Widget::NS_Widget(QWidget *parent, NS_Actions *actions, struct browser_window *bw)
    : QWidget(parent, Qt::Widget), m_bw(bw), m_actions(actions), m_xoffset(0), m_yoffset(0),
      m_pointer_shape(GUI_POINTER_DEFAULT), m_contextmenu(new QMenu(this)), m_drag_state(BROWSER_MOUSE_HOVER),
      m_caret_frame(0)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    // paintEvent fills every damaged pixel before drawing page content.
    setAttribute(Qt::WA_OpaquePaintEvent);
}


/* map qt event modifiers to browser mouse state keyboard modifiers */
void NS_Widget::event_to_bms_modifiers(QMouseEvent *event, unsigned int &bms)
{
    Qt::KeyboardModifiers mods = event->modifiers();
    if ((mods & Qt::ShiftModifier) != 0) {
        bms |= BROWSER_MOUSE_MOD_1;
    }
    if ((mods & Qt::ControlModifier) != 0) {
        bms |= BROWSER_MOUSE_MOD_2;
    }
    if ((mods & Qt::AltModifier) != 0) {
        bms |= BROWSER_MOUSE_MOD_3;
    }
    if ((mods & Qt::MetaModifier) != 0) {
        bms |= BROWSER_MOUSE_MOD_4;
    }
}


/* method to redraw caret called from redraw event */
void NS_Widget::redraw_caret(struct rect *clip, struct redraw_context *ctx)
{
    if (m_caret_frame != 1) {
        return;
    }
    QPainter *painter = (QPainter *)ctx->priv;
    QPen pen(Qt::SolidLine);
    painter->setPen(pen);
    QPainter::CompositionMode oldmode = painter->compositionMode();
    painter->setCompositionMode(QPainter::RasterOp_NotDestination);
    painter->drawLine(
        m_caret_x - m_xoffset, m_caret_y - m_yoffset, m_caret_x - m_xoffset, m_caret_y + m_caret_h - m_yoffset);
    painter->setCompositionMode(oldmode);
}


/* cause caret animation to advance a frame and schedule the next */
void NS_Widget::next_caret_frame(void *p)
{
    NS_Widget *widget = (NS_Widget *)p;
    int frame_time = NS_Application::cursorFlashTime();

    if (frame_time < 100) {
        frame_time = 0;
    } else if (widget->m_caret_frame > 0) {
        /* first time through animation holds first frame twice as long
         */
        frame_time = frame_time / 2;
    }

    widget->m_caret_frame++;
    if (widget->m_caret_frame > CARET_FRAME_COUNT) {
        widget->m_caret_frame = 1;
    }

    widget->update(widget->m_caret_x - widget->m_xoffset, widget->m_caret_y - widget->m_yoffset,
        widget->m_caret_x + CARET_WIDTH - widget->m_xoffset, widget->m_caret_y + widget->m_caret_h - widget->m_yoffset);

    if (frame_time != 0) {
        nsqt_schedule(frame_time, next_caret_frame, p);
    }
}


/* widget has lost focus */
void NS_Widget::focusOutEvent(QFocusEvent *event)
{
    setCaret(false, 0, 0, 0);
}


/**
 * widget has been resized
 */
void NS_Widget::resizeEvent(QResizeEvent *event)
{
    m_scroll_blit_ready = false;
    if (m_bw == nullptr) {
        return;
    }
    browser_window_schedule_reformat(m_bw);
}


/**
 * Invalidate the browser window pointer before destruction
 */
void NS_Widget::invalidateBrowserWindow()
{
    m_scroll_blit_ready = false;
    m_bw = nullptr;
}


/**
 * redraw the netsurf browsing widget
 */
void NS_Widget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), Qt::white);
    if (m_bw == nullptr) {
        return;
    }
    // Invalidation during rendering must leave this false for the next scroll.
    m_scroll_blit_ready = true;
#ifdef WISP_QT_CAPTURE
    m_paint_calls++;
#endif

    /* Enable antialiasing for smoother edges on diagonal lines and curves */
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    struct redraw_context ctx = {
        .interactive = true,
        .background_images = true,
        .plot = &nsqt_plotters,
        .priv = &painter,
    };
    // Keep disjoint damage separate; its bounding rectangle can span the page.
    // Unsupported drawing needs one full viewport pass. Qt's system clip still
    // restricts the displayed writes to the actual damage region.
    const QRegion paintRegion = browser_window_can_redraw_regions(m_bw) ? event->region() : QRegion(rect());
    for (const QRect &area : paintRegion) {
#ifdef WISP_QT_CAPTURE
        m_paint_pixels += quint64(area.width()) * area.height();
#endif
        painter.save();
        struct rect clip = {area.x(), area.y(), area.x() + area.width(), area.y() + area.height()};
        if (!browser_window_redraw(m_bw, -m_xoffset, -m_yoffset, &clip, &ctx))
            m_scroll_blit_ready = false;
        painter.restore();
    }

    // The caret uses XOR, so draw it once through Qt's complete event clip.
    struct rect caret_clip = {event->rect().x(), event->rect().y(), event->rect().x() + event->rect().width(),
        event->rect().y() + event->rect().height()};
    redraw_caret(&caret_clip, &ctx);

    m_painted_dpr = devicePixelRatioF();
}


void NS_Widget::mousePressEvent(QMouseEvent *event)
{
    m_click_detail = 1;
    if (m_bw == nullptr) {
        return;
    }
    QPointF pos = event->position();
    /** the mouse button the event was generated for */
    Qt::MouseButton button = event->button();
    unsigned int bms = BROWSER_MOUSE_HOVER; /* empty state */

    /* pressed buttons */
    if ((button & Qt::LeftButton) != 0) {
        bms |= BROWSER_MOUSE_PRESS_1;
    }
    if ((button & Qt::MiddleButton) != 0) {
        bms |= BROWSER_MOUSE_PRESS_2;
    }
    if ((button & Qt::RightButton) != 0) {
        bms |= BROWSER_MOUSE_PRESS_3;
    }
    if ((button & Qt::BackButton) != 0) {
        bms |= BROWSER_MOUSE_PRESS_4;
    }
    if ((button & Qt::ForwardButton) != 0) {
        bms |= BROWSER_MOUSE_PRESS_5;
    }

    /* record last press position */
    if (bms != BROWSER_MOUSE_HOVER) {
        m_press_pos = pos;
    }

    /* keyboard modifiers */
    event_to_bms_modifiers(event, bms);

    browser_window_mouse_click(m_bw, (browser_mouse_state)bms, pos.x() + m_xoffset, pos.y() + m_yoffset);
}


void NS_Widget::mouseDoubleClickEvent(QMouseEvent *event)
{
    mousePressEvent(event);
    m_click_detail = 2;
}

void NS_Widget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_bw == nullptr) {
        return;
    }
    const QPointF pos = event->position();
    Qt::MouseButtons buttons = event->buttons();
    unsigned int bms = BROWSER_MOUSE_HOVER;

    if (buttons != Qt::NoButton) {
        /* mouse movement with buttons held
         * press event has set m_press_pos
         */
        if (m_drag_state == BROWSER_MOUSE_HOVER) {
            /* drag not started,
             * ensure cursor has moved significantly
             * from start position
             */
            if ((fabs(pos.x() - m_press_pos.x()) > 5.0) || (fabs(pos.y() - m_press_pos.y()) > 5.0)) {
                /* start drag operation */
                if ((buttons & Qt::LeftButton) != 0) {
                    /* Start button 1 drag where button
                     * initialy pressed */
                    browser_window_mouse_click(
                        m_bw, BROWSER_MOUSE_DRAG_1, m_press_pos.x() + m_xoffset, m_press_pos.y() + m_yoffset);
                    m_drag_state = BROWSER_MOUSE_HOLDING_1;
                } else if ((buttons & Qt::MiddleButton) != 0) {
                    /* Start button 2 drag where button
                     * initialy pressed */
                    browser_window_mouse_click(
                        m_bw, BROWSER_MOUSE_DRAG_2, m_press_pos.x() + m_xoffset, m_press_pos.y() + m_yoffset);
                    m_drag_state = BROWSER_MOUSE_HOLDING_2;
                }
            }
        }

        if (m_drag_state != BROWSER_MOUSE_HOVER) {
            bms = m_drag_state | BROWSER_MOUSE_DRAG_ON;
        }
    }

    /* keyboard modifiers */
    event_to_bms_modifiers(event, bms);

    browser_window_mouse_track(m_bw, (browser_mouse_state)bms, pos.x() + m_xoffset, pos.y() + m_yoffset);
}


void NS_Widget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_bw == nullptr) {
        return;
    }
    /** the position the event was generated for */
    const QPointF pos = event->position();
    /** the mouse button the event was generated for */
    Qt::MouseButton button = event->button();
    /** browser mouse state to report starts empty */
    unsigned int bms = BROWSER_MOUSE_HOVER;

    /* released button */
    if ((button & Qt::LeftButton) != 0) {
        if (m_drag_state == BROWSER_MOUSE_HOLDING_1) {
            /* terminate drag */
            browser_window_mouse_track(
                m_bw, (browser_mouse_state)BROWSER_MOUSE_HOVER, pos.x() + m_xoffset, pos.y() + m_yoffset);
            m_drag_state = BROWSER_MOUSE_HOVER;
        } else {
            bms |= BROWSER_MOUSE_CLICK_1;
        }
    }
    if ((button & Qt::MiddleButton) != 0) {
        if (m_drag_state == BROWSER_MOUSE_HOLDING_2) {
            /* terminate drag */
            browser_window_mouse_track(
                m_bw, (browser_mouse_state)BROWSER_MOUSE_HOVER, pos.x() + m_xoffset, pos.y() + m_yoffset);
            m_drag_state = BROWSER_MOUSE_HOVER;
        } else {
            bms |= BROWSER_MOUSE_CLICK_2;
        }
    }
    if ((button & Qt::RightButton) != 0) {
        bms |= BROWSER_MOUSE_CLICK_3;
    }
    if ((button & Qt::BackButton) != 0) {
        bms |= BROWSER_MOUSE_CLICK_4;
    }
    if ((button & Qt::ForwardButton) != 0) {
        bms |= BROWSER_MOUSE_CLICK_5;
    }

    /* keyboard modifiers */
    event_to_bms_modifiers(event, bms);

    const QPointF screen = event->globalPosition();
    struct browser_mouse_event input = {};
    input.state = (browser_mouse_state)bms;
    input.x = pos.x() + m_xoffset;
    input.y = pos.y() + m_yoffset;
    input.screen_x = screen.x();
    input.screen_y = screen.y();
    input.buttons = ((event->buttons() & Qt::LeftButton) ? 1 : 0) | ((event->buttons() & Qt::RightButton) ? 2 : 0) |
        ((event->buttons() & Qt::MiddleButton) ? 4 : 0);
    input.detail = m_click_detail;
    browser_window_mouse_click_event(m_bw, &input);
}


void NS_Widget::keyPressEvent(QKeyEvent *event)
{
    if (m_bw == nullptr) {
        QWidget::keyPressEvent(event);
        return;
    }
    uint32_t nskey;
    nskey = qkeyevent_to_nskey(event);
    if (browser_window_key_press(m_bw, nskey) == false) {
        QWidget::keyPressEvent(event);
    }
}


/*
 * open a relevant context menu
 *
 * get the features of the browser window where it was opened
 *  | link | object | selection | menu to open     |
 *  |------|--------|-----------|------------------|
 *  |      |        |           | context          |
 *  |   x  |        |           | link             |
 *  |      |   x    |           | object           |
 *  |   x  |   x    |           | link+object      |
 *  |      |        |     x     | copy             |
 *  |   x  |        |     x     | link+copy        |
 *  |      |   x    |     x     | object+copy      |
 *  |   x  |   x    |     x     | link+object+copy |
 *
 */
void NS_Widget::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_bw == nullptr) {
        return;
    }
    struct browser_window_features features;
    char *selected_text;

    browser_window_get_features(m_bw, event->x() + m_xoffset, event->y() + m_yoffset, &features);
    selected_text = browser_window_get_selection(m_bw);

    if ((selected_text == NULL) && (features.link_title != NULL)) {
        selected_text = (char *)malloc(features.link_title_length + 1);
        memcpy(selected_text, features.link_title, features.link_title_length);
        *(selected_text + features.link_title_length) = 0;
    }

    m_actions->update(features.link, features.object, selected_text);

    m_contextmenu->clear();

    if ((features.link == NULL) && (features.object == NULL) && (selected_text == NULL)) {
        /* populate base menu */
        m_contextmenu->addAction(m_actions->m_back);
        m_contextmenu->addAction(m_actions->m_forward);
        m_contextmenu->addAction(m_actions->m_stop_reload);
        m_contextmenu->addSeparator();
        m_contextmenu->addAction(m_actions->m_add_edit_bookmark);
        m_contextmenu->addSeparator();
        m_contextmenu->addAction(m_actions->m_page_save);
        m_contextmenu->addSeparator();
        m_contextmenu->addAction(m_actions->m_page_source);
    } else {
        bool prev = false; // have there been previous menu entries
        if (features.link != NULL) {
            /* link entries */
            m_contextmenu->addAction(m_actions->m_link_new_tab);
            m_contextmenu->addAction(m_actions->m_link_new_window);
            m_contextmenu->addSeparator();
            m_contextmenu->addAction(m_actions->m_link_bookmark);
            m_contextmenu->addAction(m_actions->m_link_save);
            m_contextmenu->addAction(m_actions->m_link_copy);
            prev = true;
        }
        if (features.object != NULL) {
            /* object/image entries */
            if (prev) {
                m_contextmenu->addSeparator();
            } else {
                prev = true;
            }
            if (content_get_type(features.object) == CONTENT_IMAGE) {
                m_contextmenu->addAction(m_actions->m_img_new_tab);
                m_contextmenu->addAction(m_actions->m_img_save);
                m_contextmenu->addAction(m_actions->m_img_copy);
            } else {
                m_contextmenu->addAction(m_actions->m_obj_save);
                m_contextmenu->addAction(m_actions->m_obj_copy);
            }
        }
        if (selected_text != NULL) {
            if (prev) {
                m_contextmenu->addSeparator();
            } else {
                prev = true;
            }
            m_contextmenu->addAction(m_actions->m_sel_copy);
            m_contextmenu->addAction(m_actions->m_sel_search);
        }
        /** @todo are there any additional entries like "inspect" */
    }
    m_contextmenu->popup(event->globalPos());
}


/**
 * get the current scroll offsets
 */
bool NS_Widget::get_scroll(int *sx, int *sy)
{
    *sx = m_xoffset;
    *sy = m_yoffset;

    return true;
}


/**
 * get the viewable dimensions of browsing context
 */
nserror NS_Widget::get_dimensions(int *width, int *height)
{
    *width = size().width();
    *height = size().height();

    return NSERROR_OK;
}


/**
 * change pointer
 */
void NS_Widget::set_pointer(enum gui_pointer_shape set_shape)
{
    enum Qt::CursorShape qshape = Qt::ArrowCursor;

    /* if the shape is being changed to what is already set there is
     * nothing to do.
     */
    if (m_pointer_shape == set_shape) {
        return;
    }
    m_pointer_shape = set_shape;

    switch (m_pointer_shape) {
    case GUI_POINTER_POINT:
        qshape = Qt::PointingHandCursor;
        break;
    case GUI_POINTER_CARET:
        qshape = Qt::IBeamCursor;
        break;
    case GUI_POINTER_CROSS:
        qshape = Qt::CrossCursor;
        break;
    case GUI_POINTER_MOVE:
        qshape = Qt::OpenHandCursor;
        break;
    case GUI_POINTER_NOT_ALLOWED:
    case GUI_POINTER_NO_DROP:
        qshape = Qt::ForbiddenCursor;
        break;
    case GUI_POINTER_WAIT:
        qshape = Qt::WaitCursor;
        break;
    case GUI_POINTER_HELP:
        qshape = Qt::WhatsThisCursor;
        break;
    case GUI_POINTER_UP:
    case GUI_POINTER_DOWN:
        qshape = Qt::SizeVerCursor;
        break;
    case GUI_POINTER_LEFT:
    case GUI_POINTER_RIGHT:
        qshape = Qt::SizeHorCursor;
        break;
    case GUI_POINTER_RU:
    case GUI_POINTER_LD:
        qshape = Qt::SizeBDiagCursor;
        break;
    case GUI_POINTER_LU:
    case GUI_POINTER_RD:
        qshape = Qt::SizeFDiagCursor;
        break;
    case GUI_POINTER_PROGRESS:
        qshape = Qt::BusyCursor;
        break;
    case GUI_POINTER_MENU:
    case GUI_POINTER_DEFAULT:
    default:
        qshape = Qt::ArrowCursor;
        break;
    }
    setCursor(QCursor(qshape));
}


/**
 * Change position of caret
 */
void NS_Widget::setCaret(bool visible, int cx, int cy, int ch)
{
    /* Removing a caret queues a repaint too. Until it finishes, the backing
     * store still contains the old XOR ink, even though m_caret_frame is zero. */
    if (m_caret_frame != 0 || visible)
        m_scroll_blit_ready = false;
    /* compute area to redraw and update caret position */
    if (m_caret_frame != 0) {
        int x0, y0, x1, y1;
        x0 = m_caret_x - m_xoffset;
        y0 = m_caret_y - m_yoffset;
        x1 = m_caret_x + CARET_WIDTH - m_xoffset;
        y1 = m_caret_y + m_caret_h - m_yoffset;

        if (visible) {
            /*
             * caret is being shown and should remain visible.
             * reschedule animation
             */
            m_caret_x = cx;
            m_caret_y = cy;
            m_caret_h = ch;

            m_caret_frame = 0;
            next_caret_frame(this);
        } else {
            /*
             * caret being shown but should no longer be visible.
             * remove animation scheduling.
             */
            nsqt_schedule(-1, next_caret_frame, this);
            m_caret_frame = 0;
        }
        /*
         * update previous caret area in widget relative coordinates.
         * must be done after updating the cursor position
         */
        update(x0, y0, x1, y1);
    } else {
        if (visible) {
            /* caret not being shown and now should be update region
             * of newly drawn caret only
             */
            m_caret_x = cx;
            m_caret_y = cy;
            m_caret_h = ch;

            next_caret_frame(this);

        } else {
            /* not being shown and should remain so */
        }
    }
}


/**
 * mark an area of the browsing context as invalid
 */
nserror NS_Widget::invalidate(const struct rect *rect)
{
    m_scroll_blit_ready = false;
    if (rect == NULL) {
        update();
    } else {
        // Core invalidation uses scaled document coordinates; QWidget uses viewport pixels.
        update(rect->x0 - m_xoffset, rect->y0 - m_yoffset, rect->x1 - rect->x0, rect->y1 - rect->y0);
    }
    return NSERROR_OK;
}


/**
 * slot to recive horizontal scroll signal
 */
void NS_Widget::setHorizontalScroll(int value)
{
    scrollTo(value, m_yoffset);
}

/**
 * slot to recive vertical scroll signal
 */
void NS_Widget::setVerticalScroll(int value)
{
    scrollTo(m_xoffset, value);
}

void NS_Widget::scrollTo(int x, int y)
{
    int dx = m_xoffset - x, dy = m_yoffset - y;
    if (dx == 0 && dy == 0)
        return;
    qreal dpr = devicePixelRatioF();
    bool reuse = m_scroll_blit_ready && m_caret_frame == 0 && isVisible() && qAbs(dx) < width() &&
        qAbs(dy) < height() && dpr == m_painted_dpr && qFuzzyIsNull(dx * dpr - qRound(dx * dpr)) &&
        qFuzzyIsNull(dy * dpr - qRound(dy * dpr)) && qint64(qMax(x, m_xoffset)) + width() <= INT_MAX &&
        qint64(qMax(y, m_yoffset)) + height() <= INT_MAX;
#ifdef WISP_QT_CAPTURE
    reuse = reuse && !nsqt_capture_disable_scroll_blit;
#endif
    QRegion damage;
    if (reuse) {
        // Include the newly exposed strips in the complexity/area budget.
        damage = QRegion(rect()) - QRegion(rect().translated(dx, dy));
        const struct rect oldViewport = {m_xoffset, m_yoffset, m_xoffset + width(), m_yoffset + height()};
        const struct rect newViewport = {x, y, x + width(), y + height()};
        reuse = browser_window_get_scroll_damage(
            m_bw, &oldViewport, &newViewport,
            [](const struct rect *r, void *pw) {
                auto *region = static_cast<QRegion *>(pw);
                *region += QRect(r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0);
                return region->rectCount() <= 32;
            },
            &damage);
        qint64 pixels = 0;
        for (const QRect &r : damage)
            pixels += qint64(r.width()) * r.height();
        // Many paint passes or a nearly full repaint cost more than the copy saves.
        reuse = reuse && pixels * 4 < qint64(width()) * height() * 3;
    }
    m_xoffset = x;
    m_yoffset = y;
    browser_window_scroll_changed(m_bw);
#ifdef WISP_QT_CAPTURE
    (reuse ? m_scroll_blits : m_scroll_repaints)++;
#endif
    m_scroll_blit_ready = false;
    if (reuse) {
        QWidget::scroll(dx, dy, rect());
        update(damage);
    } else {
        update();
    }
}

#ifdef WISP_QT_CAPTURE
QJsonObject NS_Widget::capturePaintStats() const
{
    return {{"scroll_blits", qint64(m_scroll_blits)}, {"scroll_repaints", qint64(m_scroll_repaints)},
        {"paint_calls", qint64(m_paint_calls)}, {"paint_pixels", qint64(m_paint_pixels)}};
}

QJsonObject NS_Widget::captureNavigationState() const
{
    if (!m_bw)
        return {};
    auto *content = browser_window_get_content(m_bw);
    return {{"content_url", content ? QString::fromUtf8(nsurl_access(hlcache_handle_get_url(content))) : QString()},
        {"status", QString::fromUtf8(browser_window_get_status_message(m_bw))},
        {"load_complete", content && content_get_status(content) == CONTENT_STATUS_DONE &&
            !browser_window_stop_available(m_bw)}};
}
#endif


QSize NS_Widget::sizeHint() const
{
    int width = nsoption_int(window_width);
    if (width == 0) {
        width = 1000;
    }
    int height = nsoption_int(window_height);
    if (height == 0) {
        height = 700;
    }
    QSize s(width, height);
    return s;
}
