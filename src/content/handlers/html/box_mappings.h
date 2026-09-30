/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_BOX_MAPPINGS_H
#define WISP_HTML_BOX_MAPPINGS_H

#include <dom/core/node.h>
#include <wisp/utils/errors.h>

#ifdef WISP_ENABLE_PARTIAL_UPDATES
struct html_content;
struct box;
struct box_construct_region;
struct html_box_mapping_plan;

/** Prepare mappings for every element in scope, including hidden descendants.
 * old_root may be NULL for a hidden subtree. The caller keeps both trees and
 * the document alive and the candidate immutable until apply/discard. owner is
 * a talloc parent, independent of either retiring box arena. No live mappings
 * are changed. This validates mapping membership, not general box/link closure. */
nserror html_box_prepare_mappings(struct html_content *html, dom_node *scope, struct box *old_root,
    const struct box_construct_region *candidate, void *owner, struct html_box_mapping_plan **result);

/** Read-only preflight. Checks DOM generation, ancestry/order, tree membership
 * and expected mappings. The region publisher must also validate CSS inputs,
 * allocation ownership, layout links, resources, forms and interaction state. */
nserror html_box_check_mappings(const struct html_box_mapping_plan *plan);

/** Revalidate all entries, then publish without allocation or callbacks.
 * Requires locked/restyling publication; single use. Does not splice boxes or
 * retire old storage. Caller must finish publication before yielding. */
nserror html_box_apply_mappings(struct html_box_mapping_plan *plan);
void html_box_discard_mappings(struct html_box_mapping_plan *plan);
#endif
#endif
