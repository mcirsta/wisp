/*
 * Copyright 2020 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf.
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
 * about scheme testament handler interface
 */

#ifndef WISP_CONTENT_FETCHERS_ABOUT_TESTAMENT_H

struct fetch_about_context;
#define WISP_CONTENT_FETCHERS_ABOUT_TESTAMENT_H

/**
 * Handler to generate about scheme testament page.
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
bool fetch_about_testament_handler(struct fetch_about_context *ctx);

// dummy data for compilation to work

#define WT_MODIFICATIONS                                                                                               \
    {                                                                                                                  \
        {                                                                                                              \
            NULL, NULL                                                                                                 \
        }                                                                                                              \
    }
#define WT_BRANCHPATH "unknown"
#define GECOS "user"
#define USERNAME "user"
#define WT_REVID "unknown"
#define WT_COMPILEDATE __DATE__
#define WT_HOSTNAME "localhost"
#define WT_ROOT "unknown"
#define WT_MODIFIED 0
#define WT_NO_GIT 1

#endif
