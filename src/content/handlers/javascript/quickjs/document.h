/*
 * Copyright 2025 Neosurf Contributors
 *
 * This file is part of NeoSurf, http://www.netsurf-browser.org/
 */

#ifndef WISP_QUICKJS_DOCUMENT_H
#define WISP_QUICKJS_DOCUMENT_H

#include "quickjs.h"

struct OptionJSValue{
    JSValue element;
    bool null;
};

/**
 * Initialize Document object on the global object.
 *
 * @param ctx QuickJS context
 * @return 0 on success, -1 on failure
 */
int qjs_init_document(JSContext *ctx);
void *qjs_get_document_priv(JSContext *ctx);
JSValue create_element_object(JSContext *ctx, const char *tag, JSValue element);
#endif /* NEOSURF_QUICKJS_DOCUMENT_H */
