/*
 * This file is part of Hubbub.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 Andrew Sidwell <takkaria@netsurf-browser.org>
 */

#include <assert.h>
#include <string.h>

#include "utils/string.h"
#include "utils/utils.h"
#include "treebuilder/internal.h"
#include "treebuilder/modes.h"
#include "treebuilder/treebuilder.h"


/*** Attribute-correction stuff ***/

#define S(s) s, SLEN(s)

/**
 * Mapping table for case changes
 */
typedef struct {
    const char *attr; /**< Lower case attribute name */
    size_t len; /**< Length of name in bytes */
    const char *proper; /**< Correctly cased version */
} case_changes;

static const case_changes svg_attributes[] = {
    {S("attributename"), "attributeName"},
    {S("attributetype"), "attributeType"},
    {S("basefrequency"), "baseFrequency"},
    {S("baseprofile"), "baseProfile"},
    {S("calcmode"), "calcMode"},
    {S("clippathunits"), "clipPathUnits"},
    {S("contentscripttype"), "contentScriptType"},
    {S("contentstyletype"), "contentStyleType"},
    {S("diffuseconstant"), "diffuseConstant"},
    {S("edgemode"), "edgeMode"},
    {S("externalresourcesrequired"), "externalResourcesRequired"},
    {S("filterres"), "filterRes"},
    {S("filterunits"), "filterUnits"},
    {S("glyphref"), "glyphRef"},
    {S("gradienttransform"), "gradientTransform"},
    {S("gradientunits"), "gradientUnits"},
    {S("kernelmatrix"), "kernelMatrix"},
    {S("kernelunitlength"), "kernelUnitLength"},
    {S("keypoints"), "keyPoints"},
    {S("keysplines"), "keySplines"},
    {S("keytimes"), "keyTimes"},
    {S("lengthadjust"), "lengthAdjust"},
    {S("limitingconeangle"), "limitingConeAngle"},
    {S("markerheight"), "markerHeight"},
    {S("markerunits"), "markerUnits"},
    {S("markerwidth"), "markerWidth"},
    {S("maskcontentunits"), "maskContentUnits"},
    {S("maskunits"), "maskUnits"},
    {S("numoctaves"), "numOctaves"},
    {S("pathlength"), "pathLength"},
    {S("patterncontentunits"), "patternContentUnits"},
    {S("patterntransform"), "patternTransform"},
    {S("patternunits"), "patternUnits"},
    {S("pointsatx"), "pointsAtX"},
    {S("pointsaty"), "pointsAtY"},
    {S("pointsatz"), "pointsAtZ"},
    {S("preservealpha"), "preserveAlpha"},
    {S("preserveaspectratio"), "preserveAspectRatio"},
    {S("primitiveunits"), "primitiveUnits"},
    {S("refx"), "refX"},
    {S("refy"), "refY"},
    {S("repeatcount"), "repeatCount"},
    {S("repeatdur"), "repeatDur"},
    {S("requiredextensions"), "requiredExtensions"},
    {S("requiredfeatures"), "requiredFeatures"},
    {S("specularconstant"), "specularConstant"},
    {S("specularexponent"), "specularExponent"},
    {S("spreadmethod"), "spreadMethod"},
    {S("startoffset"), "startOffset"},
    {S("stddeviation"), "stdDeviation"},
    {S("stitchtiles"), "stitchTiles"},
    {S("surfacescale"), "surfaceScale"},
    {S("systemlanguage"), "systemLanguage"},
    {S("tablevalues"), "tableValues"},
    {S("targetx"), "targetX"},
    {S("targety"), "targetY"},
    {S("textlength"), "textLength"},
    {S("viewbox"), "viewBox"},
    {S("viewtarget"), "viewTarget"},
    {S("xchannelselector"), "xChannelSelector"},
    {S("ychannelselector"), "yChannelSelector"},
    {S("zoomandpan"), "zoomAndPan"},
};

