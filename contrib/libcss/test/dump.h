#ifndef test_dump_h_
#define test_dump_h_

#include <string.h>

#include <libcss/types.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/font_face.h"
#include "stylesheet.h"

#include "testutils.h"
#include "dump_output.h"

static void dump_rule_selector(css_rule_selector *s, struct dump_output *out, uint32_t depth);
static void dump_selector_list(css_selector *list, struct dump_output *out);
static void dump_selector(css_selector *selector, struct dump_output *out);
static void dump_selector_detail(css_selector_detail *detail, struct dump_output *out);
static void dump_bytecode(css_style *style, struct dump_output *out, uint32_t depth);
static void dump_string(lwc_string *string, struct dump_output *out);
static void dump_font_face(css_font_face *font_face, struct dump_output *out);

static void dump_rule_selector(css_rule_selector *s, struct dump_output *out, uint32_t depth)
{
    dump_char(out, '|');
    for (uint32_t i = 0; i < depth; i++)
        dump_char(out, ' ');
    for (uint32_t i = 0; i < s->base.items; i++) {
        dump_selector_list(s->selectors[i], out);
        if (i != (uint32_t)(s->base.items - 1))
            dump_write(out, ", ", 2);
    }
    dump_char(out, '\n');
    if (s->style != NULL)
        dump_bytecode(s->style, out, depth + 1);
}

/* The output owns its storage and records failure. No size guess from fixture
 * expectations or stylesheet input is a bound on the generated diagnostics. */
static void dump_sheet(css_stylesheet *sheet, struct dump_output *out)
{
    for (css_rule *rule = sheet->rule_list; rule != NULL && !out->failed; rule = rule->next) {
        switch (rule->type) {
        case CSS_RULE_SELECTOR:
            dump_rule_selector((css_rule_selector *)rule, out, 1);
            break;
        case CSS_RULE_CHARSET: {
            css_rule_charset *s = (css_rule_charset *)rule;
            dump_printf(out, "| @charset(");
            dump_string(s->encoding, out);
            dump_printf(out, ")\n");
            break;
        }
        case CSS_RULE_IMPORT: {
            css_rule_import *s = (css_rule_import *)rule;
            dump_printf(out, "| @import url(\"%.*s\")\n", (int)lwc_string_length(s->url), lwc_string_data(s->url));
            /* TODO: media list */
            break;
        }
        case CSS_RULE_MEDIA: {
            css_rule_media *s = (css_rule_media *)rule;
            dump_printf(out, "| @media %s%03" PRIx64 "\n", s->media->negate_type ? "not " : "", s->media->type);
            /* TODO: media list and nested non-selector rules */
            for (css_rule *child = s->first_child; child != NULL && !out->failed; child = child->next)
                dump_rule_selector((css_rule_selector *)child, out, 2);
            break;
        }
        case CSS_RULE_PAGE: {
            css_rule_page *s = (css_rule_page *)rule;
            dump_printf(out, "| @page ");
            if (s->selector != NULL)
                dump_selector_list(s->selector, out);
            dump_char(out, '\n');
            if (s->style != NULL)
                dump_bytecode(s->style, out, 2);
            break;
        }
        case CSS_RULE_FONT_FACE: {
            css_rule_font_face *s = (css_rule_font_face *)rule;
            dump_printf(out, "| @font-face");
            if (s->font_face != NULL)
                dump_font_face(s->font_face, out);
            dump_char(out, '\n');
            break;
        }
        default:
            dump_printf(out, "Unhandled rule type %d\n", rule->type);
            break;
        }
    }
}

void dump_selector_list(css_selector *list, struct dump_output *out)
{
    if (list->combinator != NULL) {
        dump_selector_list(list->combinator, out);
    }

    switch (list->data.comb) {
    case CSS_COMBINATOR_NONE:
        break;
    case CSS_COMBINATOR_ANCESTOR:
        dump_char(out, ' ');
        break;
    case CSS_COMBINATOR_PARENT:
        dump_write(out, " > ", 3);
        break;
    case CSS_COMBINATOR_SIBLING:
        dump_write(out, " + ", 3);
        break;
    case CSS_COMBINATOR_GENERIC_SIBLING:
        dump_write(out, " + ", 3);
        break;
    }

    dump_selector(list, out);
}


