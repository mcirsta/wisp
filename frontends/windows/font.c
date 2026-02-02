/*
 * Copyright 2009 - 2014 Vincent Sanders <vince@netsurf-browser.org>
 * Copyright 2009 - 2013 Michael Drake <tlsa@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Windows font handling and character encoding implementation.
 */

#include "wisp/utils/config.h"
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <windows.h>

#include <libwapcaplet/libwapcaplet.h>

#include <wisp/ns_inttypes.h>
#include "wisp/layout.h"
#include "wisp/plot_style.h"
#include "wisp/utf8.h"
#include "wisp/utils/errors.h"
#include "wisp/utils/log.h"
#include "wisp/utils/nsoption.h"
#include "wisp/utils/utf8.h"

#include "utils/hashmap.h"
#include "windows/font.h"

#ifdef WISP_WOFF_DECODE
#include <zlib.h>
#endif

#define SPLIT_CACHE_MAX_ENTRIES 16384
#define WSTR_CACHE_MAX_BYTES (16 * 1024 * 1024)
#define FONT_CACHE_MAX_ENTRIES 256
#define FONT_MAP_MAX_ENTRIES 256

HWND font_hwnd;

/* Cached memory DC for text measurement */
static HDC s_text_hdc = NULL;

static HDC get_text_hdc(void)
{
    if (s_text_hdc == NULL) {
        s_text_hdc = CreateCompatibleDC(NULL);
        SetMapMode(s_text_hdc, MM_TEXT);
    }
    return s_text_hdc;
}

/*
 * CSS font name -> internal font name mapping
 * When a web font is loaded via @font-face, the CSS family name may differ
 * from the font's internal name. This hashmap stores the mapping.
 */
typedef struct {
    char *css_name; /* CSS family name (key) */
} font_map_key_t;

typedef struct {
    char *internal_name; /* Font's internal family name */
} font_map_value_t;

static void *fm_key_clone(void *key)
{
    font_map_key_t *src = (font_map_key_t *)key;
    font_map_key_t *k = malloc(sizeof(font_map_key_t));
    if (k == NULL)
        return NULL;
    k->css_name = src->css_name ? strdup(src->css_name) : NULL;
    if (src->css_name != NULL && k->css_name == NULL) {
        free(k);
        return NULL;
    }
    return k;
}

static void fm_key_destroy(void *key)
{
    font_map_key_t *k = (font_map_key_t *)key;
    if (k->css_name)
        free(k->css_name);
    free(k);
}

static uint32_t fm_key_hash(void *key)
{
    font_map_key_t *k = (font_map_key_t *)key;
    uint32_t h = 2166136261u;
    if (k->css_name != NULL) {
        const char *p = k->css_name;
        while (*p) {
            h = (h ^ (uint32_t)tolower((unsigned char)*p)) * 16777619u;
            p++;
        }
    }
    return h;
}

static bool fm_key_eq(void *a, void *b)
{
    font_map_key_t *ka = (font_map_key_t *)a;
    font_map_key_t *kb = (font_map_key_t *)b;
    if (ka->css_name == NULL && kb->css_name == NULL)
        return true;
    if (ka->css_name == NULL || kb->css_name == NULL)
        return false;
    return strcasecmp(ka->css_name, kb->css_name) == 0;
}

static void *fm_value_alloc(void *key)
{
    return calloc(1, sizeof(font_map_value_t));
}

static void fm_value_destroy(void *value)
{
    font_map_value_t *v = (font_map_value_t *)value;
    if (v) {
        if (v->internal_name)
            free(v->internal_name);
        free(v);
    }
}

static hashmap_parameters_t font_map_params = {
    fm_key_clone, fm_key_hash, fm_key_eq, fm_key_destroy, fm_value_alloc, fm_value_destroy};

static hashmap_t *font_css_map = NULL;

/**
 * Look up the internal font name for a CSS family name.
 * Returns the internal name if found, NULL otherwise.
 */
static const char *font_map_lookup(const char *css_name)
{
    if (font_css_map == NULL || css_name == NULL)
        return NULL;
    font_map_key_t key = {.css_name = (char *)css_name};
    void *slot = hashmap_lookup(font_css_map, &key);
    if (slot != NULL) {
        font_map_value_t *v = (font_map_value_t *)slot;
        return v->internal_name;
    }
    return NULL;
}

/**
 * Store a CSS name -> internal name mapping.
 */
static void font_map_insert(const char *css_name, const char *internal_name)
{
    if (css_name == NULL || internal_name == NULL)
        return;
    if (font_css_map == NULL) {
        font_css_map = hashmap_create(&font_map_params);
        if (font_css_map == NULL)
            return;
    }
    font_map_key_t key = {.css_name = (char *)css_name};
    void *slot = hashmap_insert(font_css_map, &key);
    if (slot != NULL) {
        font_map_value_t *v = (font_map_value_t *)slot;
        if (v->internal_name)
            free(v->internal_name);
        v->internal_name = strdup(internal_name);
    }
}

#ifdef WISP_WOFF_DECODE
/**
 * WOFF (Web Open Font Format) header structure.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t signature; /* 'wOFF' = 0x774F4646 */
    uint32_t flavor; /* Original font signature (e.g., 0x00010000 for TrueType) */
    uint32_t length; /* Total size of WOFF file */
    uint16_t numTables; /* Number of font tables */
    uint16_t reserved;
    uint32_t totalSfntSize; /* Total size of uncompressed font */
    uint16_t majorVersion;
    uint16_t minorVersion;
    uint32_t metaOffset; /* Offset to metadata block (0 if none) */
    uint32_t metaLength; /* Compressed metadata length */
    uint32_t metaOrigLength; /* Uncompressed metadata length */
    uint32_t privOffset; /* Offset to private data (0 if none) */
    uint32_t privLength; /* Private data length */
} woff_header_t;

