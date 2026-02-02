/*
 * Copyright 2008 Vincent Sanders <vince@simtec.co.uk>
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

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "wisp/utils/errors.h"
#include "wisp/utils/log.h"
#include "wisp/utils/sys_time.h"

#include "windows/qpc_safe.h"
#include "windows/schedule.h"

/* linked list of scheduled callbacks, sorted by execution time */
static struct nscallback *schedule_list = NULL;

/**
 * scheduled callback.
 */
struct nscallback {
    struct nscallback *next;
    uint64_t tv; /* Absolute time in microseconds */
    void (*callback)(void *p);
    void *p;
};


/**
 * Get current monotonic time in microseconds
 */
static uint64_t get_monotonic_time_us(void)
{
    uint64_t current_us;
    if (qpc_safe_get_monotonic_us(&current_us)) {
        return current_us;
    }
    /* Fallback to original gettimeofday */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000000) + tv.tv_usec;
}

/**
 * Unschedule a callback.
 *
 * \param  callback  callback function
 * \param  p         user parameter, passed to callback function
 *
 * All scheduled callbacks matching both callback and p are removed.
 */

static nserror schedule_remove(void (*callback)(void *p), void *p)
{
    struct nscallback *cur_nscb;
    struct nscallback *prev_nscb;
    struct nscallback *unlnk_nscb;

    /* check there is something on the list to remove */
    if (schedule_list == NULL) {
        return NSERROR_OK;
    }

    NSLOG(schedule, DEBUG, "removing %p, %p", callback, p);

    cur_nscb = schedule_list;
    prev_nscb = NULL;

    while (cur_nscb != NULL) {
        if ((cur_nscb->callback == callback) && (cur_nscb->p == p)) {
            /* item to remove */

            NSLOG(schedule, DEBUG, "callback entry %p removing  %p(%p)", cur_nscb, cur_nscb->callback, cur_nscb->p);

            /* remove callback */
            unlnk_nscb = cur_nscb;
            cur_nscb = unlnk_nscb->next;

            if (prev_nscb == NULL) {
                schedule_list = cur_nscb;
            } else {
                prev_nscb->next = cur_nscb;
            }
            free(unlnk_nscb);
        } else {
            /* move to next element */
            prev_nscb = cur_nscb;
            cur_nscb = prev_nscb->next;
        }
    }
    return NSERROR_OK;
}

/* exported interface documented in windows/schedule.h */
nserror win32_schedule(int ival, void (*callback)(void *p), void *p)
{
    struct nscallback *nscb;
    struct nscallback *cur_nscb;
    struct nscallback *prev_nscb;
    nserror ret;

    ret = schedule_remove(callback, p);
    if ((ival < 0) || (ret != NSERROR_OK)) {
        return ret;
    }

    nscb = calloc(1, sizeof(struct nscallback));
    if (nscb == NULL) {
        return NSERROR_NOMEM;
    }

    NSLOG(schedule, DEBUG, "adding callback %p for %p(%p) at %d ms", nscb, callback, p, ival);

    /* Store absolute time in microseconds */
    nscb->tv = get_monotonic_time_us() + ((uint64_t)ival * 1000);

    nscb->callback = callback;
    nscb->p = p;

    /* Insert into list maintaining sort order by time */
    if (schedule_list == NULL || nscb->tv < schedule_list->tv) {
        /* Insert at head */
        nscb->next = schedule_list;
        schedule_list = nscb;
    } else {
        /* Find insertion point */
        cur_nscb = schedule_list;
        prev_nscb = NULL;
        while (cur_nscb != NULL && cur_nscb->tv <= nscb->tv) {
            prev_nscb = cur_nscb;
            cur_nscb = cur_nscb->next;
        }
        /* Insert after prev_nscb */
        nscb->next = cur_nscb;
        prev_nscb->next = nscb;
    }

    return NSERROR_OK;
}

/* exported interface documented in schedule.h */
int schedule_run(void)
{
    uint64_t now;
    uint64_t nexttime;
    struct nscallback *cur_nscb;
    struct nscallback *unlnk_nscb;

    if (schedule_list == NULL)
        return -1;

    now = get_monotonic_time_us();

    /* Process all callbacks that are due (head of the sorted list) */
    while (schedule_list != NULL && schedule_list->tv <= now) {
        /* Remove head */
        unlnk_nscb = schedule_list;
        schedule_list = unlnk_nscb->next;

        NSLOG(schedule, DEBUG, "callback entry %p running %p(%p)", unlnk_nscb, unlnk_nscb->callback, unlnk_nscb->p);

        /* Call callback */
        unlnk_nscb->callback(unlnk_nscb->p);

        free(unlnk_nscb);

        /* Reset 'now' in case callback took a long time?
         * For now we stick to the initial capture time to ensure
         * we don't get stuck in a loop processing 0-delay reschedules.
         */
    }

    if (schedule_list == NULL) {
        return -1;
    }

    /* The next event is simply the head of the list */
    nexttime = schedule_list->tv;

    /* make returned time relative to now */
    int64_t diff = nexttime - now;
    if (diff < 0) {
        diff = 0;
    }

    /* Convert microseconds to milliseconds, rounding up */
    int timeout = (int)((diff + 999) / 1000);

    NSLOG(schedule, DEBUG, "returning time to next event as %dms (diff %lldus)", timeout, diff);

    return timeout;
}

/* exported interface documented in schedule.h */
void list_schedule(void)
{
    uint64_t now;
    struct nscallback *cur_nscb;

    now = get_monotonic_time_us();

    NSLOG(wisp, INFO, "schedule list at %lld", now);

    cur_nscb = schedule_list;

    while (cur_nscb != NULL) {
        NSLOG(wisp, INFO, "Schedule %p at %lld", cur_nscb, cur_nscb->tv);
        cur_nscb = cur_nscb->next;
    }
}


/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