static const case_changes svg_tagnames[] = {
    {S("altglyph"), "altGlyph"},
    {S("altglyphdef"), "altGlyphDef"},
    {S("altglyphitem"), "altGlyphItem"},
    {S("animatecolor"), "animateColor"},
    {S("animatemotion"), "animateMotion"},
    {S("animatetransform"), "animateTransform"},
    {S("clippath"), "clipPath"},
    {S("feblend"), "feBlend"},
    {S("fecolormatrix"), "feColorMatrix"},
    {S("fecomponenttransfer"), "feComponentTransfer"},
    {S("fecomposite"), "feComposite"},
    {S("feconvolvematrix"), "feConvolveMatrix"},
    {S("fediffuselighting"), "feDiffuseLighting"},
    {S("fedisplacementmap"), "feDisplacementMap"},
    {S("fedistantlight"), "feDistantLight"},
    {S("fedropshadow"), "feDropShadow"},
    {S("feflood"), "feFlood"},
    {S("fefunca"), "feFuncA"},
    {S("fefuncb"), "feFuncB"},
    {S("fefuncg"), "feFuncG"},
    {S("fefuncr"), "feFuncR"},
    {S("fegaussianblur"), "feGaussianBlur"},
    {S("feimage"), "feImage"},
    {S("femerge"), "feMerge"},
    {S("femergenode"), "feMergeNode"},
    {S("femorphology"), "feMorphology"},
    {S("feoffset"), "feOffset"},
    {S("fepointlight"), "fePointLight"},
    {S("fespecularlighting"), "feSpecularLighting"},
    {S("fespotlight"), "feSpotLight"},
    {S("fetile"), "feTile"},
    {S("feturbulence"), "feTurbulence"},
    {S("foreignobject"), "foreignObject"},
    {S("glyphref"), "glyphRef"},
    {S("lineargradient"), "linearGradient"},
    {S("radialgradient"), "radialGradient"},
    {S("textpath"), "textPath"},
};

#undef S

/**
 * Adjust MathML attributes
 *
 * \param treebuilder  Treebuilder instance
 * \param tag          Tag to adjust the attributes of
 */
void adjust_mathml_attributes(hubbub_treebuilder *treebuilder, hubbub_tag *tag)
{
    size_t i;
    UNUSED(treebuilder);

    for (i = 0; i < tag->n_attributes; i++) {
        hubbub_attribute *attr = &tag->attributes[i];
        const uint8_t *name = attr->name.ptr;
        size_t len = attr->name.len;

        if (hubbub_string_match(name, len, (const uint8_t *)"definitionurl", SLEN("definitionurl"))) {
            attr->name.ptr = (uint8_t *)"definitionURL";
        }
    }
}

/**
 * Adjust SVG attributes.
 *
 * \param treebuilder	Treebuilder instance
 * \param tag		Tag to adjust the attributes of
 */
void adjust_svg_attributes(hubbub_treebuilder *treebuilder, hubbub_tag *tag)
{
    size_t i;
    UNUSED(treebuilder);

    for (i = 0; i < tag->n_attributes; i++) {
        hubbub_attribute *attr = &tag->attributes[i];

        const uint8_t *name = attr->name.ptr;
        size_t len = attr->name.len;
        size_t j;

        for (j = 0; j < N_ELEMENTS(svg_attributes); j++) {
            if (hubbub_string_match(name, len, (uint8_t *)svg_attributes[j].attr, svg_attributes[j].len)) {
                attr->name.ptr = (uint8_t *)svg_attributes[j].proper;
            }
        }
    }
}

/**
 * Adjust SVG tagnmes.
 *
 * \param treebuilder	Treebuilder instance
 * \param tag		Tag to adjust the name of
 */
