/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#ifndef WISP_QUICKJS_TIMERS_H
#define WISP_QUICKJS_TIMERS_H

#include "quickjs.h"

/**
 * Initialize timer functions (setTimeout, setInterval, etc.) on the global object.
 *
 * @param ctx QuickJS context
 * @return 0 on success, -1 on failure
 */
int qjs_init_timers(JSContext *ctx);

#endif /* NEOSURF_QUICKJS_TIMERS_H */
