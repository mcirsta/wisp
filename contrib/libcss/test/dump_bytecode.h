/*
 * LibCSS bytecode diagnostics. Licensed under the MIT License.
 * Included by dump.h after its number, unit and counter formatting helpers.
 *
 * Keep stream traversal here separate from property formatting. Keyword-only
 * properties use tables; payload readers consume just their own operand words.
 */
#ifndef test_dump_bytecode_h_
#define test_dump_bytecode_h_

struct dump_bytecode_cursor {
    const css_code_t *next;
    const css_code_t *end;
    css_stylesheet *sheet;
};

static uint32_t dump_bytecode_read(struct dump_bytecode_cursor *cursor)
{
    assert(cursor->next < cursor->end);
    return *cursor->next++;
}

static void dump_bytecode_length(struct dump_bytecode_cursor *cursor, char **ptr)
{
    css_fixed value = (css_fixed)dump_bytecode_read(cursor);
    uint32_t unit = dump_bytecode_read(cursor);
    dump_unit(value, unit, ptr);
}

/* Keyword values carry no operand words. */
static const char *const dump_keywords_align_content[] = {
    [ALIGN_CONTENT_STRETCH] = "stretch",
    [ALIGN_CONTENT_FLEX_START] = "flex-start",
    [ALIGN_CONTENT_FLEX_END] = "flex-end",
    [ALIGN_CONTENT_CENTER] = "center",
    [ALIGN_CONTENT_SPACE_BETWEEN] = "space-between",
    [ALIGN_CONTENT_SPACE_AROUND] = "space-around",
    [ALIGN_CONTENT_SPACE_EVENLY] = "space-evenly",
};

static const char *const dump_keywords_align_items[] = {
    [ALIGN_ITEMS_NORMAL] = "normal",
    [ALIGN_ITEMS_STRETCH] = "stretch",
    [ALIGN_ITEMS_FLEX_START] = "flex-start",
    [ALIGN_ITEMS_FLEX_END] = "flex-end",
    [ALIGN_ITEMS_CENTER] = "center",
    [ALIGN_ITEMS_BASELINE] = "baseline",
};

static const char *const dump_keywords_align_self[] = {
    [ALIGN_SELF_NORMAL] = "normal",
    [ALIGN_SELF_STRETCH] = "stretch",
    [ALIGN_SELF_FLEX_START] = "flex-start",
    [ALIGN_SELF_FLEX_END] = "flex-end",
    [ALIGN_SELF_CENTER] = "center",
    [ALIGN_SELF_BASELINE] = "baseline",
    [ALIGN_SELF_AUTO] = "auto",
};

static const char *const dump_keywords_background_attachment[] = {
    [BACKGROUND_ATTACHMENT_FIXED] = "fixed",
    [BACKGROUND_ATTACHMENT_SCROLL] = "scroll",
};

static const char *const dump_keywords_background_repeat[] = {
    [BACKGROUND_REPEAT_NO_REPEAT] = "no-repeat",
    [BACKGROUND_REPEAT_REPEAT_X] = "repeat-x",
    [BACKGROUND_REPEAT_REPEAT_Y] = "repeat-y",
    [BACKGROUND_REPEAT_REPEAT] = "repeat",
};

static const char *const dump_keywords_border_collapse[] = {
    [BORDER_COLLAPSE_SEPARATE] = "separate",
    [BORDER_COLLAPSE_COLLAPSE] = "collapse",
};

static const char *const dump_keywords_box_sizing[] = {
    [BOX_SIZING_CONTENT_BOX] = "content-box",
    [BOX_SIZING_BORDER_BOX] = "border-box",
};

static const char *const dump_keywords_break_inside[] = {
    [BREAK_INSIDE_AUTO] = "auto",
    [BREAK_INSIDE_AVOID] = "avoid",
    [BREAK_INSIDE_AVOID_PAGE] = "avoid-page",
    [BREAK_INSIDE_AVOID_COLUMN] = "avoid-column",
};

static const char *const dump_keywords_caption_side[] = {
    [CAPTION_SIDE_TOP] = "top",
    [CAPTION_SIDE_BOTTOM] = "bottom",
};

static const char *const dump_keywords_clear[] = {
    [CLEAR_NONE] = "none",
    [CLEAR_LEFT] = "left",
    [CLEAR_RIGHT] = "right",
    [CLEAR_BOTH] = "both",
};

static const char *const dump_keywords_column_fill[] = {
    [COLUMN_FILL_BALANCE] = "balance",
    [COLUMN_FILL_AUTO] = "auto",
};

static const char *const dump_keywords_column_span[] = {
    [COLUMN_SPAN_NONE] = "none",
    [COLUMN_SPAN_ALL] = "all",
};

static const char *const dump_keywords_direction[] = {
    [DIRECTION_LTR] = "ltr",
    [DIRECTION_RTL] = "rtl",
};

static const char *const dump_keywords_display[] = {
    [DISPLAY_INLINE] = "inline",
    [DISPLAY_BLOCK] = "block",
    [DISPLAY_LIST_ITEM] = "list-item",
    [DISPLAY_RUN_IN] = "run-in",
    [DISPLAY_INLINE_BLOCK] = "inline-block",
    [DISPLAY_TABLE] = "table",
    [DISPLAY_INLINE_TABLE] = "inline-table",
    [DISPLAY_TABLE_ROW_GROUP] = "table-row-group",
    [DISPLAY_TABLE_HEADER_GROUP] = "table-header-group",
    [DISPLAY_TABLE_FOOTER_GROUP] = "table-footer-group",
    [DISPLAY_TABLE_ROW] = "table-row",
    [DISPLAY_TABLE_COLUMN_GROUP] = "table-column-group",
    [DISPLAY_TABLE_COLUMN] = "table-column",
    [DISPLAY_TABLE_CELL] = "table-cell",
    [DISPLAY_TABLE_CAPTION] = "table-caption",
    [DISPLAY_NONE] = "none",
    [DISPLAY_FLEX] = "flex",
    [DISPLAY_INLINE_FLEX] = "inline-flex",
    [DISPLAY_GRID] = "grid",
    [DISPLAY_INLINE_GRID] = "inline-grid",
    [DISPLAY_FLOW_ROOT] = "flow-root",
};

static const char *const dump_keywords_empty_cells[] = {
    [EMPTY_CELLS_SHOW] = "show",
    [EMPTY_CELLS_HIDE] = "hide",
};

static const char *const dump_keywords_flex_direction[] = {
    [FLEX_DIRECTION_ROW] = "row",
    [FLEX_DIRECTION_COLUMN] = "column",
    [FLEX_DIRECTION_ROW_REVERSE] = "row-reverse",
    [FLEX_DIRECTION_COLUMN_REVERSE] = "column-reverse",
};

static const char *const dump_keywords_flex_wrap[] = {
    [FLEX_WRAP_NOWRAP] = "nowrap",
    [FLEX_WRAP_WRAP] = "wrap",
    [FLEX_WRAP_WRAP_REVERSE] = "wrap-reverse",
};

static const char *const dump_keywords_float[] = {
    [FLOAT_LEFT] = "left",
    [FLOAT_RIGHT] = "right",
    [FLOAT_NONE] = "none",
};

static const char *const dump_keywords_font_style[] = {
    [FONT_STYLE_NORMAL] = "normal",
    [FONT_STYLE_ITALIC] = "italic",
    [FONT_STYLE_OBLIQUE] = "oblique",
};

static const char *const dump_keywords_font_variant[] = {
    [FONT_VARIANT_NORMAL] = "normal",
    [FONT_VARIANT_SMALL_CAPS] = "small-caps",
};

static const char *const dump_keywords_font_weight[] = {
    [FONT_WEIGHT_NORMAL] = "normal",
    [FONT_WEIGHT_BOLD] = "bold",
    [FONT_WEIGHT_BOLDER] = "bolder",
    [FONT_WEIGHT_LIGHTER] = "lighter",
    [FONT_WEIGHT_100] = "100",
    [FONT_WEIGHT_200] = "200",
    [FONT_WEIGHT_300] = "300",
    [FONT_WEIGHT_400] = "400",
    [FONT_WEIGHT_500] = "500",
    [FONT_WEIGHT_600] = "600",
    [FONT_WEIGHT_700] = "700",
    [FONT_WEIGHT_800] = "800",
    [FONT_WEIGHT_900] = "900",
};

