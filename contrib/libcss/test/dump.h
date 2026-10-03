#ifndef test_dump_h_
#define test_dump_h_

#include <string.h>

#include <libcss/types.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/font_face.h"
#include "stylesheet.h"

#include "testutils.h"

static void dump_sheet(css_stylesheet *sheet, char *buf, size_t *len);
static void dump_rule_selector(css_rule_selector *s, char **buf, size_t *buflen, uint32_t depth);
static void dump_rule_charset(css_rule_charset *s, char **buf, size_t *buflen);
static void dump_rule_import(css_rule_import *s, char **buf, size_t *buflen);
static void dump_rule_media(css_rule_media *s, char **buf, size_t *buflen);
static void dump_rule_page(css_rule_page *s, char **buf, size_t *buflen);
static void dump_rule_font_face(css_rule_font_face *s, char **buf, size_t *buflen);
static void dump_selector_list(css_selector *list, char **ptr);
static void dump_selector(css_selector *selector, char **ptr);
static void dump_selector_detail(css_selector_detail *detail, char **ptr);
static void dump_bytecode(css_style *style, char **ptr, uint32_t depth);
static void dump_string(lwc_string *string, char **ptr);
static void dump_font_face(css_font_face *font_face, char **ptr);

void dump_sheet(css_stylesheet *sheet, char *buf, size_t *buflen)
{
    css_rule *rule;

    for (rule = sheet->rule_list; rule != NULL; rule = rule->next) {
        switch (rule->type) {
        case CSS_RULE_SELECTOR:
            dump_rule_selector((css_rule_selector *)rule, &buf, buflen, 1);
            break;
        case CSS_RULE_CHARSET:
            dump_rule_charset((css_rule_charset *)rule, &buf, buflen);
            break;
        case CSS_RULE_IMPORT:
            dump_rule_import((css_rule_import *)rule, &buf, buflen);
            break;
        case CSS_RULE_MEDIA:
            dump_rule_media((css_rule_media *)rule, &buf, buflen);
            break;
        case CSS_RULE_PAGE:
            dump_rule_page((css_rule_page *)rule, &buf, buflen);
            break;
        case CSS_RULE_FONT_FACE:
            dump_rule_font_face((css_rule_font_face *)rule, &buf, buflen);
            break;
        default: {
            int written = sprintf(buf, "Unhandled rule type %d\n", rule->type);

            *buflen -= written;
            buf += written;
        } break;
        }
    }
}

void dump_rule_selector(css_rule_selector *s, char **buf, size_t *buflen, uint32_t depth)
{
    uint32_t i;
    char *ptr = *buf;

    *ptr++ = '|';
    for (i = 0; i < depth; i++)
        *ptr++ = ' ';

    /* Build selector string */
    for (i = 0; i < s->base.items; i++) {
        dump_selector_list(s->selectors[i], &ptr);
        if (i != (uint32_t)(s->base.items - 1)) {
            memcpy(ptr, ", ", 2);
            ptr += 2;
        }
    }
    *ptr++ = '\n';

    if (s->style != NULL)
        dump_bytecode(s->style, &ptr, depth + 1);

    *buflen -= ptr - *buf;
    *buf = ptr;
}

void dump_rule_charset(css_rule_charset *s, char **buf, size_t *buflen)
{
    char *ptr = *buf;

    ptr += sprintf(ptr, "| @charset(");
    dump_string(s->encoding, &ptr);
    *ptr++ = ')';
    *ptr++ = '\n';

    *buflen -= ptr - *buf;
    *buf = ptr;
}

void dump_rule_import(css_rule_import *s, char **buf, size_t *buflen)
{
    char *ptr = *buf;

    ptr += sprintf(ptr, "| @import url(\"%.*s\")", (int)lwc_string_length(s->url), lwc_string_data(s->url));

    /** \todo media list */

    *ptr++ = '\n';

    *buflen -= ptr - *buf;
    *buf = ptr;
}

