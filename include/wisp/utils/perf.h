/*
 * Copyright 2024 NeoSurf Contributors
 *
 * This file is part of NeoSurf, http://www.neosurf-browser.org/
 *
 * NeoSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NeoSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Performance tracing macros for measuring page load timing.
 *
 * Enable via CMake: -DNEOSURF_ENABLE_PERF_TRACE=ON
 *
 * Usage:
 *   PERF("Event description");
 *   PERF("Event with data: %d", value);
 *
 * Output format:
 *   PERF[  1234.567] Event description
 */

#ifndef WISP_UTILS_PERF_H
#define WISP_UTILS_PERF_H

#include <nsutils/time.h>
#include <stdio.h>

#ifdef WISP_PERF_TRACE_ENABLED

/**
 * Performance timing macro - outputs timestamped messages to stderr.
 *
 * \param fmt  printf-style format string
 * \param ...  format arguments
 */
#define PERF(fmt, ...)                                                                                                 \
    do {                                                                                                               \
        uint64_t _perf_ms;                                                                                             \
        nsu_getmonotonic_ms(&_perf_ms);                                                                                \
        fprintf(stderr, "PERF[%6lu.%03lu] " fmt "\n", (unsigned long)(_perf_ms / 1000),                                \
            (unsigned long)(_perf_ms % 1000), ##__VA_ARGS__);                                                          \
        fflush(stderr);                                                                                                \
    } while (0)

#else

/** No-op when performance tracing is disabled */
#define PERF(fmt, ...) ((void)0)

#endif /* NEOSURF_PERF_TRACE_ENABLED */

#endif /* NEOSURF_UTILS_PERF_H */