static const char *const dump_keywords_justify_content[] = {
    [JUSTIFY_CONTENT_FLEX_START] = "flex-start",
    [JUSTIFY_CONTENT_FLEX_END] = "flex-end",
    [JUSTIFY_CONTENT_CENTER] = "center",
    [JUSTIFY_CONTENT_SPACE_BETWEEN] = "space-between",
    [JUSTIFY_CONTENT_SPACE_AROUND] = "space-around",
    [JUSTIFY_CONTENT_SPACE_EVENLY] = "space-evenly",
};

static const char *const dump_keywords_list_style_position[] = {
    [LIST_STYLE_POSITION_INSIDE] = "inside",
    [LIST_STYLE_POSITION_OUTSIDE] = "outside",
};

static const char *const dump_keywords_list_style_type[] = {
    [LIST_STYLE_TYPE_DISC] = "disc",
    [LIST_STYLE_TYPE_CIRCLE] = "circle",
    [LIST_STYLE_TYPE_SQUARE] = "square",
    [LIST_STYLE_TYPE_DECIMAL] = "decimal",
    [LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO] = "decimal-leading-zero",
    [LIST_STYLE_TYPE_LOWER_ROMAN] = "lower-roman",
    [LIST_STYLE_TYPE_UPPER_ROMAN] = "upper-roman",
    [LIST_STYLE_TYPE_LOWER_GREEK] = "lower-greek",
    [LIST_STYLE_TYPE_LOWER_LATIN] = "lower-latin",
    [LIST_STYLE_TYPE_UPPER_LATIN] = "upper-latin",
    [LIST_STYLE_TYPE_ARMENIAN] = "armenian",
    [LIST_STYLE_TYPE_GEORGIAN] = "georgian",
    [LIST_STYLE_TYPE_LOWER_ALPHA] = "lower-alpha",
    [LIST_STYLE_TYPE_UPPER_ALPHA] = "upper-alpha",
    [LIST_STYLE_TYPE_NONE] = "none",
};

static const char *const dump_keywords_overflow_x[] = {
    [OVERFLOW_VISIBLE] = "visible",
    [OVERFLOW_HIDDEN] = "hidden",
    [OVERFLOW_SCROLL] = "scroll",
    [OVERFLOW_AUTO] = "auto",
};

static const char *const dump_keywords_page_break_inside[] = {
    [PAGE_BREAK_INSIDE_AUTO] = "auto",
    [PAGE_BREAK_INSIDE_AVOID] = "avoid",
};

static const char *const dump_keywords_position[] = {
    [POSITION_STATIC] = "static",
    [POSITION_RELATIVE] = "relative",
    [POSITION_ABSOLUTE] = "absolute",
    [POSITION_FIXED] = "fixed",
};

static const char *const dump_keywords_speak_header[] = {
    [SPEAK_HEADER_ONCE] = "once",
    [SPEAK_HEADER_ALWAYS] = "always",
};

static const char *const dump_keywords_speak_numeral[] = {
    [SPEAK_NUMERAL_DIGITS] = "digits",
    [SPEAK_NUMERAL_CONTINUOUS] = "continuous",
};

static const char *const dump_keywords_speak_punctuation[] = {
    [SPEAK_PUNCTUATION_CODE] = "code",
    [SPEAK_PUNCTUATION_NONE] = "none",
};

static const char *const dump_keywords_speak[] = {
    [SPEAK_NORMAL] = "normal",
    [SPEAK_NONE] = "none",
    [SPEAK_SPELL_OUT] = "spell-out",
};

static const char *const dump_keywords_table_layout[] = {
    [TABLE_LAYOUT_AUTO] = "auto",
    [TABLE_LAYOUT_FIXED] = "fixed",
};

static const char *const dump_keywords_text_align[] = {
    [TEXT_ALIGN_LEFT] = "left",
    [TEXT_ALIGN_RIGHT] = "right",
    [TEXT_ALIGN_CENTER] = "center",
    [TEXT_ALIGN_JUSTIFY] = "justify",
    [TEXT_ALIGN_LIBCSS_LEFT] = "-libcss-left",
    [TEXT_ALIGN_LIBCSS_CENTER] = "-libcss-center",
    [TEXT_ALIGN_LIBCSS_RIGHT] = "-libcss-right",
};

static const char *const dump_keywords_text_transform[] = {
    [TEXT_TRANSFORM_CAPITALIZE] = "capitalize",
    [TEXT_TRANSFORM_UPPERCASE] = "uppercase",
    [TEXT_TRANSFORM_LOWERCASE] = "lowercase",
    [TEXT_TRANSFORM_NONE] = "none",
};

static const char *const dump_keywords_unicode_bidi[] = {
    [UNICODE_BIDI_NORMAL] = "normal",
    [UNICODE_BIDI_EMBED] = "embed",
    [UNICODE_BIDI_BIDI_OVERRIDE] = "bidi-override",
};

static const char *const dump_keywords_visibility[] = {
    [VISIBILITY_VISIBLE] = "visible",
    [VISIBILITY_HIDDEN] = "hidden",
    [VISIBILITY_COLLAPSE] = "collapse",
};

static const char *const dump_keywords_content_visibility[] = {
    [CONTENT_VISIBILITY_VISIBLE] = "visible",
    [CONTENT_VISIBILITY_HIDDEN] = "hidden",
};

static const char *const dump_keywords_white_space[] = {
    [WHITE_SPACE_NORMAL] = "normal",
    [WHITE_SPACE_PRE] = "pre",
    [WHITE_SPACE_NOWRAP] = "nowrap",
    [WHITE_SPACE_PRE_WRAP] = "pre-wrap",
    [WHITE_SPACE_PRE_LINE] = "pre-line",
};

static const char *const dump_keywords_writing_mode[] = {
    [WRITING_MODE_HORIZONTAL_TB] = "horizontal-tb",
    [WRITING_MODE_VERTICAL_RL] = "vertical-rl",
    [WRITING_MODE_VERTICAL_LR] = "vertical-lr",
};

/* Readers for properties with operands or property-specific flags. */
static void dump_bytecode_azimuth(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value & ~AZIMUTH_BEHIND) {
    case AZIMUTH_ANGLE:
        dump_bytecode_length(cursor, ptr);
        break;
    case AZIMUTH_LEFTWARDS:
        *ptr += sprintf(*ptr, "leftwards");
        break;
    case AZIMUTH_RIGHTWARDS:
        *ptr += sprintf(*ptr, "rightwards");
        break;
    case AZIMUTH_LEFT_SIDE:
        *ptr += sprintf(*ptr, "left-side");
        break;
    case AZIMUTH_FAR_LEFT:
        *ptr += sprintf(*ptr, "far-left");
        break;
    case AZIMUTH_LEFT:
        *ptr += sprintf(*ptr, "left");
        break;
    case AZIMUTH_CENTER_LEFT:
        *ptr += sprintf(*ptr, "center-left");
        break;
    case AZIMUTH_CENTER:
        *ptr += sprintf(*ptr, "center");
        break;
    case AZIMUTH_CENTER_RIGHT:
        *ptr += sprintf(*ptr, "center-right");
        break;
    case AZIMUTH_RIGHT:
        *ptr += sprintf(*ptr, "right");
        break;
    case AZIMUTH_FAR_RIGHT:
        *ptr += sprintf(*ptr, "far-right");
        break;
    case AZIMUTH_RIGHT_SIDE:
        *ptr += sprintf(*ptr, "right-side");
        break;
    }
    if (value & AZIMUTH_BEHIND)
        *ptr += sprintf(*ptr, " behind");
}

static void dump_bytecode_border_color(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    assert(BACKGROUND_COLOR_TRANSPARENT == (enum op_background_color)BORDER_COLOR_TRANSPARENT);
    assert(BACKGROUND_COLOR_CURRENT_COLOR == (enum op_background_color)BORDER_COLOR_CURRENT_COLOR);
    assert(BACKGROUND_COLOR_SET == (enum op_background_color)BORDER_COLOR_SET);

    switch (value) {
    case BACKGROUND_COLOR_TRANSPARENT:
        *ptr += sprintf(*ptr, "transparent");
        break;
    case BACKGROUND_COLOR_CURRENT_COLOR:
        *ptr += sprintf(*ptr, "currentColor");
        break;
    case BACKGROUND_COLOR_SET: {
        uint32_t colour = dump_bytecode_read(cursor);
        *ptr += sprintf(*ptr, "#%08x", colour);
    } break;
    }
}

