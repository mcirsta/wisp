/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_QUICKJS_INPUT_EVENT_H
#define WISP_QUICKJS_INPUT_EVENT_H
#include "event.h"

/* JS-visible UI data, separate from libdom's dispatch state. The old native
 * MouseEvent uses integer coordinates and has no buttons mask. */
struct qjs_input_data {
    JSValue view, related;
    double screen_x, screen_y, client_x, client_y;
    int32_t detail;
    int16_t button;
    uint16_t buttons, modifiers;
};
struct qjs_input_event {
    struct qjs_event base;
    struct qjs_input_data data;
};
JSValue qjs_input_event_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv, int kind);
int qjs_input_event_properties(JSContext *ctx, JSValueConst proto, enum qjs_event_kind kind);
#endif
