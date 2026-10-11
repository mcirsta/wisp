/*
 * Copyright 2017 Vincent Sanders <vince@netsurf-browser.org>
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
 *
 * heap fault injection generation.
 *
 * This library inject allocation faults into NetSurf tests
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "test/malloc_fig.h"

static unsigned int count = UINT_MAX;
static unsigned int realloc_count = UINT_MAX;
static unsigned int calloc_count = UINT_MAX;

void calloc_limit(unsigned int newcount)
{
    calloc_count = newcount;
}

void *calloc(size_t nmemb, size_t size)
{
    static void *(*real_calloc)(size_t, size_t) = NULL;
    if (real_calloc == NULL)
        real_calloc = dlsym(RTLD_NEXT, "calloc");
    if (calloc_count == 0) {
        errno = ENOMEM;
        return NULL;
    }
    if (calloc_count != UINT_MAX)
        calloc_count--;
    return real_calloc(nmemb, size);
}

void realloc_limit(unsigned int newcount)
{
    realloc_count = newcount;
}

void *realloc(void *ptr, size_t size)
{
    static void *(*real_realloc)(void *, size_t) = NULL;
    if (real_realloc == NULL)
        real_realloc = dlsym(RTLD_NEXT, "realloc");
    if (realloc_count == 0) {
        errno = ENOMEM;
        return NULL;
    }
    if (realloc_count != UINT_MAX)
        realloc_count--;
    return real_realloc(ptr, size);
}

void malloc_limit(unsigned int newcount)
{
    count = newcount;
    // fprintf(stderr, "malloc_limit %d\n", count);
}

void *malloc(size_t size)
{
    static void *(*real_malloc)(size_t) = NULL;
    void *p = NULL;

    if (real_malloc == NULL) {
        real_malloc = dlsym(RTLD_NEXT, "malloc");
    }

    if (count > 0) {
        p = real_malloc(size);
        count--;
    } else {
        errno = ENOMEM;
    }
    // fprintf(stderr, "malloc(%d) = %p remian:%d\n", size, p, count);
    return p;
}
