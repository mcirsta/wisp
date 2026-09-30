/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_RESTYLE_PROFILE_H
#define WISP_HTML_RESTYLE_PROFILE_H

/* Aggregate only: no per-node logging or allocation in measured code. */
#ifdef WISP_PROFILE_RESTYLE
#include <time.h>
#include <nsutils/time.h>

static inline uint64_t html_profile_now(void)
{
#ifdef CLOCK_MONOTONIC
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
        return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
#endif
    /* Keep optional diagnostics buildable on the other NetSurf platforms.
     * This fallback has millisecond resolution, despite nanosecond units. */
    uint64_t ms = 0;
    nsu_getmonotonic_ms(&ms);
    return ms * 1000000;
}

/* The renderer's thread only: exclude scheduling delays and worker activity.
 * Zero means unavailable; never substitute wall time for a CPU measurement. */
static inline uint64_t html_profile_cpu_now(void)
{
#ifdef CLOCK_THREAD_CPUTIME_ID
    struct timespec now;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0)
        return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
#endif
    return 0;
}

#define HTML_PROFILE_START(name) const uint64_t name = html_profile_now()
#define HTML_PROFILE_CPU_START(name) const uint64_t name = html_profile_cpu_now()
#define HTML_PROFILE(...)                                                                                              \
    do {                                                                                                               \
        __VA_ARGS__;                                                                                                   \
    } while (0)
#else
#define HTML_PROFILE_START(name) ((void)0)
#define HTML_PROFILE_CPU_START(name) ((void)0)
#define HTML_PROFILE(...) ((void)0)
#endif

#endif