void adjust_svg_tagname(hubbub_treebuilder *treebuilder, hubbub_tag *tag)
{
    const uint8_t *name = tag->name.ptr;
    size_t len = tag->name.len;
    size_t i;

    UNUSED(treebuilder);

    for (i = 0; i < N_ELEMENTS(svg_tagnames); i++) {
        if (hubbub_string_match(name, len, (uint8_t *)svg_tagnames[i].attr, svg_tagnames[i].len)) {
            tag->name.ptr = (uint8_t *)svg_tagnames[i].proper;
        }
    }
}


#define S(s) (uint8_t *)s, SLEN(s)

/**
 * Adjust foreign attributes.
 *
 * \param treebuilder	Treebuilder instance
 * \param tag		Tag to adjust the attributes of
 */
void adjust_foreign_attributes(hubbub_treebuilder *treebuilder, hubbub_tag *tag)
{
    size_t i;
    UNUSED(treebuilder);

    for (i = 0; i < tag->n_attributes; i++) {
        hubbub_attribute *attr = &tag->attributes[i];
        const uint8_t *name = attr->name.ptr;

        /* 10 == strlen("xlink:href") */
        if (attr->name.len >= 10 && strncmp((char *)name, "xlink:", SLEN("xlink:")) == 0) {
            size_t len = attr->name.len - 6;
            name += 6;

            if (hubbub_string_match(name, len, S("actuate")) || hubbub_string_match(name, len, S("arcrole")) ||
                hubbub_string_match(name, len, S("href")) || hubbub_string_match(name, len, S("role")) ||
                hubbub_string_match(name, len, S("show")) || hubbub_string_match(name, len, S("title")) ||
                hubbub_string_match(name, len, S("type"))) {
                attr->ns = HUBBUB_NS_XLINK;
                attr->name.ptr += 6;
                attr->name.len -= 6;
            }
            /* 8 == strlen("xml:base") */
        } else if (attr->name.len >= 8 && strncmp((char *)name, "xml:", SLEN("xml:")) == 0) {
            size_t len = attr->name.len - 4;
            name += 4;

            if (hubbub_string_match(name, len, S("base")) || hubbub_string_match(name, len, S("lang")) ||
                hubbub_string_match(name, len, S("space"))) {
                attr->ns = HUBBUB_NS_XML;
                attr->name.ptr += 4;
                attr->name.len -= 4;
            }
        } else if (hubbub_string_match(name, attr->name.len, S("xmlns"))) {
            attr->ns = HUBBUB_NS_XMLNS;
        } else if (hubbub_string_match(name, attr->name.len, S("xmlns:xlink"))) {
            attr->ns = HUBBUB_NS_XMLNS;
            attr->name.ptr += 6;
            attr->name.len -= 6;
        }
    }
}

#undef S


/* Foreign content is dispatched per token; it is not an insertion mode.
 * https://html.spec.whatwg.org/multipage/parsing.html#tree-construction */
bool is_mathml_text_integration_point(const element_context *node)
{
    return node->ns == HUBBUB_NS_MATHML &&
        (node->type == MI || node->type == MO || node->type == MN || node->type == MS || node->type == MTEXT);
}

bool is_html_integration_point(const hubbub_tag *tag, element_type type)
{
    if (tag->ns == HUBBUB_NS_SVG)
        return type == FOREIGNOBJECT || type == DESC || type == TITLE;
    if (tag->ns == HUBBUB_NS_MATHML && type == ANNOTATION_XML) {
        for (uint32_t i = 0; i < tag->n_attributes; i++) {
            const hubbub_attribute *attr = &tag->attributes[i];
            if (attr->ns == HUBBUB_NS_NULL &&
                hubbub_string_match(attr->name.ptr, attr->name.len, (const uint8_t *)"encoding", SLEN("encoding"))) {
                return hubbub_string_match_ci(
                           attr->value.ptr, attr->value.len, (const uint8_t *)"text/html", SLEN("text/html")) ||
                    hubbub_string_match_ci(attr->value.ptr, attr->value.len, (const uint8_t *)"application/xhtml+xml",
                        SLEN("application/xhtml+xml"));
            }
        }
    }
    return false;
}