typedef struct {
    uint32_t tag; /* Table identifier (same as in font) */
    uint32_t offset; /* Offset to compressed data in WOFF */
    uint32_t compLength; /* Compressed table length */
    uint32_t origLength; /* Uncompressed table length */
    uint32_t origChecksum; /* Original table checksum */
} woff_table_entry_t;
#pragma pack(pop)

#define WOFF_SIGNATURE 0x774F4646 /* 'wOFF' in big-endian */

/**
 * Check if data is a WOFF font.
 */
static bool is_woff_font(const uint8_t *data, size_t size)
{
    if (size < 4)
        return false;
    /* WOFF signature in big-endian: 'w' 'O' 'F' 'F' */
    return (data[0] == 'w' && data[1] == 'O' && data[2] == 'F' && data[3] == 'F');
}

/**
 * Read big-endian uint32.
 */
static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/**
 * Read big-endian uint16.
 */
static uint16_t read_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

/**
 * Write big-endian uint32.
 */
static void write_be32(uint8_t *p, uint32_t val)
{
    p[0] = (val >> 24) & 0xFF;
    p[1] = (val >> 16) & 0xFF;
    p[2] = (val >> 8) & 0xFF;
    p[3] = val & 0xFF;
}

/**
 * Write big-endian uint16.
 */
static void write_be16(uint8_t *p, uint16_t val)
{
    p[0] = (val >> 8) & 0xFF;
    p[1] = val & 0xFF;
}

/**
 * Decode WOFF font to raw TrueType/OpenType format.
 *
 * Returns allocated buffer with decoded font, or NULL on error.
 * Caller must free the returned buffer.
 */
static uint8_t *woff_decode(const uint8_t *woff_data, size_t woff_size, size_t *out_size)
{
    if (woff_size < sizeof(woff_header_t)) {
        NSLOG(wisp, WARNING, "WOFF data too small for header");
        return NULL;
    }

    /* Parse header (all fields are big-endian) */
    const uint8_t *p = woff_data;
    uint32_t signature = read_be32(p);
    if (signature != WOFF_SIGNATURE) {
        NSLOG(wisp, WARNING, "Invalid WOFF signature: 0x%08x", signature);
        return NULL;
    }

    uint32_t flavor = read_be32(p + 4);
    uint16_t numTables = read_be16(p + 12);
    uint32_t totalSfntSize = read_be32(p + 16);

    if (totalSfntSize > 50 * 1024 * 1024) {
        NSLOG(wisp, WARNING, "WOFF output size too large: %u", totalSfntSize);
        return NULL;
    }

    /* Allocate output buffer */
    uint8_t *sfnt = malloc(totalSfntSize);
    if (!sfnt) {
        NSLOG(wisp, WARNING, "Failed to allocate %u bytes for SFNT", totalSfntSize);
        return NULL;
    }
    memset(sfnt, 0, totalSfntSize);

    /* Calculate search parameters for offset table */
    uint16_t entrySelector = 0;
    uint16_t searchRange = 1;
    while (searchRange * 2 <= numTables) {
        searchRange *= 2;
        entrySelector++;
    }
    searchRange *= 16;
    uint16_t rangeShift = numTables * 16 - searchRange;

    /* Write offset table (12 bytes) */
    write_be32(sfnt + 0, flavor);
    write_be16(sfnt + 4, numTables);
    write_be16(sfnt + 6, searchRange);
    write_be16(sfnt + 8, entrySelector);
    write_be16(sfnt + 10, rangeShift);

    /* Table directory starts at offset 12 */
    size_t table_dir_offset = 12;
    /* Tables start after the table directory */
    size_t tables_offset = 12 + numTables * 16;
    size_t current_table_offset = tables_offset;

    /* Process each table */
    p = woff_data + 44; /* Skip WOFF header (44 bytes) */
    for (uint16_t i = 0; i < numTables; i++) {
        if ((size_t)(p - woff_data + 20) > woff_size) {
            NSLOG(wisp, WARNING, "WOFF table entry %u exceeds data", i);
            free(sfnt);
            return NULL;
        }

        uint32_t tag = read_be32(p);
        uint32_t offset = read_be32(p + 4);
        uint32_t compLength = read_be32(p + 8);
        uint32_t origLength = read_be32(p + 12);
        uint32_t origChecksum = read_be32(p + 16);
        p += 20;

        if (offset + compLength > woff_size) {
            NSLOG(wisp, WARNING, "WOFF table %u data exceeds file", i);
            free(sfnt);
            return NULL;
        }

        if (current_table_offset + origLength > totalSfntSize) {
            NSLOG(wisp, WARNING, "WOFF table %u exceeds output size", i);
            free(sfnt);
            return NULL;
        }

        /* Write table directory entry (16 bytes) */
        uint8_t *dir_entry = sfnt + table_dir_offset + i * 16;
        write_be32(dir_entry + 0, tag);
        write_be32(dir_entry + 4, origChecksum);
        write_be32(dir_entry + 8, (uint32_t)current_table_offset);
        write_be32(dir_entry + 12, origLength);

        /* Decompress or copy table data */
        const uint8_t *table_src = woff_data + offset;
        uint8_t *table_dst = sfnt + current_table_offset;

        if (compLength < origLength) {
            /* Compressed - decompress with zlib */
            uLongf destLen = origLength;
            int ret = uncompress(table_dst, &destLen, table_src, compLength);
            if (ret != Z_OK) {
                NSLOG(wisp, WARNING, "WOFF table %u decompression failed: %d", i, ret);
                free(sfnt);
                return NULL;
            }
            if (destLen != origLength) {
                NSLOG(wisp, WARNING, "WOFF table %u size mismatch: expected %u, got %lu", i, origLength,
                    (unsigned long)destLen);
                free(sfnt);
                return NULL;
            }
        } else {
            /* Not compressed - just copy */
            memcpy(table_dst, table_src, origLength);
        }

        /* Align next table to 4-byte boundary */
        current_table_offset += origLength;
        current_table_offset = (current_table_offset + 3) & ~3;
    }

    NSLOG(wisp, INFO, "WOFF decoded: %zu bytes -> %u bytes", woff_size, totalSfntSize);
    *out_size = totalSfntSize;
    return sfnt;
}
#endif /* NEOSURF_WOFF_DECODE */