static void dump_bytecode_background_image(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    assert(BACKGROUND_IMAGE_NONE == (enum op_background_image)CUE_AFTER_NONE);
    assert(BACKGROUND_IMAGE_URI == (enum op_background_image)CUE_AFTER_URI);
    assert(BACKGROUND_IMAGE_NONE == (enum op_background_image)CUE_BEFORE_NONE);
    assert(BACKGROUND_IMAGE_URI == (enum op_background_image)CUE_BEFORE_URI);
    assert(BACKGROUND_IMAGE_NONE == (enum op_background_image)LIST_STYLE_IMAGE_NONE);
    assert(BACKGROUND_IMAGE_URI == (enum op_background_image)LIST_STYLE_IMAGE_URI);

    switch (value) {
    case BACKGROUND_IMAGE_NONE:
        *ptr += sprintf(*ptr, "none");
        break;
    case BACKGROUND_IMAGE_URI: {
        uint32_t snum = dump_bytecode_read(cursor);
        lwc_string *he;
        css__stylesheet_string_get(cursor->sheet, snum, &he);
        *ptr += sprintf(*ptr, "url('%.*s')", (int)lwc_string_length(he), lwc_string_data(he));
    } break;
    case BACKGROUND_IMAGE_LINEAR_GRADIENT:
    case BACKGROUND_IMAGE_RADIAL_GRADIENT: {
        uint32_t header = dump_bytecode_read(cursor);
        uint32_t count = dump_bytecode_read(cursor);
        *ptr += sprintf(
            *ptr, "%s-gradient(%u", value == BACKGROUND_IMAGE_LINEAR_GRADIENT ? "linear" : "radial", header);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t color = dump_bytecode_read(cursor);
            css_fixed position = (css_fixed)dump_bytecode_read(cursor);
            uint32_t unit = dump_bytecode_read(cursor);
            *ptr += sprintf(*ptr, ", #%08x", color);
            if (unit != 0) {
                *ptr += sprintf(*ptr, " ");
                dump_unit(position, unit, ptr);
            }
        }
        *ptr += sprintf(*ptr, ")");
    } break;
    }
}

static void dump_bytecode_background_position(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value & 0xf0) {
    case BACKGROUND_POSITION_HORZ_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    case BACKGROUND_POSITION_HORZ_CENTER:
        *ptr += sprintf(*ptr, "center");
        break;
    case BACKGROUND_POSITION_HORZ_RIGHT:
        *ptr += sprintf(*ptr, "right");
        break;
    case BACKGROUND_POSITION_HORZ_LEFT:
        *ptr += sprintf(*ptr, "left");
        break;
    }
    *ptr += sprintf(*ptr, " ");
    switch (value & 0x0f) {
    case BACKGROUND_POSITION_VERT_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    case BACKGROUND_POSITION_VERT_CENTER:
        *ptr += sprintf(*ptr, "center");
        break;
    case BACKGROUND_POSITION_VERT_BOTTOM:
        *ptr += sprintf(*ptr, "bottom");
        break;
    case BACKGROUND_POSITION_VERT_TOP:
        *ptr += sprintf(*ptr, "top");
        break;
    }
}

static void dump_bytecode_border_spacing(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case BORDER_SPACING_SET: {
        dump_bytecode_length(cursor, ptr);

        dump_bytecode_length(cursor, ptr);
    } break;
    }
}

static void dump_bytecode_border_style(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    (void)cursor;
    assert(BORDER_STYLE_NONE == (enum op_border_style)OUTLINE_STYLE_NONE);
    assert(BORDER_STYLE_NONE == (enum op_border_style)COLUMN_RULE_STYLE_NONE);
    assert(BORDER_STYLE_HIDDEN == (enum op_border_style)OUTLINE_STYLE_HIDDEN);
    assert(BORDER_STYLE_HIDDEN == (enum op_border_style)COLUMN_RULE_STYLE_HIDDEN);
    assert(BORDER_STYLE_DOTTED == (enum op_border_style)OUTLINE_STYLE_DOTTED);
    assert(BORDER_STYLE_DOTTED == (enum op_border_style)COLUMN_RULE_STYLE_DOTTED);
    assert(BORDER_STYLE_DASHED == (enum op_border_style)OUTLINE_STYLE_DASHED);
    assert(BORDER_STYLE_DASHED == (enum op_border_style)COLUMN_RULE_STYLE_DASHED);
    assert(BORDER_STYLE_SOLID == (enum op_border_style)OUTLINE_STYLE_SOLID);
    assert(BORDER_STYLE_SOLID == (enum op_border_style)COLUMN_RULE_STYLE_SOLID);
    assert(BORDER_STYLE_DOUBLE == (enum op_border_style)OUTLINE_STYLE_DOUBLE);
    assert(BORDER_STYLE_DOUBLE == (enum op_border_style)COLUMN_RULE_STYLE_DOUBLE);
    assert(BORDER_STYLE_GROOVE == (enum op_border_style)OUTLINE_STYLE_GROOVE);
    assert(BORDER_STYLE_GROOVE == (enum op_border_style)COLUMN_RULE_STYLE_GROOVE);
    assert(BORDER_STYLE_RIDGE == (enum op_border_style)OUTLINE_STYLE_RIDGE);
    assert(BORDER_STYLE_RIDGE == (enum op_border_style)COLUMN_RULE_STYLE_RIDGE);
    assert(BORDER_STYLE_INSET == (enum op_border_style)OUTLINE_STYLE_INSET);
    assert(BORDER_STYLE_INSET == (enum op_border_style)COLUMN_RULE_STYLE_INSET);
    assert(BORDER_STYLE_OUTSET == (enum op_border_style)OUTLINE_STYLE_OUTSET);
    assert(BORDER_STYLE_OUTSET == (enum op_border_style)COLUMN_RULE_STYLE_OUTSET);

    switch (value) {
    case BORDER_STYLE_NONE:
        *ptr += sprintf(*ptr, "none");
        break;
    case BORDER_STYLE_HIDDEN:
        *ptr += sprintf(*ptr, "hidden");
        break;
    case BORDER_STYLE_DOTTED:
        *ptr += sprintf(*ptr, "dotted");
        break;
    case BORDER_STYLE_DASHED:
        *ptr += sprintf(*ptr, "dashed");
        break;
    case BORDER_STYLE_SOLID:
        *ptr += sprintf(*ptr, "solid");
        break;
    case BORDER_STYLE_DOUBLE:
        *ptr += sprintf(*ptr, "double");
        break;
    case BORDER_STYLE_GROOVE:
        *ptr += sprintf(*ptr, "groove");
        break;
    case BORDER_STYLE_RIDGE:
        *ptr += sprintf(*ptr, "ridge");
        break;
    case BORDER_STYLE_INSET:
        *ptr += sprintf(*ptr, "inset");
        break;
    case BORDER_STYLE_OUTSET:
        *ptr += sprintf(*ptr, "outset");
        break;
    }
}

static void dump_bytecode_border_width(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    assert(BORDER_WIDTH_SET == (enum op_border_width)OUTLINE_WIDTH_SET);
    assert(BORDER_WIDTH_THIN == (enum op_border_width)OUTLINE_WIDTH_THIN);
    assert(BORDER_WIDTH_MEDIUM == (enum op_border_width)OUTLINE_WIDTH_MEDIUM);
    assert(BORDER_WIDTH_THICK == (enum op_border_width)OUTLINE_WIDTH_THICK);

    switch (value) {
    case BORDER_WIDTH_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    case BORDER_WIDTH_THIN:
        *ptr += sprintf(*ptr, "thin");
        break;
    case BORDER_WIDTH_MEDIUM:
        *ptr += sprintf(*ptr, "medium");
        break;
    case BORDER_WIDTH_THICK:
        *ptr += sprintf(*ptr, "thick");
        break;
    }
}