void dump_selector(css_selector *selector, struct dump_output *out)
{
    css_selector_detail *d = &selector->data;

    while (true) {
        dump_selector_detail(d, out);

        if (d->next == 0)
            break;

        d++;
    }
}

void dump_selector_detail(css_selector_detail *detail, struct dump_output *out)
{
    if (detail->negate)
        dump_printf(out, ":not(");

    switch (detail->type) {
    case CSS_SELECTOR_ELEMENT:
        if (lwc_string_length(detail->qname.name) == 1 && lwc_string_data(detail->qname.name)[0] == '*' &&
            detail->next == 0) {
            dump_string(detail->qname.name, out);
        } else if (lwc_string_length(detail->qname.name) != 1 || lwc_string_data(detail->qname.name)[0] != '*') {
            dump_string(detail->qname.name, out);
        }
        break;
    case CSS_SELECTOR_CLASS:
        dump_char(out, '.');
        dump_string(detail->qname.name, out);
        break;
    case CSS_SELECTOR_ID:
        dump_char(out, '#');
        dump_string(detail->qname.name, out);
        break;
    case CSS_SELECTOR_PSEUDO_CLASS:
    case CSS_SELECTOR_PSEUDO_ELEMENT:
        dump_char(out, ':');
        dump_string(detail->qname.name, out);
        if (detail->value_type == CSS_SELECTOR_DETAIL_VALUE_STRING) {
            if (detail->value.string != NULL) {
                dump_char(out, '(');
                dump_string(detail->value.string, out);
                dump_char(out, ')');
            }
        } else {
            dump_printf(out, "(%dn+%d)", detail->value.nth.a, detail->value.nth.b);
        }
        break;
    case CSS_SELECTOR_ATTRIBUTE:
        dump_char(out, '[');
        dump_string(detail->qname.name, out);
        dump_char(out, ']');
        break;
    case CSS_SELECTOR_ATTRIBUTE_EQUAL:
        dump_char(out, '[');
        dump_string(detail->qname.name, out);
        dump_char(out, '=');
        dump_char(out, '"');
        dump_string(detail->value.string, out);
        dump_char(out, '"');
        dump_char(out, ']');
        break;
    case CSS_SELECTOR_ATTRIBUTE_DASHMATCH:
        dump_char(out, '[');
        dump_string(detail->qname.name, out);
        dump_char(out, '|');
        dump_char(out, '=');
        dump_char(out, '"');
        dump_string(detail->value.string, out);
        dump_char(out, '"');
        dump_char(out, ']');
        break;
    case CSS_SELECTOR_ATTRIBUTE_INCLUDES:
        dump_char(out, '[');
        dump_string(detail->qname.name, out);
        dump_char(out, '~');
        dump_char(out, '=');
        dump_char(out, '"');
        dump_string(detail->value.string, out);
        dump_char(out, '"');
        dump_char(out, ']');
        break;
    case CSS_SELECTOR_ATTRIBUTE_PREFIX:
        dump_char(out, '[');
        dump_string(detail->qname.name, out);
        dump_char(out, '^');
        dump_char(out, '=');
        dump_char(out, '"');
        dump_string(detail->value.string, out);
        dump_char(out, '"');
        dump_char(out, ']');
        break;
    case CSS_SELECTOR_ATTRIBUTE_SUFFIX:
        dump_char(out, '[');
        dump_string(detail->qname.name, out);
        dump_char(out, '$');
        dump_char(out, '=');
        dump_char(out, '"');
        dump_string(detail->value.string, out);
        dump_char(out, '"');
        dump_char(out, ']');
        break;
    case CSS_SELECTOR_ATTRIBUTE_SUBSTRING:
        dump_char(out, '[');
        dump_string(detail->qname.name, out);
        dump_char(out, '*');
        dump_char(out, '=');
        dump_char(out, '"');
        dump_string(detail->value.string, out);
        dump_char(out, '"');
        dump_char(out, ']');
        break;
    }

    if (detail->negate)
        dump_printf(out, ")");
}