/**
 * Extract font family name from TrueType/OpenType font data.
 * Parses the 'name' table to find name ID 1 (Font Family).
 *
 * Returns allocated string with family name, or NULL on error.
 * Caller must free the returned string.
 */
static char *ttf_get_family_name(const uint8_t *data, size_t size)
{
    /* TrueType/OpenType font structure:
     * - Offset table at start
     * - Table directory follows
     * - 'name' table contains font names
     */
    if (data == NULL || size < 12)
        return NULL;

    /* Read offset table */
    uint32_t sfnt_version = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];

    /* Check for TrueType (0x00010000) or OpenType CFF ('OTTO') */
    if (sfnt_version != 0x00010000 && sfnt_version != 0x4F54544F) {
        /* Could be TTC (font collection) - check for 'ttcf' */
        if (sfnt_version == 0x74746366) {
            /* TTC file - read first font offset */
            if (size < 16)
                return NULL;
            uint32_t num_fonts = ((uint32_t)data[8] << 24) | ((uint32_t)data[9] << 16) | ((uint32_t)data[10] << 8) |
                data[11];
            if (num_fonts == 0 || size < 16)
                return NULL;
            uint32_t first_offset = ((uint32_t)data[12] << 24) | ((uint32_t)data[13] << 16) |
                ((uint32_t)data[14] << 8) | data[15];
            if (first_offset >= size)
                return NULL;
            /* Recurse with first font */
            return ttf_get_family_name(data + first_offset, size - first_offset);
        }
        return NULL;
    }

    uint16_t num_tables = ((uint16_t)data[4] << 8) | data[5];
    if (size < 12 + (size_t)num_tables * 16)
        return NULL;

    /* Find 'name' table */
    uint32_t name_offset = 0, name_length = 0;
    const uint8_t *table_dir = data + 12;
    for (uint16_t i = 0; i < num_tables; i++) {
        const uint8_t *entry = table_dir + i * 16;
        uint32_t tag = ((uint32_t)entry[0] << 24) | ((uint32_t)entry[1] << 16) | ((uint32_t)entry[2] << 8) | entry[3];
        if (tag == 0x6E616D65) { /* 'name' */
            name_offset = ((uint32_t)entry[8] << 24) | ((uint32_t)entry[9] << 16) | ((uint32_t)entry[10] << 8) |
                entry[11];
            name_length = ((uint32_t)entry[12] << 24) | ((uint32_t)entry[13] << 16) | ((uint32_t)entry[14] << 8) |
                entry[15];
            break;
        }
    }

    if (name_offset == 0 || name_offset + name_length > size || name_length < 6) {
        return NULL;
    }

    /* Parse name table */
    const uint8_t *name_table = data + name_offset;
    uint16_t name_count = ((uint16_t)name_table[2] << 8) | name_table[3];
    uint16_t string_offset = ((uint16_t)name_table[4] << 8) | name_table[5];

    if (6 + (size_t)name_count * 12 > name_length)
        return NULL;

    /* Look for name ID 16 (Typographic Family name) first, then ID 1 (Font Family).
     * Name ID 16 is preferred because it provides the base family name (e.g., "Literata")
     * while Name ID 1 may include the subfamily (e.g., "Literata Light").
     * Using the base family name allows Windows CreateFont to properly match
     * font weights via its weight parameter.
     */
    const uint8_t *name_records = name_table + 6;
    char *family_name = NULL;
    char *typographic_family = NULL; /* Name ID 16 - preferred */
    char *font_family = NULL; /* Name ID 1 - fallback */

    for (uint16_t i = 0; i < name_count; i++) {
        const uint8_t *record = name_records + i * 12;
        uint16_t platform_id = ((uint16_t)record[0] << 8) | record[1];
        uint16_t encoding_id = ((uint16_t)record[2] << 8) | record[3];
        uint16_t name_id = ((uint16_t)record[6] << 8) | record[7];
        uint16_t str_length = ((uint16_t)record[8] << 8) | record[9];
        uint16_t str_offset = ((uint16_t)record[10] << 8) | record[11];

        /* We want name ID 16 (Typographic Family) or ID 1 (Font Family) */
        if (name_id != 16 && name_id != 1)
            continue;

        /* Skip if we already have this one */
        if (name_id == 16 && typographic_family != NULL)
            continue;
        if (name_id == 1 && font_family != NULL)
            continue;

        uint32_t abs_offset = name_offset + string_offset + str_offset;
        if (abs_offset + str_length > size)
            continue;

        const uint8_t *str_data = data + abs_offset;
        char *extracted_name = NULL;

        /* Platform 3 (Windows), Encoding 1 (Unicode BMP) - UTF-16BE */
        if (platform_id == 3 && encoding_id == 1) {
            int char_count = str_length / 2;
            extracted_name = malloc(char_count + 1);
            if (extracted_name) {
                for (int j = 0; j < char_count; j++) {
                    uint16_t ch = ((uint16_t)str_data[j * 2] << 8) | str_data[j * 2 + 1];
                    extracted_name[j] = (ch < 128) ? (char)ch : '?';
                }
                extracted_name[char_count] = '\0';
            }
        }
        /* Platform 1 (Macintosh), Encoding 0 (Roman) - ASCII compatible */
        else if (platform_id == 1 && encoding_id == 0) {
            extracted_name = malloc(str_length + 1);
            if (extracted_name) {
                memcpy(extracted_name, str_data, str_length);
                extracted_name[str_length] = '\0';
            }
        }

        if (extracted_name != NULL) {
            if (name_id == 16) {
                typographic_family = extracted_name;
            } else {
                font_family = extracted_name;
            }
        }

        /* Stop early if we have the preferred name */
        if (typographic_family != NULL && font_family != NULL)
            break;
    }

    /* Prefer typographic family (ID 16) over font family (ID 1) */
    if (typographic_family != NULL) {
        family_name = typographic_family;
        if (font_family != NULL)
            free(font_family);
    } else {
        family_name = font_family;
    }

    return family_name;
}