static void dump_bytecode_length_auto(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    assert(BOTTOM_SET == (enum op_bottom)LEFT_SET);
    assert(BOTTOM_AUTO == (enum op_bottom)LEFT_AUTO);
    assert(BOTTOM_SET == (enum op_bottom)RIGHT_SET);
    assert(BOTTOM_AUTO == (enum op_bottom)RIGHT_AUTO);
    assert(BOTTOM_SET == (enum op_bottom)TOP_SET);
    assert(BOTTOM_AUTO == (enum op_bottom)TOP_AUTO);
    assert(BOTTOM_SET == (enum op_bottom)HEIGHT_SET);
    assert(BOTTOM_AUTO == (enum op_bottom)HEIGHT_AUTO);
    assert(BOTTOM_SET == (enum op_bottom)MARGIN_SET);
    assert(BOTTOM_AUTO == (enum op_bottom)MARGIN_AUTO);
    assert(BOTTOM_SET == (enum op_bottom)WIDTH_SET);
    assert(BOTTOM_AUTO == (enum op_bottom)WIDTH_AUTO);
    assert(BOTTOM_SET == (enum op_bottom)COLUMN_WIDTH_SET);
    assert(BOTTOM_AUTO == (enum op_bottom)COLUMN_WIDTH_AUTO);

    switch (value) {
    case BOTTOM_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    case BOTTOM_AUTO:
        *ptr += sprintf(*ptr, "auto");
        break;
    }
}

static void dump_bytecode_break_after(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    (void)cursor;
    assert(BREAK_AFTER_AUTO == (enum op_break_after)BREAK_BEFORE_AUTO);
    assert(BREAK_AFTER_ALWAYS == (enum op_break_after)BREAK_BEFORE_ALWAYS);
    assert(BREAK_AFTER_AVOID == (enum op_break_after)BREAK_BEFORE_AVOID);
    assert(BREAK_AFTER_LEFT == (enum op_break_after)BREAK_BEFORE_LEFT);
    assert(BREAK_AFTER_RIGHT == (enum op_break_after)BREAK_BEFORE_RIGHT);
    assert(BREAK_AFTER_PAGE == (enum op_break_after)BREAK_BEFORE_PAGE);
    assert(BREAK_AFTER_COLUMN == (enum op_break_after)BREAK_BEFORE_COLUMN);
    assert(BREAK_AFTER_AVOID_PAGE == (enum op_break_after)BREAK_BEFORE_AVOID_PAGE);
    assert(BREAK_AFTER_AVOID_COLUMN == (enum op_break_after)BREAK_BEFORE_AVOID_COLUMN);

    switch (value) {
    case BREAK_AFTER_AUTO:
        *ptr += sprintf(*ptr, "auto");
        break;
    case BREAK_AFTER_ALWAYS:
        *ptr += sprintf(*ptr, "always");
        break;
    case BREAK_AFTER_AVOID:
        *ptr += sprintf(*ptr, "avoid");
        break;
    case BREAK_AFTER_LEFT:
        *ptr += sprintf(*ptr, "left");
        break;
    case BREAK_AFTER_RIGHT:
        *ptr += sprintf(*ptr, "right");
        break;
    case BREAK_AFTER_PAGE:
        *ptr += sprintf(*ptr, "page");
        break;
    case BREAK_AFTER_COLUMN:
        *ptr += sprintf(*ptr, "column");
        break;
    case BREAK_AFTER_AVOID_PAGE:
        *ptr += sprintf(*ptr, "avoid-page");
        break;
    case BREAK_AFTER_AVOID_COLUMN:
        *ptr += sprintf(*ptr, "avoid-column");
        break;
    }
}

static void dump_bytecode_clip(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    if ((value & CLIP_SHAPE_MASK) == CLIP_SHAPE_RECT) {
        *ptr += sprintf(*ptr, "rect(");
        if (value & CLIP_RECT_TOP_AUTO) {
            *ptr += sprintf(*ptr, "auto");
        } else {
            dump_bytecode_length(cursor, ptr);
        }
        *ptr += sprintf(*ptr, ", ");
        if (value & CLIP_RECT_RIGHT_AUTO) {
            *ptr += sprintf(*ptr, "auto");
        } else {
            dump_bytecode_length(cursor, ptr);
        }
        *ptr += sprintf(*ptr, ", ");
        if (value & CLIP_RECT_BOTTOM_AUTO) {
            *ptr += sprintf(*ptr, "auto");
        } else {
            dump_bytecode_length(cursor, ptr);
        }
        *ptr += sprintf(*ptr, ", ");
        if (value & CLIP_RECT_LEFT_AUTO) {
            *ptr += sprintf(*ptr, "auto");
        } else {
            dump_bytecode_length(cursor, ptr);
        }
        *ptr += sprintf(*ptr, ")");
    } else
        *ptr += sprintf(*ptr, "auto");
}

static void dump_bytecode_color(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case COLOR_TRANSPARENT:
        *ptr += sprintf(*ptr, "transparent");
        break;
    case COLOR_CURRENT_COLOR:
        *ptr += sprintf(*ptr, "currentColor");
        break;
    case COLOR_SET: {
        uint32_t colour = dump_bytecode_read(cursor);
        *ptr += sprintf(*ptr, "#%08x", colour);
    } break;
    }
}

static void dump_bytecode_column_count(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case COLUMN_COUNT_SET: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    case COLUMN_COUNT_AUTO:
        *ptr += sprintf(*ptr, "auto");
        break;
    }
}

static void dump_bytecode_column_gap(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case COLUMN_GAP_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    case COLUMN_GAP_NORMAL:
        *ptr += sprintf(*ptr, "normal");
        break;
    }
}

static void dump_bytecode_content(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    if (value == CONTENT_NORMAL) {
        *ptr += sprintf(*ptr, "normal");
        return;
    } else if (value == CONTENT_NONE) {
        *ptr += sprintf(*ptr, "none");
        return;
    }

    while (value != CONTENT_NORMAL) {
        uint32_t snum;
        lwc_string *he;
        const char *end = "";

        switch (value & 0xff) {
        case CONTENT_COUNTER:
            snum = dump_bytecode_read(cursor);
            css__stylesheet_string_get(cursor->sheet, snum, &he);
            dump_counter(he, value, ptr);
            break;

        case CONTENT_COUNTERS: {
            lwc_string *sep;
            snum = dump_bytecode_read(cursor);
            css__stylesheet_string_get(cursor->sheet, snum, &he);

            uint32_t separator = dump_bytecode_read(cursor);
            css__stylesheet_string_get(cursor->sheet, separator, &sep);

            dump_counters(he, sep, value, ptr);
        } break;

        case CONTENT_URI:
        case CONTENT_ATTR:
        case CONTENT_STRING:
            snum = dump_bytecode_read(cursor);
            css__stylesheet_string_get(cursor->sheet, snum, &he);
            if (value == CONTENT_URI)
                *ptr += sprintf(*ptr, "url(");
            if (value == CONTENT_ATTR)
                *ptr += sprintf(*ptr, "attr(");
            if (value != CONTENT_STRING)
                end = ")";

            *ptr += sprintf(*ptr, "'%.*s'%s", (int)lwc_string_length(he), lwc_string_data(he), end);
            break;

        case CONTENT_OPEN_QUOTE:
            *ptr += sprintf(*ptr, "open-quote");
            break;

        case CONTENT_CLOSE_QUOTE:
            *ptr += sprintf(*ptr, "close-quote");
            break;

        case CONTENT_NO_OPEN_QUOTE:
            *ptr += sprintf(*ptr, "no-open-quote");
            break;

        case CONTENT_NO_CLOSE_QUOTE:
            *ptr += sprintf(*ptr, "no-close-quote");
            break;
        }

        value = dump_bytecode_read(cursor);

        if (value != CONTENT_NORMAL)
            *ptr += sprintf(*ptr, " ");
    }
}

static void dump_bytecode_counter(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    assert(COUNTER_INCREMENT_NONE == (enum op_counter_increment)COUNTER_RESET_NONE);
    assert(COUNTER_INCREMENT_NAMED == (enum op_counter_increment)COUNTER_RESET_NAMED);

    switch (value) {
    case COUNTER_INCREMENT_NAMED:
        while (value != COUNTER_INCREMENT_NONE) {
            css_fixed val;
            uint32_t snum = dump_bytecode_read(cursor);
            lwc_string *he;
            css__stylesheet_string_get(cursor->sheet, snum, &he);
            *ptr += sprintf(*ptr, "%.*s ", (int)lwc_string_length(he), lwc_string_data(he));
            val = (css_fixed)dump_bytecode_read(cursor);
            dump_number(val, ptr);

            value = dump_bytecode_read(cursor);

            if (value != COUNTER_INCREMENT_NONE)
                *ptr += sprintf(*ptr, " ");
        }
        break;
    case COUNTER_INCREMENT_NONE:
        *ptr += sprintf(*ptr, "none");
        break;
    }
}