/**
 * Opcode names, indexed by opcode
 */
static const char *opcode_names[] = {
    "azimuth",
    "background-attachment",
    "background-color",
    "background-image",
    "background-position",
    "background-repeat-x",
    "border-collapse",
    "border-spacing",
    "border-top-color",
    "border-right-color",
    "border-bottom-color",
    "border-left-color",
    "border-top-style",
    "border-right-style",
    "border-bottom-style",
    "border-left-style",
    "border-top-width",
    "border-right-width",
    "border-bottom-width",
    "border-left-width",
    "bottom",
    "caption-side",
    "clear",
    "clip",
    "color",
    "content",
    "counter-increment",
    "counter-reset",
    "cue-after",
    "cue-before",
    "cursor",
    "direction",
    "display",
    "elevation",
    "empty-cells",
    "float",
    "font-family",
    "font-size",
    "font-style",
    "font-variant",
    "font-weight",
    "height",
    "left",
    "letter-spacing",
    "line-height",
    "list-style-image",
    "list-style-position",
    "list-style-type",
    "margin-top",
    "margin-right",
    "margin-bottom",
    "margin-left",
    "max-height",
    "max-width",
    "min-height",
    "min-width",
    "orphans",
    "outline-color",
    "outline-style",
    "outline-width",
    "overflow-x",
    "padding-top",
    "padding-right",
    "padding-bottom",
    "padding-left",
    "page-break-after",
    "page-break-before",
    "page-break-inside",
    "pause-after",
    "pause-before",
    "pitch-range",
    "pitch",
    "play-during",
    "position",
    "quotes",
    "richness",
    "right",
    "speak-header",
    "speak-numeral",
    "speak-punctuation",
    "speak",
    "speech-rate",
    "stress",
    "table-layout",
    "text-align",
    "text-decoration",
    "text-indent",
    "text-transform",
    "top",
    "unicode-bidi",
    "vertical-align",
    "visibility",
    "voice-family",
    "volume",
    "white-space",
    "widows",
    "width",
    "word-spacing",
    "z-index",
    "opacity",
    "break-after",
    "break-before",
    "break-inside",
    "column-count",
    "column-fill",
    "column-gap",
    "column-rule-color",
    "column-rule-style",
    "column-rule-width",
    "column-span",
    "column-width",
    "writing-mode",
    "overflow-y",
    "box-sizing",
    "align-content",
    "align-items",
    "align-self",
    "flex-basis",
    "flex-direction",
    "flex-grow",
    "flex-shrink",
    "flex-wrap",
    "justify-content",
    "order",
    "fill",
    "fill-opacity",
    "stroke-opacity",
    "stroke",
    "stroke-width",
    "grid-template-columns",
    "grid-template-rows",
    "grid-auto-flow",
    "row-gap",
    "grid-column-start",
    "grid-column-end",
    "grid-row-start",
    "grid-row-end",
    "object-fit",
    "object-position",
    "transform",
    [CSS_PROP_ASPECT_RATIO] = "aspect-ratio",
    [CSS_PROP_CONTENT_VISIBILITY] = "content-visibility",
    [CSS_PROP_BACKGROUND_REPEAT_Y] = "background-repeat-y",
};

static void dump_css_fixed(css_fixed f, struct dump_output *out)
{
#define ABS(x) ((x) < 0 ? 0u - (uint32_t)(x) : (uint32_t)(x))
    uint32_t uintpart = FIXTOINT(ABS(f));
    /* + 500 to ensure round to nearest (division will truncate) */
    uint32_t fracpart = ((ABS(f) & 0x3ff) * 1000 + 500) / (1 << 10);
#undef ABS
    size_t flen = 0;
    char tmp[20];
    size_t tlen = 0;
    char buffer[32];
    char *buf = buffer;

    if (f < 0) {
        buf[0] = '-';
        buf++;
    }

    do {
        tmp[tlen] = "0123456789"[uintpart % 10];
        tlen++;

        uintpart /= 10;
    } while (tlen < 20 && uintpart != 0);

    while (tlen > 0) {
        buf[0] = tmp[--tlen];
        buf++;
    }

    buf[0] = '.';
    buf++;

    do {
        tmp[tlen] = "0123456789"[fracpart % 10];
        tlen++;

        fracpart /= 10;
    } while (tlen < 20 && fracpart != 0);

    while (tlen > 0) {
        buf[0] = tmp[--tlen];
        buf++;
        flen++;
    }

    while (flen < 3) {
        buf[0] = '0';
        buf++;
        flen++;
    }

    buf[0] = '\0';

    dump_write(out, buffer, (size_t)(buf - buffer));
}