void dump_rule_media(css_rule_media *s, char **buf, size_t *buflen)
{
    char *ptr = *buf;
    css_rule *rule;

    ptr += sprintf(ptr, "| @media %s%03" PRIx64, s->media->negate_type ? "not " : "", s->media->type);

    /* \todo media list */

    *ptr++ = '\n';

    for (rule = s->first_child; rule != NULL; rule = rule->next) {
        size_t len = *buflen - (ptr - *buf);

        dump_rule_selector((css_rule_selector *)rule, &ptr, &len, 2);
    }

    *buflen -= ptr - *buf;
    *buf = ptr;
}

void dump_rule_page(css_rule_page *s, char **buf, size_t *buflen)
{
    char *ptr = *buf;

    ptr += sprintf(ptr, "| @page ");

    if (s->selector != NULL)
        dump_selector_list(s->selector, &ptr);

    *ptr++ = '\n';

    if (s->style != NULL)
        dump_bytecode(s->style, &ptr, 2);

    *buflen -= ptr - *buf;
    *buf = ptr;
}

void dump_rule_font_face(css_rule_font_face *s, char **buf, size_t *buflen)
{
    char *ptr = *buf;

    ptr += sprintf(ptr, "| @font-face");

    if (s->font_face != NULL) {
        dump_font_face(s->font_face, &ptr);
    }

    *ptr++ = '\n';

    *buflen -= ptr - *buf;
    *buf = ptr;
}

void dump_selector_list(css_selector *list, char **ptr)
{
    if (list->combinator != NULL) {
        dump_selector_list(list->combinator, ptr);
    }

    switch (list->data.comb) {
    case CSS_COMBINATOR_NONE:
        break;
    case CSS_COMBINATOR_ANCESTOR:
        (*ptr)[0] = ' ';
        *ptr += 1;
        break;
    case CSS_COMBINATOR_PARENT:
        memcpy(*ptr, " > ", 3);
        *ptr += 3;
        break;
    case CSS_COMBINATOR_SIBLING:
        memcpy(*ptr, " + ", 3);
        *ptr += 3;
        break;
    case CSS_COMBINATOR_GENERIC_SIBLING:
        memcpy(*ptr, " + ", 3);
        *ptr += 3;
        break;
    }

    dump_selector(list, ptr);
}


void dump_selector(css_selector *selector, char **ptr)
{
    css_selector_detail *d = &selector->data;

    while (true) {
        dump_selector_detail(d, ptr);

        if (d->next == 0)
            break;

        d++;
    }
}

