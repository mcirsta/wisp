/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2025 Neosurf Grid Support
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"
#include <stdio.h>

#define MAX_GRID_TRACKS 32

/* Track types for internal storage during parsing */
typedef enum {
    TRACK_SIMPLE, /* Single value + unit (e.g., 100px, 1fr) */
    TRACK_MINMAX /* minmax(min, max) - needs two values */
} track_type_t;

typedef struct {
    track_type_t type;
    css_fixed value; /* For TRACK_SIMPLE: the value. For TRACK_MINMAX: min
                value */
    uint32_t unit; /* For TRACK_SIMPLE: the unit. For TRACK_MINMAX:
              CSS_UNIT_MINMAX marker */
    uint32_t min_unit; /* For TRACK_MINMAX only: actual min unit */
    css_fixed max_value; /* For TRACK_MINMAX only: max value */
    uint32_t max_unit; /* For TRACK_MINMAX only: max unit */
} grid_track_t;

/**
 * Try to parse a track sizing value (dimension, keyword, or function)
 */
static css_error parse_track_size(css_language *c, const parserutils_vector *vector, int32_t *ctx, grid_track_t *track)
{
    int32_t orig_ctx = *ctx;
    css_error error;
    const css_token *token;
    bool match;

    token = parserutils_vector_peek(vector, *ctx);
    if (token == NULL) {
        return CSS_INVALID;
    }
    // fprintf(stderr, "DEBUG: parse_track_size peeked token type %d\n", token->type);

    /* Check for keywords: auto, min-content, max-content */
    if (token->type == CSS_TOKEN_IDENT) {
        if ((lwc_string_caseless_isequal(token->idata, c->strings[AUTO], &match) == lwc_error_ok && match)) {
            /* 'auto' - treat as 1fr for layout purposes */
            parserutils_vector_iterate(vector, ctx);
            track->type = TRACK_SIMPLE;
            track->value = INTTOFIX(1);
            track->unit = UNIT_FR;
            return CSS_OK;
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[MIN_CONTENT], &match) == lwc_error_ok &&
                       match)) {
            parserutils_vector_iterate(vector, ctx);
            track->type = TRACK_SIMPLE;
            track->value = 0;
            track->unit = UNIT_MIN_CONTENT;
            return CSS_OK;
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[MAX_CONTENT], &match) == lwc_error_ok &&
                       match)) {
            parserutils_vector_iterate(vector, ctx);
            track->type = TRACK_SIMPLE;
            track->value = 0;
            track->unit = UNIT_MAX_CONTENT;
            return CSS_OK;
        }
    }

    /* Check for minmax() function */
    if (token->type == CSS_TOKEN_FUNCTION) {
        if ((lwc_string_caseless_isequal(token->idata, c->strings[MINMAX], &match) == lwc_error_ok && match)) {
            /* Parse minmax(min, max) */
            css_fixed min_value = 0, max_value = 0;
            uint32_t min_unit = UNIT_PX, max_unit = UNIT_PX;

            /* Consume 'minmax(' */
            parserutils_vector_iterate(vector, ctx);
            // fprintf(stderr, "DEBUG: Found minmax\n");

            /* Skip whitespace */
            consumeWhitespace(vector, ctx);

            /* Parse min value */
            token = parserutils_vector_peek(vector, *ctx);
            if (token == NULL) {
                *ctx = orig_ctx;
                return CSS_INVALID;
            }

            /* Check for min-content/max-content/auto as min */
            if (token->type == CSS_TOKEN_IDENT) {
                if ((lwc_string_caseless_isequal(token->idata, c->strings[MIN_CONTENT], &match) == lwc_error_ok &&
                        match)) {
                    parserutils_vector_iterate(vector, ctx);
                    min_unit = UNIT_MIN_CONTENT;
                } else if ((lwc_string_caseless_isequal(token->idata, c->strings[MAX_CONTENT], &match) ==
                                   lwc_error_ok &&
                               match)) {
                    parserutils_vector_iterate(vector, ctx);
                    min_unit = UNIT_MAX_CONTENT;
                } else if ((lwc_string_caseless_isequal(token->idata, c->strings[AUTO], &match) == lwc_error_ok &&
                               match)) {
                    parserutils_vector_iterate(vector, ctx);
                    min_unit = UNIT_FR;
                    min_value = INTTOFIX(1);
                } else {
                    *ctx = orig_ctx;
                    return CSS_INVALID;
                }
            } else {
                /* Try to parse as dimension */
                error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, &min_value, &min_unit);
                if (error != CSS_OK) {
                    *ctx = orig_ctx;
                    return CSS_INVALID;
                }
                /* unit returned from css__parse_unit_specifier
                 * is already in bytecode format */
            }

            /* Skip whitespace and comma */
            consumeWhitespace(vector, ctx);
            token = parserutils_vector_peek(vector, *ctx);
            if (token != NULL && tokenIsChar(token, ',')) {
                parserutils_vector_iterate(vector, ctx);
            }
            consumeWhitespace(vector, ctx);

            /* Parse max value */
            token = parserutils_vector_peek(vector, *ctx);
            if (token == NULL) {
                *ctx = orig_ctx;
                return CSS_INVALID;
            }

            /* Check for min-content/max-content/auto/fr as max */
            if (token->type == CSS_TOKEN_IDENT) {
                if ((lwc_string_caseless_isequal(token->idata, c->strings[MIN_CONTENT], &match) == lwc_error_ok &&
                        match)) {
                    parserutils_vector_iterate(vector, ctx);
                    max_unit = UNIT_MIN_CONTENT;
                } else if ((lwc_string_caseless_isequal(token->idata, c->strings[MAX_CONTENT], &match) ==
                                   lwc_error_ok &&
                               match)) {
                    parserutils_vector_iterate(vector, ctx);
                    max_unit = UNIT_MAX_CONTENT;
                } else if ((lwc_string_caseless_isequal(token->idata, c->strings[AUTO], &match) == lwc_error_ok &&
                               match)) {
                    parserutils_vector_iterate(vector, ctx);
                    max_unit = UNIT_FR;
                    max_value = INTTOFIX(1);
                } else {
                    *ctx = orig_ctx;
                    return CSS_INVALID;
                }
            } else {
                /* Try to parse as dimension */
                error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, &max_value, &max_unit);
                if (error != CSS_OK) {
                    *ctx = orig_ctx;
                    return CSS_INVALID;
                }
                /* unit returned from css__parse_unit_specifier
                 * is already in bytecode format */
            }

            /* Skip whitespace and consume closing paren */
            consumeWhitespace(vector, ctx);
            token = parserutils_vector_iterate(vector, ctx);
            if (token == NULL || !tokenIsChar(token, ')')) {
                *ctx = orig_ctx;
                return CSS_INVALID;
            }

            /* Store minmax track */
            track->type = TRACK_MINMAX;
            track->value = min_value;
            track->unit = UNIT_MINMAX; /* Mark as minmax track */
            track->min_unit = min_unit; /* Store actual min unit */
            track->max_value = max_value;
            track->max_unit = max_unit;
            return CSS_OK;
        }
    }

    /* Try to parse a simple dimension/percentage/number */
    error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, /* default unit */
        &track->value, &track->unit);
    if (error == CSS_OK) {
        /* unit returned from css__parse_unit_specifier is
         * already in bytecode format */
        track->type = TRACK_SIMPLE;
        return CSS_OK;
    }

    return CSS_INVALID;
}

