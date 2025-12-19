/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#ifndef NEOSURF_QUICKJS_DOCUMENT_H
#define NEOSURF_QUICKJS_DOCUMENT_H

#include "quickjs.h"

/**
 * Initialize Document object on the global object.
 *
 * @param ctx QuickJS context
 * @return 0 on success, -1 on failure
 */
int qjs_init_document(JSContext *ctx);

#endif /* NEOSURF_QUICKJS_DOCUMENT_H */
