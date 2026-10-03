/*
 * Copyright 2014 Vincent Sanders <vince@netsurf-browser.org>
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
 * Interface to platform-specific fetcher operations.
 */

#ifndef _WISP_FETCH_H_
#define _WISP_FETCH_H_

#include <wisp/utils/errors.h>
#include <stddef.h>
#include <stdint.h>

struct nsurl;

/** Socket readiness, independent of the frontend and network library. */
enum gui_socket_event {
    GUI_SOCKET_READ = 1,
    GUI_SOCKET_WRITE = 2,
    GUI_SOCKET_ERROR = 4
};

typedef void (*gui_socket_callback)(intptr_t socket, unsigned events, void *pw);

/**
 * function table for fetcher operations.
 */
struct gui_fetch_table {
    /* Mandatory file-type operation; socket_watch below is also mandatory. */

    /**
     * Determine the MIME type of a local file.
     *
     * @note used in file fetcher
     *
     * \param unix_path Unix style path to file on disk
     * \return Pointer to MIME type string (should not be freed) -
     *	   invalidated on next call to fetch_filetype.
     */
    const char *(*filetype)(const char *unix_path);

    /* Optional entries, except socket_watch (documented below). */

    /**
     * Translate resource to full url.
     *
     * @note Only used in resource protocol fetcher
     *
     * Transforms a resource protocol path into a full URL. The returned URL
     * is used as the target for a redirect. The caller takes ownership of
     * the returned nsurl including unrefing it when finished with it.
     *
     * \param path The path of the resource to locate.
     * \return A netsurf url object containing the full URL of the resource
     *         path or NULL if a suitable resource URL can not be generated.
     */
    struct nsurl *(*get_resource_url)(const char *path);

    /**
     * Translate resource to source data.
     *
     * @note Only used in resource fetcher
     *
     * Obtains the data for a resource directly
     *
     * \param path The path of the resource to locate.
     * \param data Pointer to recive data into
     * \param data_len Pointer to length of returned data
     * \return NSERROR_OK and the data and length values updated
     *         else appropriate error code.
     */
    nserror (*get_resource_data)(const char *path, const uint8_t **data, size_t *data_len);

    /**
     * Releases source data.
     *
     * @note Only used in resource fetcher
     *
     * Releases source data obtained from get_resource_data()
     *
     * \param data The value returned from a previous get_resource_data call
     * \return NSERROR_OK on success else appropriate error code.
     */
    nserror (*release_resource_data)(const uint8_t *data);

    /**
     * Find a MIME type for a local file
     *
     * @note only used in curl fetcher in form file controls on
     * RISC OS otherwise its a strdup of a filetype call.
     *
     * \param ro_path RISC OS style path to file on disk
     * \return MIME type string (on heap, caller should free), or NULL
     */
    char *(*mimetype)(const char *ro_path);

    /**
     * Open a socket
     *
     * \param domain Communication domain
     * \param type Socket type
     * \param protocol Protocol
     * \return Socket descriptor on success, -1 on error and errno set
     */
    intptr_t (*socket_open)(int domain, int type, int protocol);

    /**
     * Close a socket
     *
     * \param socket Socket descriptor
     * \return 0 on success, -1 on error and errno set
     */
    int (*socket_close)(intptr_t socket);

    /**
     * Replace the readiness watch for a socket, or remove it when events is 0.
     * Interests are GUI_SOCKET_READ and/or GUI_SOCKET_WRITE. A readiness
     * callback may additionally report GUI_SOCKET_ERROR.
     *
     * Callbacks run on the engine's event-loop thread, never synchronously from
     * this function. A callback may replace/remove any watch, including itself.
     * Removal must suppress outstanding notifications before the descriptor can
     * be reused. The frontend watches readiness only; it does not own the socket.
     *
     * Mandatory. There is no periodic network polling fallback.
     */
    nserror (*socket_watch)(intptr_t socket, unsigned events, gui_socket_callback callback, void *pw);
};

#endif