/* Font cache keyed by family/size/weight/flags */
typedef struct {
    int family;
    int size;
    int weight;
    int flags;
    char *face;
} font_key_t;

static void *fc_key_clone(void *key)
{
    font_key_t *src = (font_key_t *)key;
    font_key_t *k = malloc(sizeof(font_key_t));
    if (k == NULL) {
        return NULL;
    }

    /* Initialize all fields first */
    k->family = src->family;
    k->size = src->size;
    k->weight = src->weight;
    k->flags = src->flags;
    k->face = NULL;

    /* Then duplicate the face string if needed */
    if (src->face != NULL) {
        k->face = strdup(src->face);
        if (k->face == NULL) {
            free(k);
            return NULL;
        }
    }
    return k;
}

static void fc_key_destroy(void *key)
{
    font_key_t *k = (font_key_t *)key;
    if (k->face != NULL) {
        free(k->face);
    }
    free(key);
}

static uint32_t fc_key_hash(void *key)
{
    font_key_t *k = (font_key_t *)key;
    uint32_t h = 2166136261u;
    h = (h ^ (uint32_t)k->family) * 16777619u;
    h = (h ^ (uint32_t)k->size) * 16777619u;
    h = (h ^ (uint32_t)k->weight) * 16777619u;
    h = (h ^ (uint32_t)k->flags) * 16777619u;
    if (k->face != NULL) {
        /* Limit face name length to prevent excessive hashing */
        const char *p = k->face;
        int max_len = 256; /* Reasonable limit for font face names */
        int i = 0;
        while (*p != '\0' && i < max_len) {
            h = (h ^ (uint32_t)tolower((unsigned char)*p)) * 16777619u;
            p++;
            i++;
        }
    }
    return h;
}

static bool fc_key_eq(void *a, void *b)
{
    font_key_t *ka = (font_key_t *)a;
    font_key_t *kb = (font_key_t *)b;

    if (ka->family != kb->family || ka->size != kb->size || ka->weight != kb->weight || ka->flags != kb->flags) {
        return false;
    }

    /* Handle NULL face pointers consistently */
    if (ka->face == NULL && kb->face == NULL) {
        return true;
    }
    if (ka->face == NULL || kb->face == NULL) {
        return false;
    }

    /* Use same case-insensitive comparison as hash function */
    return strcasecmp(ka->face, kb->face) == 0;
}

typedef struct {
    HFONT font;
    uint64_t gen;
} font_value_t;

static void *fc_value_alloc(void *key)
{
    font_value_t *fv = malloc(sizeof(font_value_t));
    if (fv != NULL) {
        fv->font = NULL;
        fv->gen = 0;
    }
    return fv;
}

static void fc_value_destroy(void *value)
{
    if (value != NULL) {
        font_value_t *fv = (font_value_t *)value;
        if (fv->font != NULL) {
            DeleteObject(fv->font);
        }
        free(value);
    }
}

static hashmap_parameters_t font_cache_params = {
    fc_key_clone, fc_key_hash, fc_key_eq, fc_key_destroy, fc_value_alloc, fc_value_destroy};

static hashmap_t *font_cache = NULL;
static uint64_t font_gen = 0;

typedef struct {
    int family;
    int size;
    int weight;
    int flags;
    int x;
    size_t len;
    char *str;
} split_key_t;

typedef struct {
    size_t offset;
    int actual_x;
    uint64_t gen;
} split_value_t;

static uint64_t split_gen = 0;

static void *sc_key_clone(void *key)
{
    split_key_t *src = (split_key_t *)key;
    split_key_t *k = malloc(sizeof(split_key_t));
    if (k == NULL) {
        return NULL;
    }
    *k = *src;
    if (src->len > 0) {
        k->str = malloc(src->len);
        if (k->str == NULL) {
            free(k);
            return NULL;
        }
        memcpy(k->str, src->str, src->len);
    } else {
        k->str = NULL;
    }
    return k;
}

static void sc_key_destroy(void *key)
{
    split_key_t *k = (split_key_t *)key;
    if (k->str != NULL) {
        free(k->str);
    }
    free(k);
}

static uint32_t sc_key_hash(void *key)
{
    split_key_t *k = (split_key_t *)key;
    uint32_t h = 2166136261u;
    h = (h ^ (uint32_t)k->family) * 16777619u;
    h = (h ^ (uint32_t)k->size) * 16777619u;
    h = (h ^ (uint32_t)k->weight) * 16777619u;
    h = (h ^ (uint32_t)k->flags) * 16777619u;
    h = (h ^ (uint32_t)k->x) * 16777619u;
    h = (h ^ (uint32_t)k->len) * 16777619u;
    if (k->str != NULL && k->len > 0) {
        for (size_t i = 0; i < k->len; i++) {
            h = (h ^ (uint32_t)(unsigned char)k->str[i]) * 16777619u;
        }
    }
    return h;
}

static bool sc_key_eq(void *a, void *b)
{
    split_key_t *ka = (split_key_t *)a;
    split_key_t *kb = (split_key_t *)b;
    if (ka->family != kb->family || ka->size != kb->size || ka->weight != kb->weight || ka->flags != kb->flags ||
        ka->x != kb->x || ka->len != kb->len) {
        return false;
    }
    if (ka->len == 0) {
        return true;
    }
    if (ka->str == NULL || kb->str == NULL) {
        return ka->str == kb->str;
    }
    return memcmp(ka->str, kb->str, ka->len) == 0;
}

static void *sc_value_alloc(void *key)
{
    return malloc(sizeof(split_value_t));
}

static void sc_value_destroy(void *value)
{
    if (value != NULL) {
        free(value);
    }
}

