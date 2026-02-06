/*
 * Copyright 2026 Marius Cirsta
 *
 * This file is part of Wisp, http://www.wispbrowser.org/
 *
 * Wisp is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Wisp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * AVIF image content handler interface.
 */

#ifndef NETSURF_IMAGE_AVIF_H
#define NETSURF_IMAGE_AVIF_H

#include <wisp/utils/config.h>
#include <wisp/utils/errors.h>

#ifdef WITH_AVIF

nserror nsavif_init(void);

#else

static inline nserror nsavif_init(void)
{
    return NSERROR_OK;
}

#endif /* WITH_AVIF */

#endif /* NETSURF_IMAGE_AVIF_H */