static void dump_number(css_fixed val, struct dump_output *out)
{
    if (INTTOFIX(FIXTOINT(val)) == val)
        dump_printf(out, "%d", FIXTOINT(val));
    else
        dump_css_fixed(val, out);
}

static void dump_unit_suffix(uint32_t unit, struct dump_output *out)
{
    switch (unit) {
    case UNIT_PX:
        dump_printf(out, "px");
        break;
    case UNIT_EX:
        dump_printf(out, "ex");
        break;
    case UNIT_EM:
        dump_printf(out, "em");
        break;
    case UNIT_IN:
        dump_printf(out, "in");
        break;
    case UNIT_CM:
        dump_printf(out, "cm");
        break;
    case UNIT_MM:
        dump_printf(out, "mm");
        break;
    case UNIT_PT:
        dump_printf(out, "pt");
        break;
    case UNIT_PC:
        dump_printf(out, "pc");
        break;
    case UNIT_CH:
        dump_printf(out, "ch");
        break;
    case UNIT_REM:
        dump_printf(out, "rem");
        break;
    case UNIT_LH:
        dump_printf(out, "lh");
        break;
    case UNIT_VH:
        dump_printf(out, "vh");
        break;
    case UNIT_VW:
        dump_printf(out, "vw");
        break;
    case UNIT_VI:
        dump_printf(out, "vi");
        break;
    case UNIT_VB:
        dump_printf(out, "vb");
        break;
    case UNIT_VMIN:
        dump_printf(out, "vmin");
        break;
    case UNIT_VMAX:
        dump_printf(out, "vmax");
        break;
    case UNIT_Q:
        dump_printf(out, "q");
        break;
    case UNIT_PCT:
        dump_printf(out, "%%");
        break;
    case UNIT_DEG:
        dump_printf(out, "deg");
        break;
    case UNIT_GRAD:
        dump_printf(out, "grad");
        break;
    case UNIT_RAD:
        dump_printf(out, "rad");
        break;
    case UNIT_MS:
        dump_printf(out, "ms");
        break;
    case UNIT_S:
        dump_printf(out, "s");
        break;
    case UNIT_HZ:
        dump_printf(out, "Hz");
        break;
    case UNIT_KHZ:
        dump_printf(out, "kHz");
        break;
    case UNIT_CALC_ANY:
        dump_printf(out, "any");
        break;
    case UNIT_CALC_NUMBER:
        dump_printf(out, "number");
        break;
    }
}

static void dump_unit(css_fixed val, uint32_t unit, struct dump_output *out)
{
    dump_number(val, out);
    dump_unit_suffix(unit, out);
}

