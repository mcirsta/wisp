/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf.
 *
 * NeoSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

/**
 * \file
 * QuickJS-ng Console binding header.
 */

#ifndef NEOSURF_QUICKJS_CONSOLE_H
#define NEOSURF_QUICKJS_CONSOLE_H

#include "quickjs.h"

/**
 * Initialize Console class and create global console object.
 *
 * Call this after creating a JSContext to set up the console.
 *
 * \param ctx The JavaScript context
 * \return 0 on success, -1 on failure
 */
int qjs_init_console(JSContext *ctx);

#endif /* NEOSURF_QUICKJS_CONSOLE_H */