void dump_selector_detail(css_selector_detail *detail, char **ptr)
{
    if (detail->negate)
        *ptr += sprintf(*ptr, ":not(");

    switch (detail->type) {
    case CSS_SELECTOR_ELEMENT:
        if (lwc_string_length(detail->qname.name) == 1 && lwc_string_data(detail->qname.name)[0] == '*' &&
            detail->next == 0) {
            dump_string(detail->qname.name, ptr);
        } else if (lwc_string_length(detail->qname.name) != 1 || lwc_string_data(detail->qname.name)[0] != '*') {
            dump_string(detail->qname.name, ptr);
        }
        break;
    case CSS_SELECTOR_CLASS:
        **ptr = '.';
        *ptr += 1;
        dump_string(detail->qname.name, ptr);
        break;
    case CSS_SELECTOR_ID:
        **ptr = '#';
        *ptr += 1;
        dump_string(detail->qname.name, ptr);
        break;
    case CSS_SELECTOR_PSEUDO_CLASS:
    case CSS_SELECTOR_PSEUDO_ELEMENT:
        **ptr = ':';
        *ptr += 1;
        dump_string(detail->qname.name, ptr);
        if (detail->value_type == CSS_SELECTOR_DETAIL_VALUE_STRING) {
            if (detail->value.string != NULL) {
                **ptr = '(';
                *ptr += 1;
                dump_string(detail->value.string, ptr);
                **ptr = ')';
                *ptr += 1;
            }
        } else {
            *ptr += sprintf(*ptr, "(%dn+%d)", detail->value.nth.a, detail->value.nth.b);
        }
        break;
    case CSS_SELECTOR_ATTRIBUTE:
        **ptr = '[';
        *ptr += 1;
        dump_string(detail->qname.name, ptr);
        **ptr = ']';
        *ptr += 1;
        break;
    case CSS_SELECTOR_ATTRIBUTE_EQUAL:
        **ptr = '[';
        *ptr += 1;
        dump_string(detail->qname.name, ptr);
        (*ptr)[0] = '=';
        (*ptr)[1] = '"';
        *ptr += 2;
        dump_string(detail->value.string, ptr);
        (*ptr)[0] = '"';
        (*ptr)[1] = ']';
        *ptr += 2;
        break;
    case CSS_SELECTOR_ATTRIBUTE_DASHMATCH:
        **ptr = '[';
        *ptr += 1;
        dump_string(detail->qname.name, ptr);
        (*ptr)[0] = '|';
        (*ptr)[1] = '=';
        (*ptr)[2] = '"';
        *ptr += 3;
        dump_string(detail->value.string, ptr);
        (*ptr)[0] = '"';
        (*ptr)[1] = ']';
        *ptr += 2;
        break;
    case CSS_SELECTOR_ATTRIBUTE_INCLUDES:
        **ptr = '[';
        *ptr += 1;
        dump_string(detail->qname.name, ptr);
        (*ptr)[0] = '~';
        (*ptr)[1] = '=';
        (*ptr)[2] = '"';
        *ptr += 3;
        dump_string(detail->value.string, ptr);
        (*ptr)[0] = '"';
        (*ptr)[1] = ']';
        *ptr += 2;
        break;
    case CSS_SELECTOR_ATTRIBUTE_PREFIX:
        **ptr = '[';
        *ptr += 1;
        dump_string(detail->qname.name, ptr);
        (*ptr)[0] = '^';
        (*ptr)[1] = '=';
        (*ptr)[2] = '"';
        *ptr += 3;
        dump_string(detail->value.string, ptr);
        (*ptr)[0] = '"';
        (*ptr)[1] = ']';
        *ptr += 2;
        break;
    case CSS_SELECTOR_ATTRIBUTE_SUFFIX:
        **ptr = '[';
        *ptr += 1;
        dump_string(detail->qname.name, ptr);
        (*ptr)[0] = '$';
        (*ptr)[1] = '=';
        (*ptr)[2] = '"';
        *ptr += 3;
        dump_string(detail->value.string, ptr);
        (*ptr)[0] = '"';
        (*ptr)[1] = ']';
        *ptr += 2;
        break;
    case CSS_SELECTOR_ATTRIBUTE_SUBSTRING:
        **ptr = '[';
        *ptr += 1;
        dump_string(detail->qname.name, ptr);
        (*ptr)[0] = '*';
        (*ptr)[1] = '=';
        (*ptr)[2] = '"';
        *ptr += 3;
        dump_string(detail->value.string, ptr);
        (*ptr)[0] = '"';
        (*ptr)[1] = ']';
        *ptr += 2;
        break;
    }

    if (detail->negate)
        *ptr += sprintf(*ptr, ")");
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
    "background-repeat",
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
};

static void dump_css_fixed(css_fixed f, char **ptr)
{
#define ABS(x) (uint32_t)((x) < 0 ? -(x) : (x))
    uint32_t uintpart = FIXTOINT(ABS(f));
    /* + 500 to ensure round to nearest (division will truncate) */
    uint32_t fracpart = ((ABS(f) & 0x3ff) * 1000 + 500) / (1 << 10);
#undef ABS
    size_t flen = 0;
    char tmp[20];
    size_t tlen = 0;
    char *buf = *ptr;

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

    *ptr = buf;
}

static void dump_number(css_fixed val, char **ptr)
{
    if (INTTOFIX(FIXTOINT(val)) == val)
        *ptr += sprintf(*ptr, "%d", FIXTOINT(val));
    else
        dump_css_fixed(val, ptr);
}

