/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *			http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 */

#include <inttypes.h>
#include <stddef.h>

#include "utils/validate.h"

#include <dom/core/string.h>

#include "utils/character_valid.h"
#include "utils/namespace.h"
#include "utils/utils.h"

#include <parserutils/charset/utf8.h>

/* DOM's element local-name rules also accept names produced by the HTML
 * parser. Non-ASCII UTF-8 bytes cannot coincide with the excluded ASCII bytes.
 * https://dom.spec.whatwg.org/#valid-element-local-name */
bool _dom_validate_element_local_name(dom_string *name)
{
    size_t length = dom_string_byte_length(name);
    const uint8_t *text = (const uint8_t *)dom_string_data(name);
    if (length == 0)
        return false;
    bool alpha = (text[0] >= 'a' && text[0] <= 'z') || (text[0] >= 'A' && text[0] <= 'Z');
    if (!alpha && text[0] != ':' && text[0] != '_' && text[0] < 0x80)
        return false;
    for (size_t i = 1; i < length; i++) {
        uint8_t c = text[i];
        if (alpha) {
            if (c == 0 || c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ' || c == '/' || c == '>')
                return false;
        } else if (c < 0x80 &&
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                c == ':' || c == '_')) {
            return false;
        }
    }
    return true;
}

/* An combination of various tests */
static bool is_first_char(uint32_t ch);
static bool is_name_char(uint32_t ch);

/* Test whether the character can be the first character of
 * a NCName. */
static bool is_first_char(uint32_t ch)
{
    /* Refer http://www.w3.org/TR/REC-xml/ for detail */
    if (((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')) || (ch == '_') || (ch == ':') ||
        ((ch >= 0xC0) && (ch <= 0xD6)) || ((ch >= 0xD8) && (ch <= 0xF6)) || ((ch >= 0xF8) && (ch <= 0x2FF)) ||
        ((ch >= 0x370) && (ch <= 0x37D)) || ((ch >= 0x37F) && (ch <= 0x1FFF)) || ((ch >= 0x200C) && (ch <= 0x200D)) ||
        ((ch >= 0x2070) && (ch <= 0x218F)) || ((ch >= 0x2C00) && (ch <= 0x2FEF)) ||
        ((ch >= 0x3001) && (ch <= 0xD7FF)) || ((ch >= 0xF900) && (ch <= 0xFDCF)) ||
        ((ch >= 0xFDF0) && (ch <= 0xFFFD)) || ((ch >= 0x10000) && (ch <= 0xEFFFF)))
        return true;

    if (is_letter(ch) || ch == (uint32_t)'_' || ch == (uint32_t)':') {
        return true;
    }

    return false;
}

/* Test whether the character can be a part of a NCName */
static bool is_name_char(uint32_t ch)
{
    /* Refer http://www.w3.org/TR/REC-xml/ for detail */
    if (((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')) || ((ch >= '0') && (ch <= '9')) || /* !start */
        (ch == '_') || (ch == ':') || (ch == '-') || (ch == '.') || (ch == 0xB7) || /* !start */
        ((ch >= 0xC0) && (ch <= 0xD6)) || ((ch >= 0xD8) && (ch <= 0xF6)) || ((ch >= 0xF8) && (ch <= 0x2FF)) ||
        ((ch >= 0x300) && (ch <= 0x36F)) || /* !start */
        ((ch >= 0x370) && (ch <= 0x37D)) || ((ch >= 0x37F) && (ch <= 0x1FFF)) || ((ch >= 0x200C) && (ch <= 0x200D)) ||
        ((ch >= 0x203F) && (ch <= 0x2040)) || /* !start */
        ((ch >= 0x2070) && (ch <= 0x218F)) || ((ch >= 0x2C00) && (ch <= 0x2FEF)) ||
        ((ch >= 0x3001) && (ch <= 0xD7FF)) || ((ch >= 0xF900) && (ch <= 0xFDCF)) ||
        ((ch >= 0xFDF0) && (ch <= 0xFFFD)) || ((ch >= 0x10000) && (ch <= 0xEFFFF)))
        return true;

    if (is_letter(ch) == true)
        return true;
    if (is_digit(ch) == true)
        return true;
    if (is_combining_char(ch) == true)
        return true;
    if (is_extender(ch) == true)
        return true;

    return false;
}

/**
 * Test whether the name is a valid one according XML 1.0 standard.
 * For the standard please refer:
 *
 * http://www.w3.org/TR/2004/REC-xml-20040204/
 *
 * \param name  The name need to be tested
 * \return true if ::name is valid, false otherwise.
 */
bool _dom_validate_name(dom_string *name)
{
    uint32_t ch;
    size_t clen, slen;
    parserutils_error err;
    const uint8_t *s;

    if (name == NULL)
        return false;

    slen = dom_string_length(name);
    if (slen == 0)
        return false;

    s = (const uint8_t *)dom_string_data(name);
    slen = dom_string_byte_length(name);

    err = parserutils_charset_utf8_to_ucs4(s, slen, &ch, &clen);
    if (err != PARSERUTILS_OK) {
        return false;
    }

    if (is_first_char(ch) == false)
        return false;

    s += clen;
    slen -= clen;

    while (slen > 0) {
        err = parserutils_charset_utf8_to_ucs4(s, slen, &ch, &clen);
        if (err != PARSERUTILS_OK) {
            return false;
        }

        if (is_name_char(ch) == false)
            return false;

        s += clen;
        slen -= clen;
    }

    return true;
}

/**
 * Validate whether the string is a legal NCName.
 * Refer http://www.w3.org/TR/REC-xml-names/ for detail.
 *
 * \param str  The name to validate
 * \return true if ::name is valid, false otherwise.
 */
bool _dom_validate_ncname(dom_string *name)
{
    uint32_t ch;
    size_t clen, slen;
    parserutils_error err;
    const uint8_t *s;

    if (name == NULL)
        return false;

    slen = dom_string_length(name);
    if (slen == 0)
        return false;

    s = (const uint8_t *)dom_string_data(name);
    slen = dom_string_byte_length(name);

    err = parserutils_charset_utf8_to_ucs4(s, slen, &ch, &clen);
    if (err != PARSERUTILS_OK) {
        return false;
    }

    if (is_letter(ch) == false && ch != (uint32_t)'_')
        return false;

    s += clen;
    slen -= clen;

    while (slen > 0) {
        err = parserutils_charset_utf8_to_ucs4(s, slen, &ch, &clen);
        if (err != PARSERUTILS_OK) {
            return false;
        }

        if (is_name_char(ch) == false)
            return false;

        if (ch == (uint32_t)':')
            return false;

        s += clen;
        slen -= clen;
    }

    return true;
}