static hashmap_parameters_t split_cache_params = {
    sc_key_clone, sc_key_hash, sc_key_eq, sc_key_destroy, sc_value_alloc, sc_value_destroy};

static hashmap_t *split_cache = NULL;

typedef struct {
    size_t len;
    char *str;
} wstr_key_t;

typedef struct {
    WCHAR *wstr;
    int wclen;
    size_t bytes;
    uint64_t gen;
} wstr_value_t;

static uint64_t wstr_gen = 0;
static size_t wstr_total_bytes = 0;
static hashmap_t *wstr_cache = NULL;

static bool wstr_iter_oldest_cb(void *key, void *value, void *ctx)
{
    wstr_value_t *v = (wstr_value_t *)value;
    void **out = (void **)ctx;
    if (out[0] == NULL || v->gen < ((wstr_value_t *)out[1])->gen) {
        out[0] = key;
        out[1] = value;
    }
    return false;
}

static void wstr_cache_evict_if_needed(void)
{
    while (wstr_total_bytes > WSTR_CACHE_MAX_BYTES && wstr_cache != NULL) {
        void *ctx[2] = {NULL, NULL};
        hashmap_iterate(wstr_cache, wstr_iter_oldest_cb, ctx);
        if (ctx[0] == NULL || ctx[1] == NULL)
            break;
        wstr_value_t *ov = (wstr_value_t *)ctx[1];
        size_t obytes = ov->bytes;
        hashmap_remove(wstr_cache, ctx[0]);
        if (wstr_total_bytes >= obytes) {
            wstr_total_bytes -= obytes;
        } else {
            wstr_total_bytes = 0;
        }
    }
}

static bool split_iter_oldest_cb(void *key, void *value, void *ctx)
{
    split_value_t *v = (split_value_t *)value;
    void **out = (void **)ctx;
    if (out[0] == NULL || v->gen < ((split_value_t *)out[1])->gen) {
        out[0] = key;
        out[1] = value;
    }
    return false;
}

static void split_cache_evict_if_needed(void)
{
    if (split_cache == NULL)
        return;
    size_t count = hashmap_count(split_cache);
    if (count <= SPLIT_CACHE_MAX_ENTRIES)
        return;
    size_t target = (SPLIT_CACHE_MAX_ENTRIES * 3) / 4;
    size_t to_remove = count - target;
    for (size_t i = 0; i < to_remove; i++) {
        void *ctx[2] = {NULL, NULL};
        hashmap_iterate(split_cache, split_iter_oldest_cb, ctx);
        if (ctx[0] == NULL)
            break;
        hashmap_remove(split_cache, ctx[0]);
    }
}

static bool font_iter_oldest_cb(void *key, void *value, void *ctx)
{
    font_value_t *v = (font_value_t *)value;
    void **out = (void **)ctx;
    if (out[0] == NULL || v->gen < ((font_value_t *)out[1])->gen) {
        out[0] = key;
        out[1] = value;
    }
    return false;
}

static void font_cache_evict_if_needed(void)
{
    if (font_cache == NULL)
        return;
    size_t count = hashmap_count(font_cache);
    if (count <= FONT_CACHE_MAX_ENTRIES)
        return;
    size_t target = (FONT_CACHE_MAX_ENTRIES * 3) / 4;
    size_t to_remove = count - target;
    for (size_t i = 0; i < to_remove; i++) {
        void *ctx[2] = {NULL, NULL};
        hashmap_iterate(font_cache, font_iter_oldest_cb, ctx);
        if (ctx[0] == NULL)
            break;
        hashmap_remove(font_cache, ctx[0]);
    }
}

static void *wc_key_clone(void *key)
{
    wstr_key_t *src = (wstr_key_t *)key;
    wstr_key_t *k = malloc(sizeof(wstr_key_t));
    if (k == NULL) {
        return NULL;
    }
    k->len = src->len;
    if (src->len > 0) {
        k->str = malloc(src->len);
        if (k->str == NULL) {
            free(k);
            return NULL;
        }
        memcpy(k->str, src->str, src->len);
    } else {
        k->str = NULL;
    }
    return k;
}

static void wc_key_destroy(void *key)
{
    wstr_key_t *k = (wstr_key_t *)key;
    if (k->str != NULL) {
        free(k->str);
    }
    free(k);
}

static uint32_t wc_key_hash(void *key)
{
    wstr_key_t *k = (wstr_key_t *)key;
    uint32_t h = 2166136261u;
    h = (h ^ (uint32_t)k->len) * 16777619u;
    if (k->str != NULL && k->len > 0) {
        for (size_t i = 0; i < k->len; i++) {
            h = (h ^ (uint32_t)(unsigned char)k->str[i]) * 16777619u;
        }
    }
    return h;
}

static bool wc_key_eq(void *a, void *b)
{
    wstr_key_t *ka = (wstr_key_t *)a;
    wstr_key_t *kb = (wstr_key_t *)b;
    if (ka->len != kb->len) {
        return false;
    }
    if (ka->len == 0) {
        return true;
    }
    if (ka->str == NULL || kb->str == NULL) {
        return ka->str == kb->str;
    }
    return memcmp(ka->str, kb->str, ka->len) == 0;
}

static void *wc_value_alloc(void *key)
{
    return malloc(sizeof(wstr_value_t));
}

static void wc_value_destroy(void *value)
{
    if (value != NULL) {
        wstr_value_t *v = (wstr_value_t *)value;
        if (v->wstr != NULL) {
            free(v->wstr);
        }
        free(value);
    }
}

static hashmap_parameters_t wstr_cache_params = {
    wc_key_clone, wc_key_hash, wc_key_eq, wc_key_destroy, wc_value_alloc, wc_value_destroy};