static void dump_unit(css_fixed val, uint32_t unit, char **ptr)
{
    dump_number(val, ptr);

    switch (unit) {
    case UNIT_PX:
        *ptr += sprintf(*ptr, "px");
        break;
    case UNIT_EX:
        *ptr += sprintf(*ptr, "ex");
        break;
    case UNIT_EM:
        *ptr += sprintf(*ptr, "em");
        break;
    case UNIT_IN:
        *ptr += sprintf(*ptr, "in");
        break;
    case UNIT_CM:
        *ptr += sprintf(*ptr, "cm");
        break;
    case UNIT_MM:
        *ptr += sprintf(*ptr, "mm");
        break;
    case UNIT_PT:
        *ptr += sprintf(*ptr, "pt");
        break;
    case UNIT_PC:
        *ptr += sprintf(*ptr, "pc");
        break;
    case UNIT_CH:
        *ptr += sprintf(*ptr, "ch");
        break;
    case UNIT_REM:
        *ptr += sprintf(*ptr, "rem");
        break;
    case UNIT_LH:
        *ptr += sprintf(*ptr, "lh");
        break;
    case UNIT_VH:
        *ptr += sprintf(*ptr, "vh");
        break;
    case UNIT_VW:
        *ptr += sprintf(*ptr, "vw");
        break;
    case UNIT_VI:
        *ptr += sprintf(*ptr, "vi");
        break;
    case UNIT_VB:
        *ptr += sprintf(*ptr, "vb");
        break;
    case UNIT_VMIN:
        *ptr += sprintf(*ptr, "vmin");
        break;
    case UNIT_VMAX:
        *ptr += sprintf(*ptr, "vmax");
        break;
    case UNIT_Q:
        *ptr += sprintf(*ptr, "q");
        break;
    case UNIT_PCT:
        *ptr += sprintf(*ptr, "%%");
        break;
    case UNIT_DEG:
        *ptr += sprintf(*ptr, "deg");
        break;
    case UNIT_GRAD:
        *ptr += sprintf(*ptr, "grad");
        break;
    case UNIT_RAD:
        *ptr += sprintf(*ptr, "rad");
        break;
    case UNIT_MS:
        *ptr += sprintf(*ptr, "ms");
        break;
    case UNIT_S:
        *ptr += sprintf(*ptr, "s");
        break;
    case UNIT_HZ:
        *ptr += sprintf(*ptr, "Hz");
        break;
    case UNIT_KHZ:
        *ptr += sprintf(*ptr, "kHz");
        break;
    case UNIT_CALC_ANY:
        *ptr += sprintf(*ptr, "any");
        break;
    case UNIT_CALC_NUMBER:
        *ptr += sprintf(*ptr, "number");
        break;
    }
}

static void dump_counter(lwc_string *name, uint32_t value, char **ptr)
{
    *ptr += sprintf(*ptr, "counter(%.*s", (int)lwc_string_length(name), lwc_string_data(name));

    value >>= CONTENT_COUNTER_STYLE_SHIFT;

    switch (value) {
    case LIST_STYLE_TYPE_DISC:
        *ptr += sprintf(*ptr, ", disc");
        break;
    case LIST_STYLE_TYPE_CIRCLE:
        *ptr += sprintf(*ptr, ", circle");
        break;
    case LIST_STYLE_TYPE_SQUARE:
        *ptr += sprintf(*ptr, ", square");
        break;
    case LIST_STYLE_TYPE_DECIMAL:
        break;
    case LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO:
        *ptr += sprintf(*ptr, ", decimal-leading-zero");
        break;
    case LIST_STYLE_TYPE_LOWER_ROMAN:
        *ptr += sprintf(*ptr, ", lower-roman");
        break;
    case LIST_STYLE_TYPE_UPPER_ROMAN:
        *ptr += sprintf(*ptr, ", upper-roman");
        break;
    case LIST_STYLE_TYPE_LOWER_GREEK:
        *ptr += sprintf(*ptr, ", lower-greek");
        break;
    case LIST_STYLE_TYPE_LOWER_LATIN:
        *ptr += sprintf(*ptr, ", lower-latin");
        break;
    case LIST_STYLE_TYPE_UPPER_LATIN:
        *ptr += sprintf(*ptr, ", upper-latin");
        break;
    case LIST_STYLE_TYPE_ARMENIAN:
        *ptr += sprintf(*ptr, ", armenian");
        break;
    case LIST_STYLE_TYPE_GEORGIAN:
        *ptr += sprintf(*ptr, ", georgian");
        break;
    case LIST_STYLE_TYPE_LOWER_ALPHA:
        *ptr += sprintf(*ptr, ", lower-alpha");
        break;
    case LIST_STYLE_TYPE_UPPER_ALPHA:
        *ptr += sprintf(*ptr, ", upper-alpha");
        break;
    case LIST_STYLE_TYPE_NONE:
        *ptr += sprintf(*ptr, ", none");
        break;
    }

    *ptr += sprintf(*ptr, ")");
}

