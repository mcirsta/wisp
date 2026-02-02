/*
 * Copyright 2018 Vincent Sanders <vince@netsurf-browser.org>
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
 * Fetch operation implementation for win32
 */

#include <stdlib.h>
#include <string.h>

#include "wisp/content/fetch.h"
#include "wisp/fetch.h"
#include "wisp/utils/file.h"
#include "wisp/utils/log.h"

#include "windows/fetch.h"

/**
 * determine the MIME type of a local file.
 *
 * \param unix_path The unix style path to the file.
 * \return The mime type of the file.
 */
static const char *fetch_filetype(const char *unix_path)
{
    const char *ext;
    NSLOG(wisp, INFO, "unix path %s", unix_path);

    ext = strrchr(unix_path, '.');
    if (ext == NULL) {
        NSLOG(wisp, INFO, "no extension for %s returning html", unix_path);
        return "text/html";
    }

    ext++; /* skip dot */

    NSLOG(wisp, INFO, "extension %s", ext);

    if (strcasecmp(ext, "css") == 0)
        return "text/css";
    if (strcasecmp(ext, "jpg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, "jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, "gif") == 0)
        return "image/gif";
    if (strcasecmp(ext, "png") == 0)
        return "image/png";
    if (strcasecmp(ext, "jng") == 0)
        return "image/jng";
    if (strcasecmp(ext, "svg") == 0)
        return "image/svg";
    if (strcasecmp(ext, "bmp") == 0)
        return "image/x-ms-bmp";
    if (strcasecmp(ext, "ico") == 0)
        return "image/x-icon";
    if (strcasecmp(ext, "webp") == 0)
        return "image/webp";

    return "text/html";
}


/* exported interface documented in windows/fetch.h */
nserror nsw32_get_resource_data(const char *path, const uint8_t **data_out, size_t *data_len_out)
{
    HRSRC reshandle;
    HGLOBAL datahandle;
    uint8_t *data;
    DWORD data_len;

    reshandle = FindResource(NULL, path, "USER");
    if (reshandle == NULL) {
        return NSERROR_NOT_FOUND;
    }

    data_len = SizeofResource(NULL, reshandle);
    if (data_len == 0) {
        return NSERROR_NOT_FOUND;
    }

    datahandle = LoadResource(NULL, reshandle);
    if (datahandle == NULL) {
        return NSERROR_NOT_FOUND;
    }
    data = LockResource(datahandle);
    if (data == NULL) {
        return NSERROR_NOT_FOUND;
    }

    *data_out = data;
    *data_len_out = data_len;

    return NSERROR_OK;
}


/** win32 fetch operation table */
static struct gui_fetch_table fetch_table = {
    .filetype = fetch_filetype,

    .get_resource_url = NULL,
    .get_resource_data = nsw32_get_resource_data,
};

struct gui_fetch_table *win32_fetch_table = &fetch_table;