static nserror get_cached_wide(const char *utf8str, size_t utf8len, const WCHAR **out_wstr, int *out_wclen)
{
    if (utf8len == 0) {
        *out_wstr = NULL;
        *out_wclen = 0;
        return NSERROR_OK;
    }
    if (wstr_cache == NULL) {
        wstr_cache = hashmap_create(&wstr_cache_params);
        if (wstr_cache == NULL) {
            return NSERROR_NOMEM;
        }
    }
    wstr_key_t key;
    key.len = utf8len;
    key.str = (char *)utf8str;
    void *slot = hashmap_lookup(wstr_cache, &key);
    if (slot != NULL) {
        wstr_value_t *val = (wstr_value_t *)slot;
        val->gen = ++wstr_gen;
        *out_wstr = val->wstr;
        *out_wclen = val->wclen;
        return NSERROR_OK;
    }
    int alloc = (int)(utf8len);
    if (alloc <= 0)
        alloc = 1;
    WCHAR *buf = (WCHAR *)malloc(sizeof(WCHAR) * alloc);
    if (buf == NULL) {
        return NSERROR_NOMEM;
    }
    int wclen = MultiByteToWideChar(CP_UTF8, 0, utf8str, (int)utf8len, buf, alloc);
    if (wclen == 0) {
        free(buf);
        return NSERROR_NOSPACE;
    }
    void *islot = hashmap_insert(wstr_cache, &key);
    if (islot == NULL) {
        free(buf);
        return NSERROR_NOMEM;
    }
    wstr_value_t *ival = (wstr_value_t *)islot;
    ival->wstr = buf;
    ival->wclen = wclen;
    ival->bytes = (size_t)wclen * sizeof(WCHAR);
    ival->gen = ++wstr_gen;
    wstr_total_bytes += ival->bytes;
    wstr_cache_evict_if_needed();
    *out_wstr = buf;
    *out_wclen = wclen;
    return NSERROR_OK;
}

static int CALLBACK font_enum_proc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lParam)
{
    int *found = (int *)lParam;
    (void)lf;
    (void)tm;
    (void)type;
    *found = 1;
    return 0;
}

static bool win32_font_family_exists(const char *family_name)
{
    LOGFONTW lf;
    int found = 0;

    if (family_name == NULL || family_name[0] == '\0') {
        return false;
    }

    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    if (MultiByteToWideChar(CP_UTF8, 0, family_name, -1, lf.lfFaceName, LF_FACESIZE) == 0) {
        return false;
    }

    EnumFontFamiliesExW(get_text_hdc(), &lf, font_enum_proc, (LPARAM)&found, 0);
    return found != 0;
}

static const char *select_face_name(const plot_font_style_t *style, DWORD *out_family)
{
    const char *face = NULL;

    if (style->families != NULL) {
        lwc_string *const *families = style->families;
        while (*families != NULL) {
            const char *candidate = lwc_string_data(*families);

            /* First try: check if CSS name exists directly */
            if (win32_font_family_exists(candidate)) {
                if (out_family != NULL) {
                    *out_family = FF_DONTCARE | DEFAULT_PITCH;
                }
                return candidate;
            }

            /* Second try: check if we have a CSS->internal name mapping
             * Note: AddFontMemResourceEx fonts are NOT enumerable via EnumFontFamilies,
             * but they ARE available for CreateFont. Trust the mapping. */
            const char *internal = font_map_lookup(candidate);
            if (internal != NULL) {
                if (out_family != NULL) {
                    *out_family = FF_DONTCARE | DEFAULT_PITCH;
                }
                return internal;
            }

            families++;
        }
    }

    switch (style->family) {
    case PLOT_FONT_FAMILY_SERIF:
        face = nsoption_charp(font_serif);
        if (out_family != NULL) {
            *out_family = FF_ROMAN | DEFAULT_PITCH;
        }
        break;
    case PLOT_FONT_FAMILY_MONOSPACE:
        face = nsoption_charp(font_mono);
        if (out_family != NULL) {
            *out_family = FF_MODERN | DEFAULT_PITCH;
        }
        break;
    case PLOT_FONT_FAMILY_CURSIVE:
        face = nsoption_charp(font_cursive);
        if (out_family != NULL) {
            *out_family = FF_SCRIPT | DEFAULT_PITCH;
        }
        break;
    case PLOT_FONT_FAMILY_FANTASY:
        face = nsoption_charp(font_fantasy);
        if (out_family != NULL) {
            *out_family = FF_DECORATIVE | DEFAULT_PITCH;
        }
        break;
    case PLOT_FONT_FAMILY_SANS_SERIF:
    default:
        face = nsoption_charp(font_sans);
        if (out_family != NULL) {
            *out_family = FF_SWISS | DEFAULT_PITCH;
        }
        break;
    }

    return face;
}

static HFONT get_cached_font(const plot_font_style_t *style)
{
    font_key_t key;
    void *slot;
    HFONT font;
    const char *face;

    face = select_face_name(style, NULL);
    key.family = style->family;
    key.size = style->size;
    key.weight = style->weight;
    key.flags = style->flags;
    key.face = (char *)face; /* Safe: face points to static config strings or NULL */

    if (font_cache == NULL) {
        font_cache = hashmap_create(&font_cache_params);
        if (font_cache == NULL) {
            return NULL;
        }
    }

    slot = hashmap_lookup(font_cache, &key);
    if (slot != NULL) {
        font_value_t *fv = (font_value_t *)slot;
        font = fv->font;
        if (font != NULL) {
            fv->gen = ++font_gen;
            return font;
        }
    }

    font = get_font(style);
    if (font != NULL) {
        slot = hashmap_insert(font_cache, &key);
        if (slot != NULL) {
            font_value_t *fv = (font_value_t *)slot;
            fv->font = font;
            fv->gen = ++font_gen;
            font_cache_evict_if_needed();
        }
    }
    return font;
}

/* exported interface documented in windows/font.h */
nserror utf8_to_font_encoding(const struct font_desc *font, const char *string, size_t len, char **result)
{
    return utf8_to_enc(string, font->encoding, len, result);
}


/**
 * Convert a string to UCS2 from UTF8
 *
 * \param[in] string source string
 * \param[in] len source string length
 * \param[out] result The UCS2 string
 */