static void dump_counters(lwc_string *name, lwc_string *separator, uint32_t value, char **ptr)
{
    *ptr += sprintf(*ptr, "counters(%.*s, %.*s", (int)lwc_string_length(name), lwc_string_data(name),
        (int)lwc_string_length(separator), lwc_string_data(separator));

    value >>= CONTENT_COUNTERS_STYLE_SHIFT;

    switch (value) {
    case LIST_STYLE_TYPE_DISC:
        *ptr += sprintf(*ptr, ", disc");
        break;
    case LIST_STYLE_TYPE_CIRCLE:
        *ptr += sprintf(*ptr, ", circle");
        break;
    case LIST_STYLE_TYPE_SQUARE:
        *ptr += sprintf(*ptr, ", square");
        break;
    case LIST_STYLE_TYPE_DECIMAL:
        break;
    case LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO:
        *ptr += sprintf(*ptr, ", decimal-leading-zero");
        break;
    case LIST_STYLE_TYPE_LOWER_ROMAN:
        *ptr += sprintf(*ptr, ", lower-roman");
        break;
    case LIST_STYLE_TYPE_UPPER_ROMAN:
        *ptr += sprintf(*ptr, ", upper-roman");
        break;
    case LIST_STYLE_TYPE_LOWER_GREEK:
        *ptr += sprintf(*ptr, ", lower-greek");
        break;
    case LIST_STYLE_TYPE_LOWER_LATIN:
        *ptr += sprintf(*ptr, ", lower-latin");
        break;
    case LIST_STYLE_TYPE_UPPER_LATIN:
        *ptr += sprintf(*ptr, ", upper-latin");
        break;
    case LIST_STYLE_TYPE_ARMENIAN:
        *ptr += sprintf(*ptr, ", armenian");
        break;
    case LIST_STYLE_TYPE_GEORGIAN:
        *ptr += sprintf(*ptr, ", georgian");
        break;
    case LIST_STYLE_TYPE_LOWER_ALPHA:
        *ptr += sprintf(*ptr, ", lower-alpha");
        break;
    case LIST_STYLE_TYPE_UPPER_ALPHA:
        *ptr += sprintf(*ptr, ", upper-alpha");
        break;
    case LIST_STYLE_TYPE_NONE:
        *ptr += sprintf(*ptr, ", none");
        break;
    }

    *ptr += sprintf(*ptr, ")");
}

#include "dump_bytecode.h"

void dump_string(lwc_string *string, char **ptr)
{
    *ptr += sprintf(*ptr, "%.*s", (int)lwc_string_length(string), lwc_string_data(string));
}

