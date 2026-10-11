/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>
#include <stdbool.h>
#include <wisp/utils/ascii.h>
#include "utils/encoding.h"
#include "encoding/tables.inc"

const char *wisp_encoding_from_label(const char *label, size_t length)
{
    if (label == NULL)
        return NULL;
    while (length != 0 && memchr(" \t\n\r\f", *label, 5) != NULL) {
        label++;
        length--;
    }
    while (length != 0 && memchr(" \t\n\r\f", label[length - 1], 5) != NULL)
        length--;
    if (length == 0 || length > ENCODING_LABEL_MAX)
        return NULL;
    char lower[ENCODING_LABEL_MAX + 1];
    for (size_t i = 0; i < length; i++) {
        unsigned char byte = label[i];
        if (byte == 0 || byte > 0x7f)
            return NULL;
        lower[i] = ascii_to_lower(byte);
    }
    lower[length] = 0;
    size_t low = 0, high = sizeof(encoding_labels) / sizeof(encoding_labels[0]);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int order = strcmp(lower, encoding_labels[mid].label);
        if (order == 0)
            return encoding_labels[mid].name;
        if (order < 0)
            high = mid;
        else
            low = mid + 1;
    }
    return NULL;
}

const char *wisp_encoding_for_output(const char *name)
{
    if (name == NULL || strcmp(name, "replacement") == 0 || strcmp(name, "UTF-16BE") == 0 ||
        strcmp(name, "UTF-16LE") == 0)
        return "UTF-8";
    return name;
}

static int encode_mac_cyrillic(uint32_t codepoint)
{
    if (codepoint < 0x80)
        return codepoint;
    size_t low = 0, high = sizeof(mac_cyrillic) / sizeof(mac_cyrillic[0]);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (codepoint == mac_cyrillic[mid].codepoint)
            return mac_cyrillic[mid].byte;
        if (codepoint < mac_cyrillic[mid].codepoint)
            high = mid;
        else
            low = mid + 1;
    }
    return -1;
}

static int encode_user_defined(uint32_t codepoint)
{
    if (codepoint < 0x80)
        return codepoint;
    if (codepoint >= 0xf780 && codepoint <= 0xf7ff)
        return codepoint - 0xf780 + 0x80;
    return -1;
}

wisp_single_byte_encoder wisp_encoding_get_single_byte_encoder(const char *name)
{
    if (strcmp(name, "x-mac-cyrillic") == 0)
        return encode_mac_cyrillic;
    if (strcmp(name, "x-user-defined") == 0)
        return encode_user_defined;
    return NULL;
}
