/* Shared traversal of trusted parser bytecode. Licensed under the MIT License. */
#include "stylesheet.h"
#include "bytecode/opcodes.h"

void css__stylesheet_style_skip(css_style *style, css_code_t opv)
{
    opcode_t op = getOpcode(opv);
    uint32_t value = getValue(opv);

    uint32_t counted = css__counted_payload_words(opv, style->bytecode);
    if (counted != 0) {
        advance_bytecode(style, counted * sizeof(css_code_t));
    } else if (css__is_background_list(opv)) {
        advance_bytecode(style, (1 + *style->bytecode) * sizeof(css_code_t));
    } else if (css__grid_line_has_integer(opv)) {
        advance_bytecode(style, sizeof(css_code_t));
    } else if (op == CSS_PROP_VAR_DEFERRED || op == CSS_PROP_CUSTOM_PROPERTY) {
        advance_bytecode(style, 2 * sizeof(css_code_t));
    } else if (hasFlagValue(opv) == false && value == VALUE_IS_CALC) {
        advance_bytecode(style, 2 * sizeof(css_code_t));
    } else if (hasFlagValue(opv) == false) {
        switch (op) {
        case CSS_PROP_AZIMUTH:
            if ((value & ~AZIMUTH_BEHIND) == AZIMUTH_ANGLE)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_BORDER_TOP_COLOR:
        case CSS_PROP_BORDER_RIGHT_COLOR:
        case CSS_PROP_BORDER_BOTTOM_COLOR:
        case CSS_PROP_BORDER_LEFT_COLOR:
        case CSS_PROP_BACKGROUND_COLOR:
        case CSS_PROP_COLUMN_RULE_COLOR:
        case CSS_PROP_FILL:
        case CSS_PROP_STROKE:
            if (value == BACKGROUND_COLOR_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_BACKGROUND_IMAGE:
        case CSS_PROP_CUE_AFTER:
        case CSS_PROP_CUE_BEFORE:
        case CSS_PROP_LIST_STYLE_IMAGE:
            if (value == BACKGROUND_IMAGE_URI)
                advance_bytecode(style, sizeof(css_code_t));
            else if (op == CSS_PROP_BACKGROUND_IMAGE &&
                (value == BACKGROUND_IMAGE_LINEAR_GRADIENT || value == BACKGROUND_IMAGE_RADIAL_GRADIENT))
                advance_bytecode(style, (2 + 3 * style->bytecode[1]) * sizeof(css_code_t));
            break;
        case CSS_PROP_OBJECT_POSITION:
            if ((value & 0xf0) == BACKGROUND_POSITION_HORZ_SET)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            if ((value & 0x0f) == BACKGROUND_POSITION_VERT_SET)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_BACKGROUND_POSITION_X:
        case CSS_PROP_BACKGROUND_POSITION_Y:
            if (value == BACKGROUND_POSITION_AXIS_SET)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_BORDER_SPACING:
            if (value == BORDER_SPACING_SET)
                advance_bytecode(style, 4 * sizeof(css_code_t));
            break;
        case CSS_PROP_BORDER_TOP_WIDTH:
        case CSS_PROP_BORDER_RIGHT_WIDTH:
        case CSS_PROP_BORDER_BOTTOM_WIDTH:
        case CSS_PROP_BORDER_LEFT_WIDTH:
        case CSS_PROP_OUTLINE_WIDTH:
        case CSS_PROP_COLUMN_RULE_WIDTH:
            if (value == BORDER_WIDTH_SET)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_MARGIN_TOP:
        case CSS_PROP_MARGIN_RIGHT:
        case CSS_PROP_MARGIN_BOTTOM:
        case CSS_PROP_MARGIN_LEFT:
        case CSS_PROP_BOTTOM:
        case CSS_PROP_LEFT:
        case CSS_PROP_RIGHT:
        case CSS_PROP_TOP:
        case CSS_PROP_HEIGHT:
        case CSS_PROP_WIDTH:
        case CSS_PROP_COLUMN_WIDTH:
        case CSS_PROP_COLUMN_GAP:
        case CSS_PROP_ROW_GAP:
            if (value == BOTTOM_SET)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_CLIP:
            if ((value & CLIP_SHAPE_MASK) == CLIP_SHAPE_RECT) {
                if ((value & CLIP_RECT_TOP_AUTO) == 0)
                    advance_bytecode(style, 2 * sizeof(css_code_t));
                if ((value & CLIP_RECT_RIGHT_AUTO) == 0)
                    advance_bytecode(style, 2 * sizeof(css_code_t));
                if ((value & CLIP_RECT_BOTTOM_AUTO) == 0)
                    advance_bytecode(style, 2 * sizeof(css_code_t));
                if ((value & CLIP_RECT_LEFT_AUTO) == 0)
                    advance_bytecode(style, 2 * sizeof(css_code_t));
            }
            break;
        case CSS_PROP_COLOR:
            if (value == COLOR_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_COLUMN_COUNT:
            if (value == COLUMN_COUNT_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_CONTENT:
            while (value != CONTENT_NORMAL && value != CONTENT_NONE) {
                switch (value & 0xff) {
                case CONTENT_COUNTER:
                case CONTENT_URI:
                case CONTENT_ATTR:
                case CONTENT_STRING:
                    advance_bytecode(style, sizeof(css_code_t));
                    break;
                case CONTENT_COUNTERS:
                    advance_bytecode(style, 2 * sizeof(css_code_t));
                    break;
                case CONTENT_OPEN_QUOTE:
                case CONTENT_CLOSE_QUOTE:
                case CONTENT_NO_OPEN_QUOTE:
                case CONTENT_NO_CLOSE_QUOTE:
                    break;
                }
                value = *style->bytecode;
                advance_bytecode(style, sizeof(css_code_t));
            }
            break;
        case CSS_PROP_COUNTER_INCREMENT:
        case CSS_PROP_COUNTER_RESET:
            while (value != COUNTER_INCREMENT_NONE) {
                advance_bytecode(style, 2 * sizeof(css_code_t));
                value = *style->bytecode;
                advance_bytecode(style, sizeof(css_code_t));
            }
            break;
        case CSS_PROP_CURSOR:
            while (value == CURSOR_URI) {
                advance_bytecode(style, sizeof(css_code_t));
                value = *style->bytecode;
                advance_bytecode(style, sizeof(css_code_t));
            }
            break;
        case CSS_PROP_ELEVATION:
            if (value == ELEVATION_ANGLE)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_FLEX_BASIS:
            if (value == FLEX_BASIS_SET)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_FLEX_GROW:
            if (value == FLEX_GROW_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_FLEX_SHRINK:
            if (value == FLEX_SHRINK_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_FONT_FAMILY:
            while (value != FONT_FAMILY_END) {
                if (value == FONT_FAMILY_STRING || value == FONT_FAMILY_IDENT_LIST)
                    advance_bytecode(style, sizeof(css_code_t));
                value = *style->bytecode;
                advance_bytecode(style, sizeof(css_code_t));
            }
            break;
        case CSS_PROP_FONT_SIZE:
            if (value == FONT_SIZE_DIMENSION)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_LETTER_SPACING:
        case CSS_PROP_WORD_SPACING:
            if (value == LETTER_SPACING_SET)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_LINE_HEIGHT:
            if (value == LINE_HEIGHT_NUMBER)
                advance_bytecode(style, sizeof(css_code_t));
            else if (value == LINE_HEIGHT_DIMENSION)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_MAX_HEIGHT:
        case CSS_PROP_MAX_WIDTH:
            if (value == MAX_HEIGHT_SET)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_PADDING_TOP:
        case CSS_PROP_PADDING_RIGHT:
        case CSS_PROP_PADDING_BOTTOM:
        case CSS_PROP_PADDING_LEFT:
        case CSS_PROP_MIN_HEIGHT:
        case CSS_PROP_MIN_WIDTH:
        case CSS_PROP_PAUSE_AFTER:
        case CSS_PROP_PAUSE_BEFORE:
        case CSS_PROP_TEXT_INDENT:
        case CSS_PROP_STROKE_WIDTH:
            if (value == MIN_HEIGHT_SET)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_ASPECT_RATIO:
            if (value == ASPECT_RATIO_RATIO || value == ASPECT_RATIO_AUTO_RATIO)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_OPACITY:
            if (value == OPACITY_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_FILL_OPACITY:
            if (value == FILL_OPACITY_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_STROKE_OPACITY:
            if (value == STROKE_OPACITY_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_ORDER:
            if (value == ORDER_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_ORPHANS:
        case CSS_PROP_PITCH_RANGE:
        case CSS_PROP_RICHNESS:
        case CSS_PROP_STRESS:
        case CSS_PROP_WIDOWS:
            if (value == ORPHANS_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_OUTLINE_COLOR:
            if (value == OUTLINE_COLOR_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_PITCH:
            if (value == PITCH_FREQUENCY)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_PLAY_DURING:
            if (value & PLAY_DURING_URI)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_QUOTES:
            while (value != QUOTES_NONE) {
                advance_bytecode(style, 2 * sizeof(css_code_t));
                value = *style->bytecode;
                advance_bytecode(style, sizeof(css_code_t));
            }
            break;
        case CSS_PROP_SPEECH_RATE:
            if (value == SPEECH_RATE_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        case CSS_PROP_VERTICAL_ALIGN:
            if (value == VERTICAL_ALIGN_SET)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_VOICE_FAMILY:
            while (value != VOICE_FAMILY_END) {
                if (value == VOICE_FAMILY_STRING || value == VOICE_FAMILY_IDENT_LIST)
                    advance_bytecode(style, sizeof(css_code_t));
                value = *style->bytecode;
                advance_bytecode(style, sizeof(css_code_t));
            }
            break;
        case CSS_PROP_VOLUME:
            if (value == VOLUME_NUMBER)
                advance_bytecode(style, sizeof(css_code_t));
            else if (value == VOLUME_DIMENSION)
                advance_bytecode(style, 2 * sizeof(css_code_t));
            break;
        case CSS_PROP_Z_INDEX:
            if (value == Z_INDEX_SET)
                advance_bytecode(style, sizeof(css_code_t));
            break;
        default:
            break;
        }
    }
}
