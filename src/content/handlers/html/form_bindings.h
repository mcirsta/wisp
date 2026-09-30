/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WISP_HTML_FORM_BINDINGS_H
#define WISP_HTML_FORM_BINDINGS_H

#include <wisp/utils/errors.h>

#ifdef WISP_ENABLE_PARTIAL_UPDATES
struct html_content;
struct box;
struct html_form_binding_plan;

/** Prepare native-control registration without touching live forms or gadgets.
 * old_root/new_root may be NULL for hide/show. Their boxes, gadgets, DOM nodes
 * and document must remain alive until apply/discard; new_root stays immutable.
 * Widget-bearing old or new controls and unknown form owners require full refresh.
 * Only affected forms get a temporary replacement list/index. owner is talloc. */
nserror html_form_prepare_bindings(struct html_content *html, struct box *old_root, struct box *new_root, void *owner,
    struct html_form_binding_plan **result);

/** Read-only preflight for coordinated resource/form publication. This does not
 * replace the caller's DOM/style generation and boundary checks. */
nserror html_form_check_bindings(const struct html_form_binding_plan *plan);

/** Revalidate, then register all controls without allocation, DOM writes or
 * callbacks. Requires locked/restyling publication with no yielding until the
 * caller has also spliced boxes and published mappings/resources. Old gadgets
 * become unlinked and can subsequently be destroyed safely. Single use. */
nserror html_form_apply_bindings(struct html_form_binding_plan *plan);
/** Allocation-free write phase after all coordinated plans pass preflight.
 * Requires locked/restyling publication, with no intervening callbacks. */
void html_form_commit_bindings(struct html_form_binding_plan *plan);

/** Free only temporary preparation storage; never gadgets or live indexes. */
void html_form_discard_bindings(struct html_form_binding_plan *plan);
#endif

#endif