static void dump_counter(lwc_string *name, uint32_t value, struct dump_output *out)
{
    dump_printf(out, "counter(%.*s", (int)lwc_string_length(name), lwc_string_data(name));

    value >>= CONTENT_COUNTER_STYLE_SHIFT;

    switch (value) {
    case LIST_STYLE_TYPE_DISCLOSURE_OPEN:
        dump_printf(out, ", disclosure-open");
        break;
    case LIST_STYLE_TYPE_DISCLOSURE_CLOSED:
        dump_printf(out, ", disclosure-closed");
        break;
    case LIST_STYLE_TYPE_DISC:
        dump_printf(out, ", disc");
        break;
    case LIST_STYLE_TYPE_CIRCLE:
        dump_printf(out, ", circle");
        break;
    case LIST_STYLE_TYPE_SQUARE:
        dump_printf(out, ", square");
        break;
    case LIST_STYLE_TYPE_DECIMAL:
        break;
    case LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO:
        dump_printf(out, ", decimal-leading-zero");
        break;
    case LIST_STYLE_TYPE_LOWER_ROMAN:
        dump_printf(out, ", lower-roman");
        break;
    case LIST_STYLE_TYPE_UPPER_ROMAN:
        dump_printf(out, ", upper-roman");
        break;
    case LIST_STYLE_TYPE_LOWER_GREEK:
        dump_printf(out, ", lower-greek");
        break;
    case LIST_STYLE_TYPE_LOWER_LATIN:
        dump_printf(out, ", lower-latin");
        break;
    case LIST_STYLE_TYPE_UPPER_LATIN:
        dump_printf(out, ", upper-latin");
        break;
    case LIST_STYLE_TYPE_ARMENIAN:
        dump_printf(out, ", armenian");
        break;
    case LIST_STYLE_TYPE_GEORGIAN:
        dump_printf(out, ", georgian");
        break;
    case LIST_STYLE_TYPE_LOWER_ALPHA:
        dump_printf(out, ", lower-alpha");
        break;
    case LIST_STYLE_TYPE_UPPER_ALPHA:
        dump_printf(out, ", upper-alpha");
        break;
    case LIST_STYLE_TYPE_NONE:
        dump_printf(out, ", none");
        break;
    }

    dump_printf(out, ")");
}

static void dump_counters(lwc_string *name, lwc_string *separator, uint32_t value, struct dump_output *out)
{
    dump_printf(out, "counters(%.*s, %.*s", (int)lwc_string_length(name), lwc_string_data(name),
        (int)lwc_string_length(separator), lwc_string_data(separator));

    value >>= CONTENT_COUNTERS_STYLE_SHIFT;

    switch (value) {
    case LIST_STYLE_TYPE_DISCLOSURE_OPEN:
        dump_printf(out, ", disclosure-open");
        break;
    case LIST_STYLE_TYPE_DISCLOSURE_CLOSED:
        dump_printf(out, ", disclosure-closed");
        break;
    case LIST_STYLE_TYPE_DISC:
        dump_printf(out, ", disc");
        break;
    case LIST_STYLE_TYPE_CIRCLE:
        dump_printf(out, ", circle");
        break;
    case LIST_STYLE_TYPE_SQUARE:
        dump_printf(out, ", square");
        break;
    case LIST_STYLE_TYPE_DECIMAL:
        break;
    case LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO:
        dump_printf(out, ", decimal-leading-zero");
        break;
    case LIST_STYLE_TYPE_LOWER_ROMAN:
        dump_printf(out, ", lower-roman");
        break;
    case LIST_STYLE_TYPE_UPPER_ROMAN:
        dump_printf(out, ", upper-roman");
        break;
    case LIST_STYLE_TYPE_LOWER_GREEK:
        dump_printf(out, ", lower-greek");
        break;
    case LIST_STYLE_TYPE_LOWER_LATIN:
        dump_printf(out, ", lower-latin");
        break;
    case LIST_STYLE_TYPE_UPPER_LATIN:
        dump_printf(out, ", upper-latin");
        break;
    case LIST_STYLE_TYPE_ARMENIAN:
        dump_printf(out, ", armenian");
        break;
    case LIST_STYLE_TYPE_GEORGIAN:
        dump_printf(out, ", georgian");
        break;
    case LIST_STYLE_TYPE_LOWER_ALPHA:
        dump_printf(out, ", lower-alpha");
        break;
    case LIST_STYLE_TYPE_UPPER_ALPHA:
        dump_printf(out, ", upper-alpha");
        break;
    case LIST_STYLE_TYPE_NONE:
        dump_printf(out, ", none");
        break;
    }

    dump_printf(out, ")");
}

#include "dump_bytecode.h"

void dump_string(lwc_string *string, struct dump_output *out)
{
    dump_printf(out, "%.*s", (int)lwc_string_length(string), lwc_string_data(string));
}