static void dump_bytecode_cursor(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    while (value == CURSOR_URI) {
        uint32_t snum = dump_bytecode_read(cursor);
        lwc_string *he;
        css__stylesheet_string_get(cursor->sheet, snum, &he);
        *ptr += sprintf(*ptr, "url('%.*s'), ", (int)lwc_string_length(he), lwc_string_data(he));

        value = dump_bytecode_read(cursor);
    }

    switch (value) {
    case CURSOR_AUTO:
        *ptr += sprintf(*ptr, "auto");
        break;
    case CURSOR_CROSSHAIR:
        *ptr += sprintf(*ptr, "crosshair");
        break;
    case CURSOR_DEFAULT:
        *ptr += sprintf(*ptr, "default");
        break;
    case CURSOR_POINTER:
        *ptr += sprintf(*ptr, "pointer");
        break;
    case CURSOR_MOVE:
        *ptr += sprintf(*ptr, "move");
        break;
    case CURSOR_E_RESIZE:
        *ptr += sprintf(*ptr, "e-resize");
        break;
    case CURSOR_NE_RESIZE:
        *ptr += sprintf(*ptr, "ne-resize");
        break;
    case CURSOR_NW_RESIZE:
        *ptr += sprintf(*ptr, "nw-resize");
        break;
    case CURSOR_N_RESIZE:
        *ptr += sprintf(*ptr, "n-resize");
        break;
    case CURSOR_SE_RESIZE:
        *ptr += sprintf(*ptr, "se-resize");
        break;
    case CURSOR_SW_RESIZE:
        *ptr += sprintf(*ptr, "sw-resize");
        break;
    case CURSOR_S_RESIZE:
        *ptr += sprintf(*ptr, "s-resize");
        break;
    case CURSOR_W_RESIZE:
        *ptr += sprintf(*ptr, "w-resize");
        break;
    case CURSOR_TEXT:
        *ptr += sprintf(*ptr, "text");
        break;
    case CURSOR_WAIT:
        *ptr += sprintf(*ptr, "wait");
        break;
    case CURSOR_HELP:
        *ptr += sprintf(*ptr, "help");
        break;
    case CURSOR_PROGRESS:
        *ptr += sprintf(*ptr, "progress");
        break;
    }
}

static void dump_bytecode_elevation(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case ELEVATION_ANGLE:
        dump_bytecode_length(cursor, ptr);
        break;
    case ELEVATION_BELOW:
        *ptr += sprintf(*ptr, "below");
        break;
    case ELEVATION_LEVEL:
        *ptr += sprintf(*ptr, "level");
        break;
    case ELEVATION_ABOVE:
        *ptr += sprintf(*ptr, "above");
        break;
    case ELEVATION_HIGHER:
        *ptr += sprintf(*ptr, "higher");
        break;
    case ELEVATION_LOWER:
        *ptr += sprintf(*ptr, "lower");
        break;
    }
}

static void dump_bytecode_fill_opacity(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case FILL_OPACITY_SET: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    }
}

static void dump_bytecode_stroke_opacity(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case STROKE_OPACITY_SET: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    }
}

static void dump_bytecode_flex_basis(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case FLEX_BASIS_AUTO:
        *ptr += sprintf(*ptr, "auto");
        break;
    case FLEX_BASIS_CONTENT:
        *ptr += sprintf(*ptr, "content");
        break;
    case FLEX_BASIS_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    }
}

static void dump_bytecode_flex_grow(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case FLEX_GROW_SET: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    }
}

static void dump_bytecode_flex_shrink(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case FLEX_SHRINK_SET: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    }
}

static void dump_bytecode_font_family(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    while (value != FONT_FAMILY_END) {
        switch (value) {
        case FONT_FAMILY_STRING:
        case FONT_FAMILY_IDENT_LIST: {
            uint32_t snum = dump_bytecode_read(cursor);
            lwc_string *he;
            css__stylesheet_string_get(cursor->sheet, snum, &he);
            *ptr += sprintf(*ptr, "'%.*s'", (int)lwc_string_length(he), lwc_string_data(he));
        } break;
        case FONT_FAMILY_SERIF:
            *ptr += sprintf(*ptr, "serif");
            break;
        case FONT_FAMILY_SANS_SERIF:
            *ptr += sprintf(*ptr, "sans-serif");
            break;
        case FONT_FAMILY_CURSIVE:
            *ptr += sprintf(*ptr, "cursive");
            break;
        case FONT_FAMILY_FANTASY:
            *ptr += sprintf(*ptr, "fantasy");
            break;
        case FONT_FAMILY_MONOSPACE:
            *ptr += sprintf(*ptr, "monospace");
            break;
        }

        value = dump_bytecode_read(cursor);

        if (value != FONT_FAMILY_END)
            *ptr += sprintf(*ptr, ", ");
    }
}

static void dump_bytecode_font_size(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case FONT_SIZE_DIMENSION:
        dump_bytecode_length(cursor, ptr);
        break;
    case FONT_SIZE_XX_SMALL:
        *ptr += sprintf(*ptr, "xx-small");
        break;
    case FONT_SIZE_X_SMALL:
        *ptr += sprintf(*ptr, "x-small");
        break;
    case FONT_SIZE_SMALL:
        *ptr += sprintf(*ptr, "small");
        break;
    case FONT_SIZE_MEDIUM:
        *ptr += sprintf(*ptr, "medium");
        break;
    case FONT_SIZE_LARGE:
        *ptr += sprintf(*ptr, "large");
        break;
    case FONT_SIZE_X_LARGE:
        *ptr += sprintf(*ptr, "x-large");
        break;
    case FONT_SIZE_XX_LARGE:
        *ptr += sprintf(*ptr, "xx-large");
        break;
    case FONT_SIZE_LARGER:
        *ptr += sprintf(*ptr, "larger");
        break;
    case FONT_SIZE_SMALLER:
        *ptr += sprintf(*ptr, "smaller");
        break;
    }
}

static void dump_bytecode_letter_spacing(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    assert(LETTER_SPACING_SET == (enum op_letter_spacing)WORD_SPACING_SET);
    assert(LETTER_SPACING_NORMAL == (enum op_letter_spacing)WORD_SPACING_NORMAL);

    switch (value) {
    case LETTER_SPACING_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    case LETTER_SPACING_NORMAL:
        *ptr += sprintf(*ptr, "normal");
        break;
    }
}

static void dump_bytecode_line_height(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case LINE_HEIGHT_NUMBER: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    case LINE_HEIGHT_DIMENSION:
        dump_bytecode_length(cursor, ptr);
        break;
    case LINE_HEIGHT_NORMAL:
        *ptr += sprintf(*ptr, "normal");
        break;
    }
}

static void dump_bytecode_max_height(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    assert(MAX_HEIGHT_SET == (enum op_max_height)MAX_WIDTH_SET);
    assert(MAX_HEIGHT_NONE == (enum op_max_height)MAX_WIDTH_NONE);

    switch (value) {
    case MAX_HEIGHT_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    case MAX_HEIGHT_NONE:
        *ptr += sprintf(*ptr, "none");
        break;
    }
}

static void dump_bytecode_min_height(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    assert(MIN_HEIGHT_SET == (enum op_min_height)MIN_WIDTH_SET);
    assert(MIN_HEIGHT_AUTO == (enum op_min_height)MIN_WIDTH_AUTO);

    switch (value) {
    case MIN_HEIGHT_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    case MIN_HEIGHT_AUTO:
        *ptr += sprintf(*ptr, "auto");
        break;
    }
}

static void dump_bytecode_aspect_ratio(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    if (value == ASPECT_RATIO_AUTO || value == ASPECT_RATIO_AUTO_RATIO)
        *ptr += sprintf(*ptr, "auto");
    if (value == ASPECT_RATIO_RATIO || value == ASPECT_RATIO_AUTO_RATIO) {
        css_fixed numerator = (css_fixed)dump_bytecode_read(cursor);
        css_fixed denominator = (css_fixed)dump_bytecode_read(cursor);
        if (value == ASPECT_RATIO_AUTO_RATIO)
            *ptr += sprintf(*ptr, " ");
        dump_number(numerator, ptr);
        *ptr += sprintf(*ptr, " / ");
        dump_number(denominator, ptr);
    }
}

