/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_BOX_PUBLISH_H
#define WISP_HTML_BOX_PUBLISH_H

#include <wisp/utils/errors.h>

struct html_content;
struct box_construct_region;

/** Publish one completed, unlaid-out candidate for the sole active checked-state
 * scope. Its visible block/flex/grid boundary must remain visible; hidden panels
 * are replaced through their stable ancestor, not by removing the boundary itself.
 * The caller must keep the original tree alive during detached construction,
 * capture restyle inputs before construction, and enter locked/restyling state.
 *
 * All preparation and validation precede the allocation-free write phase.
 * Failure leaves the displayed tree, bindings, interactions and *candidate
 * unchanged; the caller may discard it and choose full reconstruction.
 * Success consumes *candidate (sets it to NULL), splices its boxes, publishes
 * mappings/resources/forms and retires the old owned arena. It keeps the content
 * locked: the caller MUST reset retained layout state and complete full layout
 * before exposing the tree, ending restyle or yielding. Post-publication failure
 * requires a full rebuild/content error; there is no rollback of retired boxes.
 *
 * Active interaction/search, frames and unsupported resources/widgets currently
 * require whole reconstruction. Eligible native checked-state updates use this
 * path by default. reason optionally receives a static diagnostic string.
 */
nserror
html_box_publish_region(struct html_content *html, struct box_construct_region **candidate, const char **reason);
#endif