bool token_uses_foreign_rules(hubbub_treebuilder *treebuilder, const hubbub_token *token)
{
    if (treebuilder->context.element_stack[0].type != HTML || token->type == HUBBUB_TOKEN_EOF)
        return false;
    const element_context *node = adjusted_current_node(treebuilder);
    if (node->ns == HUBBUB_NS_HTML)
        return false;
    if (token->type == HUBBUB_TOKEN_START_TAG) {
        element_type type = element_type_from_name(treebuilder, &token->data.tag.name);
        if ((is_mathml_text_integration_point(node) && type != MGLYPH && type != MALIGNMARK) ||
            (node->ns == HUBBUB_NS_MATHML && node->type == ANNOTATION_XML && type == SVG) ||
            node->html_integration_point)
            return false;
    } else if (token->type == HUBBUB_TOKEN_CHARACTER &&
        (is_mathml_text_integration_point(node) || node->html_integration_point)) {
        return false;
    }
    return true;
}

/* Known token types are a cheap identity check. Unknown names must be compared
 * by local name: <g>, <path> and custom HTML tags must not all match UNKNOWN.
 * Borrow the DOM's name only on that slow path, with no copying or allocation. */
hubbub_error element_name_matches(hubbub_treebuilder *treebuilder, const element_context *node,
    const hubbub_string *name, element_type type, bool *match)
{
    *match = node->type == type;
    if (!*match || type != UNKNOWN)
        return HUBBUB_OK;
    hubbub_string local;
    hubbub_error err = treebuilder->tree_handler->get_name(treebuilder->tree_handler->ctx, node->node, &local);
    if (err == HUBBUB_OK)
        *match = hubbub_string_match_ci(local.ptr, local.len, name->ptr, name->len);
    return err;
}

bool is_special_node(const element_context *node)
{
    if (node->ns == HUBBUB_NS_HTML)
        return !is_formatting_element(node->type) && !is_phrasing_element(node->type);
    return is_mathml_text_integration_point(node) || (node->ns == HUBBUB_NS_MATHML && node->type == ANNOTATION_XML) ||
        (node->ns == HUBBUB_NS_SVG && (node->type == FOREIGNOBJECT || node->type == DESC || node->type == TITLE));
}

static void pop_foreign_node(hubbub_treebuilder *treebuilder)
{
    hubbub_ns ns;
    element_type type;
    void *node;
    element_stack_pop(treebuilder, &ns, &type, &node);
    treebuilder->tree_handler->unref_node(treebuilder->tree_handler->ctx, node);
}

static bool foreign_start_breaks_out(hubbub_treebuilder *treebuilder, const hubbub_tag *tag)
{
    element_type type = element_type_from_name(treebuilder, &tag->name);
    if (type == FONT) {
        for (uint32_t i = 0; i < tag->n_attributes; i++) {
            const hubbub_string *name = &tag->attributes[i].name;
            if (hubbub_string_match(name->ptr, name->len, (const uint8_t *)"color", SLEN("color")) ||
                hubbub_string_match(name->ptr, name->len, (const uint8_t *)"face", SLEN("face")) ||
                hubbub_string_match(name->ptr, name->len, (const uint8_t *)"size", SLEN("size")))
                return true;
        }
        return false;
    }
    return type == B || type == BIG || type == BLOCKQUOTE || type == BODY || type == BR || type == CENTER ||
        type == CODE || type == DD || type == DIV || type == DL || type == DT || type == EM || type == EMBED ||
        type == H1 || type == H2 || type == H3 || type == H4 || type == H5 || type == H6 || type == HEAD ||
        type == HR || type == I || type == IMG || type == LI || type == LISTING || type == MENU || type == META ||
        type == NOBR || type == OL || type == P || type == PRE || type == RUBY || type == S || type == SMALL ||
        type == SPAN || type == STRONG || type == STRIKE || type == SUB || type == SUP || type == TABLE || type == TT ||
        type == U || type == UL || type == VAR;
}

