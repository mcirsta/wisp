/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_BOX_BOUNDARY_H
#define WISP_HTML_BOX_BOUNDARY_H

#include <wisp/utils/errors.h>

struct html_content;
struct box;
struct box_construct_region;

/** Check physical box links and storage before replacing a visible owned root.
 * candidate may be NULL to inspect the live boundary alone (capture diagnostics).
 * A candidate with root == NULL represents hiding the scope.
 *
 * Uses temporary storage, makes no persistent changes, and must run before any
 * publication writes, without yielding between validation and publication.
 * reason, when non-NULL, receives a static diagnostic string.
 *
 * Success covers box topology, allocation membership, borrowed arena data and
 * computed-style lifetimes only. DOM mappings, resource/form/interaction bindings,
 * input generations and retained layout need their separate publication checks.
 * External float chains are rejected rather than repaired by this check.
 * Checks one replacement only: several simultaneous retirements require checking
 * their combined lifetime boundary, not independent successful calls here.
 */
nserror html_box_check_boundary(const struct html_content *html, const struct box *old_root,
    const struct box_construct_region *candidate, const char **reason);
#endif