css_error css__parse_grid_template_columns_internal(
    css_language *c, const parserutils_vector *vector, int32_t *ctx, css_style *result, enum css_properties_e id)
{
    (void)id;
    int32_t orig_ctx = *ctx;
    css_error error = CSS_OK;
    const css_token *token;
    bool match;
    grid_track_t tracks[MAX_GRID_TRACKS];
    int num_tracks = 0;


    // printf("DEBUG: Entering css__parse_grid_template_columns_internal with ID %d\n", id);
    // fflush(stdout);

    token = parserutils_vector_iterate(vector, ctx);
    if (token == NULL) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    /* Check for keywords first */
    if (token->type == CSS_TOKEN_IDENT) {
        if ((lwc_string_caseless_isequal(token->idata, c->strings[INHERIT], &match) == lwc_error_ok && match)) {
            return css_stylesheet_style_inherit(result, CSS_PROP_GRID_TEMPLATE_COLUMNS);
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[INITIAL], &match) == lwc_error_ok && match)) {
            return css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_TEMPLATE_COLUMNS, 0, GRID_TEMPLATE_NONE);
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[REVERT], &match) == lwc_error_ok && match)) {
            return css_stylesheet_style_revert(result, CSS_PROP_GRID_TEMPLATE_COLUMNS);
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[UNSET], &match) == lwc_error_ok && match)) {
            return css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_TEMPLATE_COLUMNS, FLAG_VALUE_UNSET, 0);
        } else if ((lwc_string_caseless_isequal(token->idata, c->strings[NONE], &match) == lwc_error_ok && match)) {
            return css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_TEMPLATE_COLUMNS, 0, GRID_TEMPLATE_NONE);
        }
    }

    /* Parse track list - reset context to re-parse first token */
    *ctx = orig_ctx;

    while (num_tracks < MAX_GRID_TRACKS) {
        grid_track_t track = {0};
        // fprintf(stderr, "DEBUG: Parsing track %d\n", num_tracks);

        /* Critical: consume whitespace between tracks */
        consumeWhitespace(vector, ctx);

        /* Peek next token to see if we are done */
        token = parserutils_vector_peek(vector, *ctx);
        if (token == NULL) {
            break; /* End of input means end of track list */
        }

        /* Check for repeat() function */
        if (token->type == CSS_TOKEN_FUNCTION) {
            bool match;
            if (lwc_string_caseless_isequal(token->idata, c->strings[REPEAT], &match) == lwc_error_ok && match) {
                /* repeat(N, track-size) */
                parserutils_vector_iterate(vector, ctx); /* consume 'repeat(' */
                consumeWhitespace(vector, ctx);

                /* Parse repeat count - must be a positive integer */
                token = parserutils_vector_peek(vector, *ctx);
                if (token == NULL || token->type != CSS_TOKEN_NUMBER) {
                    break; /* Invalid repeat syntax */
                }

                int32_t repeat_count = 0;
                size_t consumed = 0;
                css_fixed fixed_count = css__number_from_lwc_string(token->idata, true, &consumed);
                if (consumed != lwc_string_length(token->idata)) {
                    break;
                }
                repeat_count = FIXTOINT(fixed_count);
                if (repeat_count <= 0 || repeat_count > MAX_GRID_TRACKS) {
                    break; /* Invalid repeat count */
                }
                parserutils_vector_iterate(vector, ctx); /* consume number */

                /* Consume comma */
                consumeWhitespace(vector, ctx);
                token = parserutils_vector_iterate(vector, ctx);
                if (token == NULL || !tokenIsChar(token, ',')) {
                    break; /* Expected comma */
                }
                consumeWhitespace(vector, ctx);

                /* Parse the track size to repeat */
                grid_track_t repeat_track = {0};
                error = parse_track_size(c, vector, ctx, &repeat_track);
                if (error != CSS_OK) {
                    break; /* Failed to parse track in repeat() */
                }

                /* Consume closing paren */
                consumeWhitespace(vector, ctx);
                token = parserutils_vector_iterate(vector, ctx);
                if (token == NULL || !tokenIsChar(token, ')')) {
                    break; /* Expected closing paren */
                }

                /* Add repeat_count copies of the track */
                // fprintf(stderr, "DEBUG: repeat(%d, track) expanding\n", repeat_count);
                for (int r = 0; r < repeat_count && num_tracks < MAX_GRID_TRACKS; r++) {
                    tracks[num_tracks++] = repeat_track;
                }
                continue; /* Continue to next track in the list */
            }
        }

        error = parse_track_size(c, vector, ctx, &track);
        if (error == CSS_OK) {
            // fprintf(stderr, "DEBUG: Track %d parsed OK. Type=%d\n", num_tracks, track.type);
            tracks[num_tracks++] = track;
        } else {
            // fprintf(stderr, "DEBUG: Track parsing failed for index %d\n", num_tracks);
            /* If we encounter something that isn't a track, stop.
             * But if it's not the end of input, and not a valid
             * track, the property should likely be invalid? CSS
             * syntax: valid list of tracks. If junk is at end,
             * property is invalid.
             */
            break;
        }
    }

    if (num_tracks == 0) {
        *ctx = orig_ctx;
        return CSS_INVALID;
    }

    /* Emit the opcode with track count */
    /* Note: we emit 0 flags */
    error = css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_TEMPLATE_COLUMNS, 0, GRID_TEMPLATE_SET);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }

    /* Emit number of tracks */
    error = css__stylesheet_style_append(result, num_tracks);
    if (error != CSS_OK) {
        *ctx = orig_ctx;
        return error;
    }

    /* Emit each track's data */
    for (int i = 0; i < num_tracks; i++) {
        grid_track_t *t = &tracks[i];

        if (t->type == TRACK_SIMPLE) {
            /* Simple track: value, unit */
            error = css__stylesheet_style_append(result, t->value);
            if (error != CSS_OK) {
                *ctx = orig_ctx;
                return error;
            }
            error = css__stylesheet_style_append(result, t->unit);
            if (error != CSS_OK) {
                *ctx = orig_ctx;
                return error;
            }
        } else {
            /* Minmax track: encode as CSS_UNIT_MINMAX marker,
             * followed by min value, min unit, max value, max unit
             */
            /* First emit a marker value (0) and the MINMAX unit */
            error = css__stylesheet_style_append(result, 0);
            if (error != CSS_OK) {
                *ctx = orig_ctx;
                return error;
            }
            error = css__stylesheet_style_append(result, UNIT_MINMAX);
            if (error != CSS_OK) {
                *ctx = orig_ctx;
                return error;
            }
            /* Then emit min value and unit */
            error = css__stylesheet_style_append(result, t->value);
            if (error != CSS_OK) {
                *ctx = orig_ctx;
                return error;
            }
            error = css__stylesheet_style_append(result, t->min_unit);
            if (error != CSS_OK) {
                *ctx = orig_ctx;
                return error;
            }
            /* Then emit max value and unit */
            error = css__stylesheet_style_append(result, t->max_value);
            if (error != CSS_OK) {
                *ctx = orig_ctx;
                return error;
            }
            error = css__stylesheet_style_append(result, t->max_unit);
            if (error != CSS_OK) {
                *ctx = orig_ctx;
                return error;
            }
        }
    }

    return CSS_OK;
}
