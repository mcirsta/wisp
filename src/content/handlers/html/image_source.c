/* SPDX-License-Identifier: GPL-2.0-or-later
 * HTML source-set parsing and selection, shared by requests and box discovery.
 * https://html.spec.whatwg.org/multipage/images.html#parsing-a-srcset-attribute */
#include "image.h"
#include "hyperlink.h"
#include <float.h>
#include <math.h>
#include <string.h>
#include <libcss/sizes.h>
#include <wisp/content/handlers/html/private.h>

#include <wisp/utils/corestrings.h>
#include <wisp/utils/nsoption.h>
#include <wisp/utils/nsurl.h>

/* Infra ASCII whitespace excludes vertical tab. */
static bool source_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

struct image_candidate {
    const char *url;
    size_t length;
    double width, density;
    bool has_width, has_density, has_height, invalid;
};

/* HTML valid floating-point spelling, independent of the process locale.
 * Leading '+' and a decimal point with no following digits are not valid. */
static bool descriptor_number(const char *s, size_t length, bool integer, double *out)
{
    size_t i = 0;
    bool negative = !integer && length > 0 && s[0] == '-';
    if (negative)
        i++;
    long double value = 0;
    size_t digits = 0;
    while (i < length && s[i] >= '0' && s[i] <= '9') {
        value = fminl(LDBL_MAX, value * 10 + s[i++] - '0');
        digits++;
    }
    if (!integer && i < length && s[i] == '.') {
        i++;
        size_t start = i;
        long double place = .1L;
        while (i < length && s[i] >= '0' && s[i] <= '9') {
            value += (s[i++] - '0') * place;
            place /= 10;
            digits++;
        }
        if (i == start)
            return false;
    }
    if (!digits)
        return false;
    if (!integer && i < length && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        bool minus = i < length && s[i] == '-';
        if (i < length && (s[i] == '-' || s[i] == '+'))
            i++;
        size_t start = i;
        unsigned exponent = 0;
        while (i < length && s[i] >= '0' && s[i] <= '9') {
            exponent = exponent < 10000 ? exponent * 10 + s[i] - '0' : 10000;
            i++;
        }
        if (i == start)
            return false;
        if (value != 0)
            value *= powl(10, minus ? -(long double)exponent : exponent);
    }
    if (i != length || (negative && value != 0))
        return false;
    /* Width descriptors are integers, bounded only by implementation limits. */
    *out = integer ? (double)fminl(value, DBL_MAX) : (double)value;
    return isfinite(*out);
}

static void candidate_descriptor(struct image_candidate *candidate, const char *s, size_t length)
{
    double value;
    if (length < 2) {
        candidate->invalid = true;
        return;
    }
    char type = s[length - 1];
    if (!descriptor_number(s, length - 1, type != 'x', &value)) {
        candidate->invalid = true;
        return;
    }
    switch (type) {
    case 'w':
        candidate->invalid |= candidate->has_width || candidate->has_density || value == 0;
        candidate->has_width = true;
        candidate->width = value;
        break;
    case 'x':
        candidate->invalid |= candidate->has_width || candidate->has_density || candidate->has_height;
        candidate->has_density = true;
        candidate->density = value;
        break;
    case 'h': /* Future-compatible height is ignored, but requires a width. */
        candidate->invalid |= candidate->has_height || candidate->has_density || value == 0;
        candidate->has_height = true;
        break;
    default:
        candidate->invalid = true;
    }
}

/* URLs end at ASCII whitespace, not commas inside a URL. Descriptor commas
 * separate candidates except while in parentheses (HTML's recovery state). */
static bool candidate_next(const char *text, size_t length, size_t *position, struct image_candidate *candidate)
{
    size_t p = *position;
    while (p < length && (source_space(text[p]) || text[p] == ','))
        p++;
    if (p == length)
        return false;
    *candidate = (struct image_candidate){.url = text + p, .density = 1};
    size_t start = p;
    while (p < length && !source_space(text[p]))
        p++;
    size_t end = p;
    while (end > start && text[end - 1] == ',')
        end--;
    candidate->length = end - start;
    if (end == p) {
        while (p < length) {
            while (p < length && source_space(text[p]))
                p++;
            if (p == length || text[p] == ',') {
                if (p < length)
                    p++;
                break;
            }
            start = p;
            bool parens = false;
            while (p < length) {
                char c = text[p];
                if (!parens && (source_space(c) || c == ','))
                    break;
                if (c == '(')
                    parens = true;
                else if (c == ')')
                    parens = false;
                p++;
            }
            candidate_descriptor(candidate, text + start, p - start);
        }
    }
    candidate->invalid |= candidate->length == 0 || (candidate->has_height && !candidate->has_width);
    *position = p;
    return true;
}

