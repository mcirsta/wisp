/*
 * This file is part of Hubbub.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007-8 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef hubbub_parser_h_
#define hubbub_parser_h_

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <stdbool.h>

#include <hubbub/errors.h>
#include <hubbub/functypes.h>
#include <hubbub/tree.h>
#include <hubbub/types.h>

typedef struct hubbub_parser hubbub_parser;

/** HTML fragment context. Nodes are borrowed during configuration; the parser
 * retains root and form until destroyed. root is a synthetic HTML element,
 * distinct from the real context element, whose children receive the result.
 * Configure before feeding input, after the tree handler and document node.
 * Supports HTML (including template), SVG and MathML contexts. */
typedef struct hubbub_fragment_context {
    hubbub_string name; /**< Context element's case-sensitive local name */
    hubbub_ns ns; /**< Context namespace */
    hubbub_string encoding; /**< annotation-xml encoding attribute, or {NULL, 0} */
    void *root; /**< Empty synthetic HTML root */
    void *form; /**< Nearest inclusive HTML form ancestor, or NULL */
} hubbub_fragment_context;

/**
 * Hubbub parser option types
 */
typedef enum hubbub_parser_opttype {
    HUBBUB_PARSER_TOKEN_HANDLER,
    HUBBUB_PARSER_ERROR_HANDLER,
    HUBBUB_PARSER_CONTENT_MODEL,
    HUBBUB_PARSER_TREE_HANDLER,
    HUBBUB_PARSER_DOCUMENT_NODE,
    HUBBUB_PARSER_ENABLE_SCRIPTING,
    HUBBUB_PARSER_PAUSE,
    HUBBUB_PARSER_FRAGMENT_CONTEXT
} hubbub_parser_opttype;

/**
 * Hubbub parser option parameters
 */
typedef union hubbub_parser_optparams {
    struct {
        hubbub_token_handler handler;
        void *pw;
    } token_handler; /**< Token handling callback */

    struct {
        hubbub_error_handler handler;
        void *pw;
    } error_handler; /**< Error handling callback */

    struct {
        hubbub_content_model model;
    } content_model; /**< Current content model */

    hubbub_tree_handler *tree_handler; /**< Tree handling callbacks */

    void *document_node; /**< Document node */

    bool enable_scripting; /**< Whether to enable scripting */

    bool pause_parse; /**< Pause parsing */

    hubbub_fragment_context fragment;
} hubbub_parser_optparams;

/* Create a hubbub parser */
hubbub_error hubbub_parser_create(const char *enc, bool fix_enc, hubbub_parser **parser);
/* Destroy a hubbub parser */
hubbub_error hubbub_parser_destroy(hubbub_parser *parser);

/* Configure a hubbub parser */
hubbub_error hubbub_parser_setopt(hubbub_parser *parser, hubbub_parser_opttype type, hubbub_parser_optparams *params);

/* Pass a chunk of data to a hubbub parser for parsing */
/* This data is encoded in the input charset */
hubbub_error hubbub_parser_parse_chunk(hubbub_parser *parser, const uint8_t *data, size_t len);

/**
 * Insert a chunk of data into a hubbub parser input stream
 *
 * This data is encoded in the input charset
 *
 * Inserts the given data into the input stream ready for parsing but
 * does not cause any additional processing of the input. This is
 * useful to allow hubbub callbacks to add computed data to the input.
 *
 * \param parser  Parser instance to use
 * \param data    Data to parse (encoded in the input charset)
 * \param len     Length, in bytes, of data
 * \return HUBBUB_OK on success, appropriate error otherwise
 */
hubbub_error hubbub_parser_insert_chunk(hubbub_parser *parser, const uint8_t *data, size_t len);
/* Inform the parser that the last chunk of data has been parsed */
hubbub_error hubbub_parser_completed(hubbub_parser *parser);

/* Read the document charset */
const char *hubbub_parser_read_charset(hubbub_parser *parser, hubbub_charset_source *source);

#ifdef __cplusplus
}
#endif

#endif
