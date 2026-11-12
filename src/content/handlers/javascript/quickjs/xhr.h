/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#ifndef WISP_QUICKJS_XHR_H
#define WISP_QUICKJS_XHR_H

#include "private.h"
void qjs_xhr_close(jsthread *thread);
JSValue qjs_xhr_target(JSContext *ctx, JSValueConst object);

/**
 * Initialize XMLHttpRequest constructor on the global object.
 *
 * @param ctx QuickJS context
 * @return 0 on success, -1 on failure
 */
int qjs_init_xhr(JSContext *ctx);

#endif /* NEOSURF_QUICKJS_XHR_H */