static nserror utf8_to_local_encoding(const char *string, size_t len, char **result)
{
    return utf8_to_enc(string, "UCS-2", len, result);
}


/**
 * Convert a string to UTF8 from local encoding
 *
 * \param[in] string source string
 * \param[in] len source string length
 * \param[out] result The UTF8 string
 */
static nserror utf8_from_local_encoding(const char *string, size_t len, char **result)
{
    assert(string && result);

    if (len == 0) {
        len = strlen(string);
    }

    *result = strndup(string, len);
    if (!(*result)) {
        return NSERROR_NOMEM;
    }

    return NSERROR_OK;
}


/* exported interface documented in windows/font.h */
HFONT get_font(const plot_font_style_t *style)
{
    const char *face = NULL;
    DWORD family;
    int nHeight;
    HDC hdc;
    HFONT font;

    face = select_face_name(style, &family);
    if (face == NULL) {
        face = "";
        family = FF_DONTCARE | DEFAULT_PITCH;
    }

    nHeight = -10;

    hdc = GetDC(font_hwnd);
    nHeight = -MulDiv(style->size, GetDeviceCaps(hdc, LOGPIXELSY), 72 * PLOT_STYLE_SCALE);
    ReleaseDC(font_hwnd, hdc);

    NSLOG(wisp, DEEPDEBUG, "CreateFont: face='%s' height=%d weight=%d italic=%s", face ? face : "(null)", nHeight,
        style->weight, (style->flags & FONTF_ITALIC) ? "yes" : "no");

    font = CreateFont(nHeight, 0, 0, 0, style->weight, (style->flags & FONTF_ITALIC) ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, family, face);

    if (font == NULL) {
        if (style->family == PLOT_FONT_FAMILY_MONOSPACE) {
            font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
        } else {
            font = (HFONT)GetStockObject(ANSI_VAR_FONT);
        }
    }

    if (font == NULL) {
        font = (HFONT)GetStockObject(SYSTEM_FONT);
    }

    return font;
}

/* size of temporary wide character string for computing string width */
#define WSTRLEN 4096


/**
 * Measure the width of a string.
 *
 * \param[in] style plot style for this text
 * \param[in] utf8str string encoded in UTF-8 to measure
 * \param[in] utf8len length of string, in bytes
 * \param[out] width updated to width of string[0..length)
 * \return NSERROR_OK on success otherwise appropriate error code
 */
static nserror win32_font_width(const plot_font_style_t *style, const char *utf8str, size_t utf8len, int *width)
{
    nserror ret = NSERROR_OK;
    HDC hdc;
    HFONT font;
    HFONT fontbak;
    SIZE sizl;
    BOOL wres;
    const WCHAR *wstr;
    int wclen;

    if (utf8len == 0) {
        *width = 0;
        return ret;
    }

    hdc = get_text_hdc();
    font = get_cached_font(style);
    fontbak = SelectObject(hdc, font);

    ret = get_cached_wide(utf8str, utf8len, &wstr, &wclen);
    if (ret == NSERROR_OK) {
        wres = GetTextExtentPoint32W(hdc, wstr, wclen, &sizl);
        if (wres == FALSE) {
            ret = NSERROR_INVALID;
        } else {
            *width = sizl.cx;
        }
    }

    SelectObject(hdc, fontbak);

    return ret;
}


/**
 * Find the position in a string where an x coordinate falls.
 *
 * \param  style css_style for this text, with style->font_size.size ==
 *			CSS_FONT_SIZE_LENGTH
 * \param  utf8str string to measure encoded in UTF-8
 * \param  utf8len length of string
 * \param  x coordinate to search for
 * \param  char_offset updated to offset in string of actual_x, [0..length]
 * \param  actual_x updated to x coordinate of character closest to x
 * \return NSERROR_OK on success otherwise appropriate error code
 */
static nserror win32_font_position(
    const plot_font_style_t *style, const char *utf8str, size_t utf8len, int x, size_t *char_offset, int *actual_x)
{
    HDC hdc;
    HFONT font;
    HFONT fontbak;
    SIZE s;
    nserror ret = NSERROR_OK;

    /* deal with zero length input or invalid search co-ordiate */
    if ((utf8len == 0) || (x < 1)) {
        *char_offset = 0;
        *actual_x = 0;
        return ret;
    }

    hdc = get_text_hdc();
    font = get_cached_font(style);
    fontbak = SelectObject(hdc, font);

    const WCHAR *wstr;
    int wclen;
    ret = get_cached_wide(utf8str, utf8len, &wstr, &wclen);
    if (ret == NSERROR_OK) {
        int fit = 0;
        if ((GetTextExtentExPointW(hdc, wstr, wclen, x, &fit, NULL, &s) != 0) &&
            (GetTextExtentPoint32W(hdc, wstr, fit, &s) != 0)) {
            size_t boff = utf8_bounded_byte_length(utf8str, utf8len, (size_t)fit);
            *char_offset = boff;
            *actual_x = s.cx;
        } else {
            ret = NSERROR_UNKNOWN;
        }
    }

    SelectObject(hdc, fontbak);

    return ret;
}


/**
 * Find where to split a string to make it fit a width.
 *
 * \param  style	css_style for this text, with style->font_size.size ==
 *			CSS_FONT_SIZE_LENGTH
 * \param  string	UTF-8 string to measure
 * \param  length	length of string
 * \param  x		width available
 * \param[out] offset updated to offset in string of actual_x, [0..length]
 * \param  actual_x	updated to x coordinate of character closest to x
 * \return NSERROR_OK on success otherwise appropriate error code
 *
 * On exit, [char_offset == 0 ||
 *	   string[char_offset] == ' ' ||
 *	   char_offset == length]
 */