static void dump_bytecode_opacity(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case OPACITY_SET: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    }
}

static void dump_bytecode_order(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case ORDER_SET: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    }
}

static void dump_bytecode_length_value(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    assert(TEXT_INDENT_SET == (enum op_text_indent)PADDING_SET);
    assert(TEXT_INDENT_SET == (enum op_text_indent)PAUSE_AFTER_SET);
    assert(TEXT_INDENT_SET == (enum op_text_indent)PAUSE_BEFORE_SET);

    switch (value) {
    case TEXT_INDENT_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    }
}

static void dump_bytecode_number(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    assert(ORPHANS_SET == (enum op_orphans)PITCH_RANGE_SET);
    assert(ORPHANS_SET == (enum op_orphans)RICHNESS_SET);
    assert(ORPHANS_SET == (enum op_orphans)STRESS_SET);
    assert(ORPHANS_SET == (enum op_orphans)WIDOWS_SET);

    switch (value) {
    case ORPHANS_SET: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    }
}

static void dump_bytecode_outline_color(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case OUTLINE_COLOR_TRANSPARENT:
        *ptr += sprintf(*ptr, "transparent");
        break;
    case OUTLINE_COLOR_CURRENT_COLOR:
        *ptr += sprintf(*ptr, "currentColor");
        break;
    case OUTLINE_COLOR_SET: {
        uint32_t colour = dump_bytecode_read(cursor);
        *ptr += sprintf(*ptr, "#%08x", colour);
    } break;
    case OUTLINE_COLOR_INVERT:
        *ptr += sprintf(*ptr, "invert");
        break;
    }
}

static void dump_bytecode_page_break_after(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    (void)cursor;
    assert(PAGE_BREAK_AFTER_AUTO == (enum op_page_break_after)PAGE_BREAK_BEFORE_AUTO);
    assert(PAGE_BREAK_AFTER_ALWAYS == (enum op_page_break_after)PAGE_BREAK_BEFORE_ALWAYS);
    assert(PAGE_BREAK_AFTER_AVOID == (enum op_page_break_after)PAGE_BREAK_BEFORE_AVOID);
    assert(PAGE_BREAK_AFTER_LEFT == (enum op_page_break_after)PAGE_BREAK_BEFORE_LEFT);
    assert(PAGE_BREAK_AFTER_RIGHT == (enum op_page_break_after)PAGE_BREAK_BEFORE_RIGHT);

    switch (value) {
    case PAGE_BREAK_AFTER_AUTO:
        *ptr += sprintf(*ptr, "auto");
        break;
    case PAGE_BREAK_AFTER_ALWAYS:
        *ptr += sprintf(*ptr, "always");
        break;
    case PAGE_BREAK_AFTER_AVOID:
        *ptr += sprintf(*ptr, "avoid");
        break;
    case PAGE_BREAK_AFTER_LEFT:
        *ptr += sprintf(*ptr, "left");
        break;
    case PAGE_BREAK_AFTER_RIGHT:
        *ptr += sprintf(*ptr, "right");
        break;
    }
}

static void dump_bytecode_pitch(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case PITCH_FREQUENCY:
        dump_bytecode_length(cursor, ptr);
        break;
    case PITCH_X_LOW:
        *ptr += sprintf(*ptr, "x-low");
        break;
    case PITCH_LOW:
        *ptr += sprintf(*ptr, "low");
        break;
    case PITCH_MEDIUM:
        *ptr += sprintf(*ptr, "medium");
        break;
    case PITCH_HIGH:
        *ptr += sprintf(*ptr, "high");
        break;
    case PITCH_X_HIGH:
        *ptr += sprintf(*ptr, "x-high");
        break;
    }
}

static void dump_bytecode_play_during(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case PLAY_DURING_URI: {
        uint32_t snum = dump_bytecode_read(cursor);
        lwc_string *he;
        css__stylesheet_string_get(cursor->sheet, snum, &he);
        *ptr += sprintf(*ptr, "'%.*s'", (int)lwc_string_length(he), lwc_string_data(he));
    } break;
    case PLAY_DURING_AUTO:
        *ptr += sprintf(*ptr, "auto");
        break;
    case PLAY_DURING_NONE:
        *ptr += sprintf(*ptr, "none");
        break;
    }

    if (value & PLAY_DURING_MIX)
        *ptr += sprintf(*ptr, " mix");
    if (value & PLAY_DURING_REPEAT)
        *ptr += sprintf(*ptr, " repeat");
}

static void dump_bytecode_quotes(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case QUOTES_STRING:
        while (value != QUOTES_NONE) {
            uint32_t snum = dump_bytecode_read(cursor);
            lwc_string *he;
            css__stylesheet_string_get(cursor->sheet, snum, &he);
            *ptr += sprintf(*ptr, " '%.*s' ", (int)lwc_string_length(he), lwc_string_data(he));

            snum = dump_bytecode_read(cursor);
            css__stylesheet_string_get(cursor->sheet, snum, &he);
            *ptr += sprintf(*ptr, " '%.*s' ", (int)lwc_string_length(he), lwc_string_data(he));

            value = dump_bytecode_read(cursor);
        }
        break;
    case QUOTES_NONE:
        *ptr += sprintf(*ptr, "none");
        break;
    }
}

static void dump_bytecode_speech_rate(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case SPEECH_RATE_SET: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    case SPEECH_RATE_X_SLOW:
        *ptr += sprintf(*ptr, "x-slow");
        break;
    case SPEECH_RATE_SLOW:
        *ptr += sprintf(*ptr, "slow");
        break;
    case SPEECH_RATE_MEDIUM:
        *ptr += sprintf(*ptr, "medium");
        break;
    case SPEECH_RATE_FAST:
        *ptr += sprintf(*ptr, "fast");
        break;
    case SPEECH_RATE_X_FAST:
        *ptr += sprintf(*ptr, "x-fast");
        break;
    case SPEECH_RATE_FASTER:
        *ptr += sprintf(*ptr, "faster");
        break;
    case SPEECH_RATE_SLOWER:
        *ptr += sprintf(*ptr, "slower");
        break;
    }
}

static void dump_bytecode_text_decoration(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    (void)cursor;
    if (value == TEXT_DECORATION_NONE)
        *ptr += sprintf(*ptr, "none");

    if (value & TEXT_DECORATION_UNDERLINE)
        *ptr += sprintf(*ptr, " underline");
    if (value & TEXT_DECORATION_OVERLINE)
        *ptr += sprintf(*ptr, " overline");
    if (value & TEXT_DECORATION_LINE_THROUGH)
        *ptr += sprintf(*ptr, " line-through");
    if (value & TEXT_DECORATION_BLINK)
        *ptr += sprintf(*ptr, " blink");
}

static void dump_bytecode_vertical_align(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case VERTICAL_ALIGN_SET:
        dump_bytecode_length(cursor, ptr);
        break;
    case VERTICAL_ALIGN_BASELINE:
        *ptr += sprintf(*ptr, "baseline");
        break;
    case VERTICAL_ALIGN_SUB:
        *ptr += sprintf(*ptr, "sub");
        break;
    case VERTICAL_ALIGN_SUPER:
        *ptr += sprintf(*ptr, "super");
        break;
    case VERTICAL_ALIGN_TOP:
        *ptr += sprintf(*ptr, "top");
        break;
    case VERTICAL_ALIGN_TEXT_TOP:
        *ptr += sprintf(*ptr, "text-top");
        break;
    case VERTICAL_ALIGN_MIDDLE:
        *ptr += sprintf(*ptr, "middle");
        break;
    case VERTICAL_ALIGN_BOTTOM:
        *ptr += sprintf(*ptr, "bottom");
        break;
    case VERTICAL_ALIGN_TEXT_BOTTOM:
        *ptr += sprintf(*ptr, "text-bottom");
        break;
    }
}

static void dump_bytecode_voice_family(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    while (value != VOICE_FAMILY_END) {
        switch (value) {
        case VOICE_FAMILY_STRING:
        case VOICE_FAMILY_IDENT_LIST: {
            uint32_t snum = dump_bytecode_read(cursor);
            lwc_string *he;
            css__stylesheet_string_get(cursor->sheet, snum, &he);
            *ptr += sprintf(*ptr, "'%.*s'", (int)lwc_string_length(he), lwc_string_data(he));
        } break;
        case VOICE_FAMILY_MALE:
            *ptr += sprintf(*ptr, "male");
            break;
        case VOICE_FAMILY_FEMALE:
            *ptr += sprintf(*ptr, "female");
            break;
        case VOICE_FAMILY_CHILD:
            *ptr += sprintf(*ptr, "child");
            break;
        }

        value = dump_bytecode_read(cursor);

        if (value != VOICE_FAMILY_END)
            *ptr += sprintf(*ptr, ", ");
    }
}