void dump_font_face(css_font_face *font_face, char **ptr)
{
    uint8_t style, weight;

    if (font_face->font_family != NULL) {
        *(*ptr)++ = '\n';
        *ptr += sprintf(*ptr, "|  font-family: %.*s", (int)lwc_string_length(font_face->font_family),
            lwc_string_data(font_face->font_family));
    }

    *ptr += sprintf(*ptr, "\n|  font-style: ");
    style = css_font_face_font_style(font_face);
    switch (style) {
    case CSS_FONT_STYLE_INHERIT:
        *ptr += sprintf(*ptr, "unspecified");
        break;
    case CSS_FONT_STYLE_NORMAL:
        *ptr += sprintf(*ptr, "normal");
        break;
    case CSS_FONT_STYLE_ITALIC:
        *ptr += sprintf(*ptr, "italic");
        break;
    case CSS_FONT_STYLE_OBLIQUE:
        *ptr += sprintf(*ptr, "oblique");
        break;
    }

    *ptr += sprintf(*ptr, "\n|  font-weight: ");
    weight = css_font_face_font_weight(font_face);
    switch (weight) {
    case CSS_FONT_WEIGHT_INHERIT:
        *ptr += sprintf(*ptr, "unspecified");
        break;
    case CSS_FONT_WEIGHT_NORMAL:
        *ptr += sprintf(*ptr, "normal");
        break;
    case CSS_FONT_WEIGHT_BOLD:
        *ptr += sprintf(*ptr, "bold");
        break;
    case CSS_FONT_WEIGHT_100:
        *ptr += sprintf(*ptr, "100");
        break;
    case CSS_FONT_WEIGHT_200:
        *ptr += sprintf(*ptr, "200");
        break;
    case CSS_FONT_WEIGHT_300:
        *ptr += sprintf(*ptr, "300");
        break;
    case CSS_FONT_WEIGHT_400:
        *ptr += sprintf(*ptr, "400");
        break;
    case CSS_FONT_WEIGHT_500:
        *ptr += sprintf(*ptr, "500");
        break;
    case CSS_FONT_WEIGHT_600:
        *ptr += sprintf(*ptr, "600");
        break;
    case CSS_FONT_WEIGHT_700:
        *ptr += sprintf(*ptr, "700");
        break;
    case CSS_FONT_WEIGHT_800:
        *ptr += sprintf(*ptr, "800");
        break;
    case CSS_FONT_WEIGHT_900:
        *ptr += sprintf(*ptr, "900");
        break;
    default:
        *ptr += sprintf(*ptr, "Unhandled weight %d\n", (int)weight);
        break;
    }


    if (font_face->srcs != NULL) {
        uint32_t i;
        css_font_face_src *srcs = font_face->srcs;
        for (i = 0; i < font_face->n_srcs; ++i) {
            css_font_face_format format;
            *ptr += sprintf(*ptr, "\n|  src: ");

            format = css_font_face_src_format(&srcs[i]);

            *ptr += sprintf(*ptr, "\n|   format: ");

            switch (format) {
            case CSS_FONT_FACE_FORMAT_UNSPECIFIED:
                *ptr += sprintf(*ptr, "unspecified");
                break;
            case CSS_FONT_FACE_FORMAT_WOFF:
                *ptr += sprintf(*ptr, "WOFF");
                break;
            case CSS_FONT_FACE_FORMAT_OPENTYPE:
                *ptr += sprintf(*ptr, "OTF");
                break;
            case CSS_FONT_FACE_FORMAT_EMBEDDED_OPENTYPE:
                *ptr += sprintf(*ptr, "EOTF");
                break;
            case CSS_FONT_FACE_FORMAT_SVG:
                *ptr += sprintf(*ptr, "SVG");
                break;
            case CSS_FONT_FACE_FORMAT_UNKNOWN:
                *ptr += sprintf(*ptr, "unknown");
                break;
            default:
                *ptr += sprintf(*ptr, "UNEXPECTED");
                break;
            }

            if (srcs[i].location != NULL) {
                *ptr += sprintf(*ptr, "\n|   location: ");

                switch (css_font_face_src_location_type(&srcs[i])) {
                case CSS_FONT_FACE_LOCATION_TYPE_LOCAL:
                    *ptr += sprintf(*ptr, "local");
                    break;
                case CSS_FONT_FACE_LOCATION_TYPE_URI:
                    *ptr += sprintf(*ptr, "url");
                    break;
                default:
                    *ptr += sprintf(*ptr, "UNKNOWN");
                    break;
                }

                *ptr += sprintf(
                    *ptr, "(%.*s)", (int)lwc_string_length(srcs[i].location), lwc_string_data(srcs[i].location));
            }
        }
    }
}

#endif