static nserror win32_font_split(
    const plot_font_style_t *style, const char *string, size_t length, int x, size_t *offset, int *actual_x)
{
    nserror res;
    int c_off;

    if (split_cache == NULL) {
        split_cache = hashmap_create(&split_cache_params);
    }
    if (split_cache != NULL) {
        split_key_t key;
        key.family = style->family;
        key.size = style->size;
        key.weight = style->weight;
        key.flags = style->flags;
        key.x = x;
        key.len = length;
        key.str = (char *)string;
        void *slot = hashmap_lookup(split_cache, &key);
        if (slot != NULL) {
            split_value_t *val = (split_value_t *)slot;
            val->gen = ++split_gen;
            *offset = val->offset;
            *actual_x = val->actual_x;
            return NSERROR_OK;
        }
        res = win32_font_position(style, string, length, x, offset, actual_x);
        if (res != NSERROR_OK) {
            return res;
        }
        if (*offset == length) {
            void *islot = hashmap_insert(split_cache, &key);
            if (islot != NULL) {
                split_value_t *ival = (split_value_t *)islot;
                ival->offset = *offset;
                ival->actual_x = *actual_x;
                ival->gen = ++split_gen;
                split_cache_evict_if_needed();
            }
            return NSERROR_OK;
        }
        c_off = *offset;
        while ((string[*offset] != ' ') && (*offset > 0)) {
            (*offset)--;
        }
        if (*offset == 0) {
            *offset = c_off;
            while ((*offset < length) && (string[*offset] != ' ')) {
                (*offset)++;
            }
        }
        res = win32_font_width(style, string, *offset, actual_x);
        if (res == NSERROR_OK) {
            void *islot2 = hashmap_insert(split_cache, &key);
            if (islot2 != NULL) {
                split_value_t *ival2 = (split_value_t *)islot2;
                ival2->offset = *offset;
                ival2->actual_x = *actual_x;
                ival2->gen = ++split_gen;
                split_cache_evict_if_needed();
            }
        }
        return res;
    }

    res = win32_font_position(style, string, length, x, offset, actual_x);
    if (res != NSERROR_OK) {
        return res;
    }
    if (*offset == length) {
        return NSERROR_OK;
    }
    c_off = *offset;
    while ((string[*offset] != ' ') && (*offset > 0)) {
        (*offset)--;
    }
    if (*offset == 0) {
        *offset = c_off;
        while ((*offset < length) && (string[*offset] != ' ')) {
            (*offset)++;
        }
    }
    res = win32_font_width(style, string, *offset, actual_x);
    return res;
}

void win32_font_caches_flush(void)
{
    if (split_cache != NULL) {
        hashmap_destroy(split_cache);
        split_cache = NULL;
    }
    if (wstr_cache != NULL) {
        hashmap_destroy(wstr_cache);
        wstr_cache = NULL;
    }
    wstr_total_bytes = 0;
    wstr_gen = 0;
    split_gen = 0;
}

nserror html_font_face_load_data(const char *family_name, const uint8_t *data, size_t size)
{
    DWORD num_fonts = 0;
    HANDLE font_handle;
    char *internal_name = NULL;
    const uint8_t *font_data = data;
    size_t font_size = size;
#ifdef WISP_WOFF_DECODE
    uint8_t *decoded_font = NULL;
#endif

    /* Validate input parameters */
    if (family_name == NULL || data == NULL || size == 0) {
        return NSERROR_BAD_PARAMETER;
    }

    /* Check for reasonable size limits to prevent memory issues */
    if (size > 50 * 1024 * 1024) { /* 50MB limit for font files */
        NSLOG(wisp, WARNING, "Font '%s' size %zu exceeds reasonable limit", family_name, size);
        return NSERROR_BAD_PARAMETER;
    }

#ifdef WISP_WOFF_DECODE
    /* Check if this is a WOFF font that needs decoding */
    if (is_woff_font(data, size)) {
        NSLOG(wisp, INFO, "Font '%s' is WOFF format, decoding...", family_name);
        decoded_font = woff_decode(data, size, &font_size);
        if (decoded_font == NULL) {
            NSLOG(wisp, WARNING, "Failed to decode WOFF font '%s'", family_name);
            return NSERROR_INVALID;
        }
        font_data = decoded_font;
    }
#endif

    /* Extract the font's internal family name from the font data */
    internal_name = ttf_get_family_name(font_data, font_size);

    font_handle = AddFontMemResourceEx((PVOID)font_data, (DWORD)font_size, NULL, &num_fonts);
    if (font_handle == NULL || num_fonts == 0) {
        NSLOG(wisp, WARNING, "Failed to load font '%s' into Windows (error=%lu)", family_name, GetLastError());
        if (internal_name)
            free(internal_name);
#ifdef WISP_WOFF_DECODE
        if (decoded_font)
            free(decoded_font);
#endif
        return NSERROR_INVALID;
    }

#ifdef WISP_WOFF_DECODE
    /* Free the decoded font data (Windows has copied it internally) */
    if (decoded_font) {
        free(decoded_font);
    }
#endif

    /* Store mapping from CSS name to internal name */
    if (internal_name != NULL) {
        NSLOG(wisp, INFO, "Loaded web font: CSS '%s' -> internal '%s'", family_name, internal_name);
        font_map_insert(family_name, internal_name);
        free(internal_name);
    } else {
        /* No internal name found, use CSS name as-is */
        NSLOG(wisp, INFO, "Loaded web font: '%s' (no internal name found)", family_name);
    }

    win32_font_caches_flush();
    return NSERROR_OK;
}


/** win32 font operations table */
static struct gui_layout_table layout_table = {
    .width = win32_font_width,
    .position = win32_font_position,
    .split = win32_font_split,
    .load_font_data = html_font_face_load_data,
};

struct gui_layout_table *win32_layout_table = &layout_table;

/** win32 utf8 encoding operations table */
static struct gui_utf8_table utf8_table = {
    .utf8_to_local = utf8_to_local_encoding,
    .local_to_utf8 = utf8_from_local_encoding,
};

struct gui_utf8_table *win32_utf8_table = &utf8_table;