void dump_font_face(css_font_face *font_face, struct dump_output *out)
{
    uint8_t style, weight;

    if (font_face->font_family != NULL) {
        dump_char(out, '\n');
        dump_printf(out, "|  font-family: %.*s", (int)lwc_string_length(font_face->font_family),
            lwc_string_data(font_face->font_family));
    }

    dump_printf(out, "\n|  font-style: ");
    style = css_font_face_font_style(font_face);
    switch (style) {
    case CSS_FONT_STYLE_INHERIT:
        dump_printf(out, "unspecified");
        break;
    case CSS_FONT_STYLE_NORMAL:
        dump_printf(out, "normal");
        break;
    case CSS_FONT_STYLE_ITALIC:
        dump_printf(out, "italic");
        break;
    case CSS_FONT_STYLE_OBLIQUE:
        dump_printf(out, "oblique");
        break;
    }

    dump_printf(out, "\n|  font-weight: ");
    weight = css_font_face_font_weight(font_face);
    switch (weight) {
    case CSS_FONT_WEIGHT_INHERIT:
        dump_printf(out, "unspecified");
        break;
    case CSS_FONT_WEIGHT_NORMAL:
        dump_printf(out, "normal");
        break;
    case CSS_FONT_WEIGHT_BOLD:
        dump_printf(out, "bold");
        break;
    case CSS_FONT_WEIGHT_100:
        dump_printf(out, "100");
        break;
    case CSS_FONT_WEIGHT_200:
        dump_printf(out, "200");
        break;
    case CSS_FONT_WEIGHT_300:
        dump_printf(out, "300");
        break;
    case CSS_FONT_WEIGHT_400:
        dump_printf(out, "400");
        break;
    case CSS_FONT_WEIGHT_500:
        dump_printf(out, "500");
        break;
    case CSS_FONT_WEIGHT_600:
        dump_printf(out, "600");
        break;
    case CSS_FONT_WEIGHT_700:
        dump_printf(out, "700");
        break;
    case CSS_FONT_WEIGHT_800:
        dump_printf(out, "800");
        break;
    case CSS_FONT_WEIGHT_900:
        dump_printf(out, "900");
        break;
    default:
        dump_printf(out, "Unhandled weight %d\n", (int)weight);
        break;
    }


    if (font_face->srcs != NULL) {
        uint32_t i;
        css_font_face_src *srcs = font_face->srcs;
        for (i = 0; i < font_face->n_srcs; ++i) {
            css_font_face_format format;
            dump_printf(out, "\n|  src: ");

            format = css_font_face_src_format(&srcs[i]);

            dump_printf(out, "\n|   format: ");

            switch (format) {
            case CSS_FONT_FACE_FORMAT_UNSPECIFIED:
                dump_printf(out, "unspecified");
                break;
            case CSS_FONT_FACE_FORMAT_WOFF:
                dump_printf(out, "WOFF");
                break;
            case CSS_FONT_FACE_FORMAT_OPENTYPE:
                dump_printf(out, "OTF");
                break;
            case CSS_FONT_FACE_FORMAT_EMBEDDED_OPENTYPE:
                dump_printf(out, "EOTF");
                break;
            case CSS_FONT_FACE_FORMAT_SVG:
                dump_printf(out, "SVG");
                break;
            case CSS_FONT_FACE_FORMAT_UNKNOWN:
                dump_printf(out, "unknown");
                break;
            default:
                dump_printf(out, "UNEXPECTED");
                break;
            }

            if (srcs[i].location != NULL) {
                dump_printf(out, "\n|   location: ");

                switch (css_font_face_src_location_type(&srcs[i])) {
                case CSS_FONT_FACE_LOCATION_TYPE_LOCAL:
                    dump_printf(out, "local");
                    break;
                case CSS_FONT_FACE_LOCATION_TYPE_URI:
                    dump_printf(out, "url");
                    break;
                default:
                    dump_printf(out, "UNKNOWN");
                    break;
                }

                dump_printf(out, "(%.*s)", (int)lwc_string_length(srcs[i].location), lwc_string_data(srcs[i].location));
            }
        }
    }
}

#endif