static hubbub_error foreign_break_out(hubbub_treebuilder *treebuilder, const hubbub_token *token)
{
    /* Use the real current node, not the adjusted fragment context. Never pop
     * the synthetic HTML root, nor cross an integration point. */
    const element_context *node = &treebuilder->context.element_stack[treebuilder->context.current_node];
    while (node->ns != HUBBUB_NS_HTML && !node->html_integration_point && !is_mathml_text_integration_point(node)) {
        pop_foreign_node(treebuilder);
        node = &treebuilder->context.element_stack[treebuilder->context.current_node];
    }
    return process_html_token(treebuilder, token);
}

static hubbub_error foreign_end_tag(hubbub_treebuilder *treebuilder, const hubbub_token *token)
{
    element_type type = element_type_from_name(treebuilder, &token->data.tag.name);
    if (type == BR || type == P)
        return foreign_break_out(treebuilder, token);
    const element_context *stack = treebuilder->context.element_stack;
    for (uint32_t i = treebuilder->context.current_node; i > 0; i--) {
        bool match;
        hubbub_error err = element_name_matches(treebuilder, &stack[i], &token->data.tag.name, type, &match);
        if (err != HUBBUB_OK)
            return err;
        if (match) {
            while (treebuilder->context.current_node >= i)
                pop_foreign_node(treebuilder);
            return HUBBUB_OK;
        }
        if (stack[i - 1].ns == HUBBUB_NS_HTML)
            return process_html_token(treebuilder, token);
    }
    return HUBBUB_OK;
}

hubbub_error handle_in_foreign_content(hubbub_treebuilder *treebuilder, const hubbub_token *token)
{
    switch (token->type) {
    case HUBBUB_TOKEN_CHARACTER:
        /* The tokenizer replaces NUL with U+FFFD before emitting it. */
        for (size_t i = 0; i < token->data.character.len; i++) {
            uint8_t c = token->data.character.ptr[i];
            if (c != '\t' && c != '\n' && c != '\f' && c != '\r' && c != ' ') {
                treebuilder->context.frameset_ok = false;
                break;
            }
        }
        return append_text(treebuilder, &token->data.character);
    case HUBBUB_TOKEN_COMMENT:
        return process_comment_append(
            treebuilder, token, treebuilder->context.element_stack[treebuilder->context.current_node].node);
    case HUBBUB_TOKEN_DOCTYPE:
        return HUBBUB_OK;
    case HUBBUB_TOKEN_START_TAG: {
        if (foreign_start_breaks_out(treebuilder, &token->data.tag))
            return foreign_break_out(treebuilder, token);
        hubbub_tag tag = token->data.tag;
        tag.ns = adjusted_current_node(treebuilder)->ns;
        if (tag.ns == HUBBUB_NS_SVG) {
            adjust_svg_tagname(treebuilder, &tag);
            adjust_svg_attributes(treebuilder, &tag);
        } else if (tag.ns == HUBBUB_NS_MATHML) {
            adjust_mathml_attributes(treebuilder, &tag);
        }
        adjust_foreign_attributes(treebuilder, &tag);
        hubbub_error err = insert_element(treebuilder, &tag, true);
        if (err == HUBBUB_OK && tag.self_closing)
            pop_foreign_node(treebuilder);
        /* SVG script execution is not exposed by the browser. SVG scripts
         * stay foreign elements; never pass them to the HTML script callback. */
        return err;
    }
    case HUBBUB_TOKEN_END_TAG:
        return foreign_end_tag(treebuilder, token);
    case HUBBUB_TOKEN_EOF:
        return process_html_token(treebuilder, token);
    }
    return HUBBUB_OK;
}