static void consider_candidate(struct image_candidate *best, const struct image_candidate *candidate, double target)
{
    /* UA policy: smallest density meeting the target, else largest available.
     * Keep the first candidate at a duplicate density, as HTML requires. */
    if (best->url == NULL ||
        (best->density < target ? candidate->density > best->density
                                : candidate->density >= target && candidate->density < best->density))
        *best = *candidate;
}

bool html_image_source_parse(dom_string *src, dom_string *srcset, dom_string *sizes, const css_unit_ctx *units,
    const css_media *media, double target_density, nsurl *base, nsurl **url, double *density)
{
    *url = NULL;
    if (density != NULL)
        *density = 1;
    struct image_candidate best = {0}, candidate;
    bool width_present = false, one_present = false, sized = false;
    double source_size = (double)units->viewport_width / F_1;
    size_t pos = 0;
    if (srcset != NULL) {
        const char *text = dom_string_data(srcset);
        while (candidate_next(text, dom_string_byte_length(srcset), &pos, &candidate)) {
            if (candidate.invalid)
                continue;
            width_present |= candidate.has_width;
            one_present |= !candidate.has_width && candidate.density == 1;
            if (candidate.has_width) {
                if (!sized && sizes != NULL) {
                    /* No native loading=lazy mode yet; auto falls through to
                     * its fallback. Script auto-sizing writes an actual length. */
                    css_error err = css_source_size((const uint8_t *)dom_string_data(sizes),
                        dom_string_byte_length(sizes), units, media, -1, &source_size);
                    if (err != CSS_OK)
                        return false;
                }
                sized = true;
                candidate.density = source_size > 0 ? candidate.width / source_size : INFINITY;
            }
            consider_candidate(&best, &candidate, target_density);
        }
    }
    if (!width_present && !one_present && src != NULL && dom_string_byte_length(src) != 0) {
        candidate = (struct image_candidate){
            .url = dom_string_data(src), .length = dom_string_byte_length(src), .density = 1};
        consider_candidate(&best, &candidate, target_density);
    }
    if (best.url == NULL)
        return true;
    dom_string *selected;
    if (dom_string_create((const uint8_t *)best.url, best.length, &selected) != DOM_NO_ERR)
        return false;
    nserror err = html_link_url(selected, base, url);
    dom_string_unref(selected);
    if (density != NULL)
        *density = best.density;
    return err != NSERROR_NOMEM;
}

bool html_image_select_source(dom_node *node, html_content *html, nsurl **url, double *density)
{
    *url = NULL;
    if (density != NULL)
        *density = 1;
    if (!nsoption_bool(foreground_images))
        return true;
    dom_string *src = NULL, *srcset = NULL, *sizes = NULL;
    dom_exception err = dom_element_get_attribute(node, corestring_dom_src, &src);
    if (err == DOM_NO_ERR)
        err = dom_element_get_attribute(node, corestring_dom_srcset, &srcset);
    if (err == DOM_NO_ERR)
        err = dom_element_get_attribute(node, corestring_dom_sizes, &sizes);
    /* The parser discovers images before initial style/layout has filled in
     * viewport units. Query actual frontend dimensions, never a guessed width. */
    css_unit_ctx units = html->unit_len_ctx;
    css_media media = html->media;
    if (srcset != NULL && dom_string_byte_length(srcset) != 0)
        html_get_viewport_context(html, &units, &media);
    /* Density choice is UA policy. A 1x target preserves our existing bandwidth
     * preference; selected-source density still controls natural dimensions. */
    bool ok = err == DOM_NO_ERR &&
        html_image_source_parse(src, srcset, sizes, &units, &media, 1, html->base_url, url, density);
    dom_string_unref(src);
    dom_string_unref(srcset);
    dom_string_unref(sizes);
    return ok;
}