static void dump_bytecode_volume(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case VOLUME_NUMBER: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    case VOLUME_DIMENSION:
        dump_bytecode_length(cursor, ptr);
        break;
    case VOLUME_SILENT:
        *ptr += sprintf(*ptr, "silent");
        break;
    case VOLUME_X_SOFT:
        *ptr += sprintf(*ptr, "x-soft");
        break;
    case VOLUME_SOFT:
        *ptr += sprintf(*ptr, "soft");
        break;
    case VOLUME_MEDIUM:
        *ptr += sprintf(*ptr, "medium");
        break;
    case VOLUME_LOUD:
        *ptr += sprintf(*ptr, "loud");
        break;
    case VOLUME_X_LOUD:
        *ptr += sprintf(*ptr, "x-loud");
        break;
    }
}

static void dump_bytecode_z_index(struct dump_bytecode_cursor *cursor, uint32_t value, char **ptr)
{
    switch (value) {
    case Z_INDEX_SET: {
        css_fixed val = (css_fixed)dump_bytecode_read(cursor);
        dump_number(val, ptr);
    } break;
    case Z_INDEX_AUTO:
        *ptr += sprintf(*ptr, "auto");
        break;
    }
}

struct dump_bytecode_property {
    void (*read)(struct dump_bytecode_cursor *, uint32_t value, char **ptr);
    const char *const *keywords;
    size_t keyword_count;
};

#define KEYWORDS(name) {NULL, dump_keywords_##name, sizeof(dump_keywords_##name) / sizeof(dump_keywords_##name[0])}
#define READER(name) {dump_bytecode_##name, NULL, 0}

/* One dispatch entry per supported property. Shared encodings share readers. */
static const struct dump_bytecode_property dump_bytecode_properties[CSS_N_PROPERTIES] = {
    [CSS_PROP_ALIGN_CONTENT] = KEYWORDS(align_content),
    [CSS_PROP_ALIGN_ITEMS] = KEYWORDS(align_items),
    [CSS_PROP_ALIGN_SELF] = KEYWORDS(align_self),
    [CSS_PROP_AZIMUTH] = READER(azimuth),
    [CSS_PROP_BACKGROUND_ATTACHMENT] = KEYWORDS(background_attachment),
    [CSS_PROP_BORDER_TOP_COLOR] = READER(border_color),
    [CSS_PROP_BORDER_RIGHT_COLOR] = READER(border_color),
    [CSS_PROP_BORDER_BOTTOM_COLOR] = READER(border_color),
    [CSS_PROP_BORDER_LEFT_COLOR] = READER(border_color),
    [CSS_PROP_BACKGROUND_COLOR] = READER(border_color),
    [CSS_PROP_COLUMN_RULE_COLOR] = READER(border_color),
    [CSS_PROP_BACKGROUND_IMAGE] = READER(background_image),
    [CSS_PROP_CUE_AFTER] = READER(background_image),
    [CSS_PROP_CUE_BEFORE] = READER(background_image),
    [CSS_PROP_LIST_STYLE_IMAGE] = READER(background_image),
    [CSS_PROP_BACKGROUND_POSITION] = READER(background_position),
    [CSS_PROP_BACKGROUND_REPEAT] = KEYWORDS(background_repeat),
    [CSS_PROP_BORDER_COLLAPSE] = KEYWORDS(border_collapse),
    [CSS_PROP_BORDER_SPACING] = READER(border_spacing),
    [CSS_PROP_BOX_SIZING] = KEYWORDS(box_sizing),
    [CSS_PROP_BORDER_TOP_STYLE] = READER(border_style),
    [CSS_PROP_BORDER_RIGHT_STYLE] = READER(border_style),
    [CSS_PROP_BORDER_BOTTOM_STYLE] = READER(border_style),
    [CSS_PROP_BORDER_LEFT_STYLE] = READER(border_style),
    [CSS_PROP_COLUMN_RULE_STYLE] = READER(border_style),
    [CSS_PROP_OUTLINE_STYLE] = READER(border_style),
    [CSS_PROP_BORDER_TOP_WIDTH] = READER(border_width),
    [CSS_PROP_BORDER_RIGHT_WIDTH] = READER(border_width),
    [CSS_PROP_BORDER_BOTTOM_WIDTH] = READER(border_width),
    [CSS_PROP_BORDER_LEFT_WIDTH] = READER(border_width),
    [CSS_PROP_COLUMN_RULE_WIDTH] = READER(border_width),
    [CSS_PROP_OUTLINE_WIDTH] = READER(border_width),
    [CSS_PROP_MARGIN_TOP] = READER(length_auto),
    [CSS_PROP_MARGIN_RIGHT] = READER(length_auto),
    [CSS_PROP_MARGIN_BOTTOM] = READER(length_auto),
    [CSS_PROP_MARGIN_LEFT] = READER(length_auto),
    [CSS_PROP_BOTTOM] = READER(length_auto),
    [CSS_PROP_LEFT] = READER(length_auto),
    [CSS_PROP_RIGHT] = READER(length_auto),
    [CSS_PROP_TOP] = READER(length_auto),
    [CSS_PROP_HEIGHT] = READER(length_auto),
    [CSS_PROP_WIDTH] = READER(length_auto),
    [CSS_PROP_COLUMN_WIDTH] = READER(length_auto),
    [CSS_PROP_BREAK_AFTER] = READER(break_after),
    [CSS_PROP_BREAK_BEFORE] = READER(break_after),
    [CSS_PROP_BREAK_INSIDE] = KEYWORDS(break_inside),
    [CSS_PROP_CAPTION_SIDE] = KEYWORDS(caption_side),
    [CSS_PROP_CLEAR] = KEYWORDS(clear),
    [CSS_PROP_CLIP] = READER(clip),
    [CSS_PROP_COLOR] = READER(color),
    [CSS_PROP_COLUMN_COUNT] = READER(column_count),
    [CSS_PROP_COLUMN_FILL] = KEYWORDS(column_fill),
    [CSS_PROP_COLUMN_GAP] = READER(column_gap),
    [CSS_PROP_COLUMN_SPAN] = KEYWORDS(column_span),
    [CSS_PROP_CONTENT] = READER(content),
    [CSS_PROP_COUNTER_INCREMENT] = READER(counter),
    [CSS_PROP_COUNTER_RESET] = READER(counter),
    [CSS_PROP_CURSOR] = READER(cursor),
    [CSS_PROP_DIRECTION] = KEYWORDS(direction),
    [CSS_PROP_DISPLAY] = KEYWORDS(display),
    [CSS_PROP_ELEVATION] = READER(elevation),
    [CSS_PROP_EMPTY_CELLS] = KEYWORDS(empty_cells),
    [CSS_PROP_FILL_OPACITY] = READER(fill_opacity),
    [CSS_PROP_STROKE_OPACITY] = READER(stroke_opacity),
    [CSS_PROP_FLEX_BASIS] = READER(flex_basis),
    [CSS_PROP_FLEX_DIRECTION] = KEYWORDS(flex_direction),
    [CSS_PROP_FLEX_GROW] = READER(flex_grow),
    [CSS_PROP_FLEX_SHRINK] = READER(flex_shrink),
    [CSS_PROP_FLEX_WRAP] = KEYWORDS(flex_wrap),
    [CSS_PROP_FLOAT] = KEYWORDS(float),
    [CSS_PROP_FONT_FAMILY] = READER(font_family),
    [CSS_PROP_FONT_SIZE] = READER(font_size),
    [CSS_PROP_FONT_STYLE] = KEYWORDS(font_style),
    [CSS_PROP_FONT_VARIANT] = KEYWORDS(font_variant),
    [CSS_PROP_FONT_WEIGHT] = KEYWORDS(font_weight),
    [CSS_PROP_JUSTIFY_CONTENT] = KEYWORDS(justify_content),
    [CSS_PROP_LETTER_SPACING] = READER(letter_spacing),
    [CSS_PROP_WORD_SPACING] = READER(letter_spacing),
    [CSS_PROP_LINE_HEIGHT] = READER(line_height),
    [CSS_PROP_LIST_STYLE_POSITION] = KEYWORDS(list_style_position),
    [CSS_PROP_LIST_STYLE_TYPE] = KEYWORDS(list_style_type),
    [CSS_PROP_MAX_HEIGHT] = READER(max_height),
    [CSS_PROP_MAX_WIDTH] = READER(max_height),
    [CSS_PROP_MIN_HEIGHT] = READER(min_height),
    [CSS_PROP_MIN_WIDTH] = READER(min_height),
    [CSS_PROP_ASPECT_RATIO] = READER(aspect_ratio),
    [CSS_PROP_OPACITY] = READER(opacity),
    [CSS_PROP_ORDER] = READER(order),
    [CSS_PROP_PADDING_TOP] = READER(length_value),
    [CSS_PROP_PADDING_RIGHT] = READER(length_value),
    [CSS_PROP_PADDING_BOTTOM] = READER(length_value),
    [CSS_PROP_PADDING_LEFT] = READER(length_value),
    [CSS_PROP_PAUSE_AFTER] = READER(length_value),
    [CSS_PROP_PAUSE_BEFORE] = READER(length_value),
    [CSS_PROP_TEXT_INDENT] = READER(length_value),
    [CSS_PROP_ORPHANS] = READER(number),
    [CSS_PROP_PITCH_RANGE] = READER(number),
    [CSS_PROP_RICHNESS] = READER(number),
    [CSS_PROP_STRESS] = READER(number),
    [CSS_PROP_WIDOWS] = READER(number),
    [CSS_PROP_OUTLINE_COLOR] = READER(outline_color),
    [CSS_PROP_OVERFLOW_X] = KEYWORDS(overflow_x),
    [CSS_PROP_OVERFLOW_Y] = KEYWORDS(overflow_x),
    [CSS_PROP_PAGE_BREAK_AFTER] = READER(page_break_after),
    [CSS_PROP_PAGE_BREAK_BEFORE] = READER(page_break_after),
    [CSS_PROP_PAGE_BREAK_INSIDE] = KEYWORDS(page_break_inside),
    [CSS_PROP_PITCH] = READER(pitch),
    [CSS_PROP_PLAY_DURING] = READER(play_during),
    [CSS_PROP_POSITION] = KEYWORDS(position),
    [CSS_PROP_QUOTES] = READER(quotes),
    [CSS_PROP_SPEAK_HEADER] = KEYWORDS(speak_header),
    [CSS_PROP_SPEAK_NUMERAL] = KEYWORDS(speak_numeral),
    [CSS_PROP_SPEAK_PUNCTUATION] = KEYWORDS(speak_punctuation),
    [CSS_PROP_SPEAK] = KEYWORDS(speak),
    [CSS_PROP_SPEECH_RATE] = READER(speech_rate),
    [CSS_PROP_TABLE_LAYOUT] = KEYWORDS(table_layout),
    [CSS_PROP_TEXT_ALIGN] = KEYWORDS(text_align),
    [CSS_PROP_TEXT_DECORATION] = READER(text_decoration),
    [CSS_PROP_TEXT_TRANSFORM] = KEYWORDS(text_transform),
    [CSS_PROP_UNICODE_BIDI] = KEYWORDS(unicode_bidi),
    [CSS_PROP_VERTICAL_ALIGN] = READER(vertical_align),
    [CSS_PROP_VISIBILITY] = KEYWORDS(visibility),
    [CSS_PROP_CONTENT_VISIBILITY] = KEYWORDS(content_visibility),
    [CSS_PROP_VOICE_FAMILY] = READER(voice_family),
    [CSS_PROP_VOLUME] = READER(volume),
    [CSS_PROP_WHITE_SPACE] = KEYWORDS(white_space),
    [CSS_PROP_WRITING_MODE] = KEYWORDS(writing_mode),
    [CSS_PROP_Z_INDEX] = READER(z_index),
};
#undef READER
#undef KEYWORDS

