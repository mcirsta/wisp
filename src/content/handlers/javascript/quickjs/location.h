/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#ifndef WISP_QUICKJS_LOCATION_H
#define WISP_QUICKJS_LOCATION_H

#include "quickjs.h"

/**
 * Initialize Location object on the global object.
 *
 * @param ctx QuickJS context
 * @return 0 on success, -1 on failure
 */
int qjs_init_location(JSContext *ctx);

#endif /* NEOSURF_QUICKJS_LOCATION_H */