static void dump_bytecode_calc(struct dump_bytecode_cursor *cursor, char **ptr)
{
    uint32_t unit = dump_bytecode_read(cursor);
    uint32_t snum = dump_bytecode_read(cursor);
    lwc_string *expression;
    css__stylesheet_string_get(cursor->sheet, snum, &expression);
    const css_code_t *code = (const css_code_t *)(const void *)lwc_string_data(expression);
    struct dump_bytecode_cursor calc = {code, code + lwc_string_length(expression) / sizeof(*code), cursor->sheet};

    *ptr += sprintf(*ptr, "/* -> ");
    dump_unit(0, unit, ptr);
    *ptr += sprintf(*ptr, " */ calc(");
    for (;;) {
        uint32_t op = dump_bytecode_read(&calc);
        switch (op) {
        case CALC_FINISH:
            *ptr += sprintf(*ptr, "=)");
            return;
        case CALC_ADD:
            *ptr += sprintf(*ptr, "+ ");
            break;
        case CALC_SUBTRACT:
            *ptr += sprintf(*ptr, "- ");
            break;
        case CALC_MULTIPLY:
            *ptr += sprintf(*ptr, "* ");
            break;
        case CALC_DIVIDE:
            *ptr += sprintf(*ptr, "/ ");
            break;
        case CALC_PUSH_VALUE:
            dump_bytecode_length(&calc, ptr);
            *ptr += sprintf(*ptr, " ");
            break;
        case CALC_PUSH_NUMBER:
            dump_number((css_fixed)dump_bytecode_read(&calc), ptr);
            *ptr += sprintf(*ptr, " ");
            break;
        default:
            *ptr += sprintf(*ptr, "??%d ", op);
            break;
        }
    }
}

static bool dump_bytecode_value(struct dump_bytecode_cursor *cursor, uint32_t opv, char **ptr)
{
    switch (getFlagValue(opv)) {
    case FLAG_VALUE_INHERIT:
        *ptr += sprintf(*ptr, "inherit");
        return true;
    case FLAG_VALUE_INITIAL:
        *ptr += sprintf(*ptr, "initial");
        return true;
    case FLAG_VALUE_REVERT:
        *ptr += sprintf(*ptr, "revert");
        return true;
    case FLAG_VALUE_UNSET:
        *ptr += sprintf(*ptr, "unset");
        return true;
    default:
        break;
    }
    if (isCalc(opv)) {
        dump_bytecode_calc(cursor, ptr);
        return true;
    }
    opcode_t op = getOpcode(opv);
    uint32_t value = getValue(opv);
    const struct dump_bytecode_property *property = op < CSS_N_PROPERTIES ? &dump_bytecode_properties[op] : NULL;
    if (property != NULL && property->read != NULL) {
        property->read(cursor, value, ptr);
        return true;
    }
    if (property != NULL && property->keywords != NULL) {
        if (value < property->keyword_count && property->keywords[value] != NULL)
            *ptr += sprintf(*ptr, "%s", property->keywords[value]);
        return true;
    }
    *ptr += sprintf(*ptr, "Unknown opcode %x", op);
    return false;
}

static void dump_bytecode(css_style *style, char **ptr, uint32_t depth)
{
    if (style->used == 0)
        return;
    struct dump_bytecode_cursor cursor = {style->bytecode, style->bytecode + style->used, style->sheet};
    while (cursor.next < cursor.end) {
        uint32_t opv = dump_bytecode_read(&cursor);
        opcode_t op = getOpcode(opv);
        *(*ptr)++ = '|';
        for (uint32_t i = 0; i < depth; i++)
            *(*ptr)++ = ' ';
        const char *name = op < sizeof(opcode_names) / sizeof(opcode_names[0]) ? opcode_names[op] : NULL;
        *ptr += sprintf(*ptr, "%s: ", name != NULL ? name : "unknown");

        if (css__is_background_list(opv)) {
            dump_bytecode_read(&cursor); /* payload word count */
            uint32_t count = dump_bytecode_read(&cursor);
            *ptr += sprintf(*ptr, "/* %u layers%s */\n", count, isImportant(opv) ? " !important" : "");
            /* Inner OPVs use the same scalar readers on subsequent iterations. */
            continue;
        }
        if (!dump_bytecode_value(&cursor, opv, ptr))
            return;
        if (isImportant(opv))
            *ptr += sprintf(*ptr, " !important");
        *ptr += sprintf(*ptr, "\n");
    }
}

#endif
