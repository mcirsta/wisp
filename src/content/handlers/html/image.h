/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_HTML_IMAGE_H
#define WISP_HTML_IMAGE_H

#include <dom/dom.h>
#include <libcss/unit.h>
#include <wisp/utils/errors.h>

struct html_content;
struct html_image;
struct content_html_object;
struct box;
struct nsurl;

struct html_image_state {
    bool complete;
    unsigned natural_width, natural_height;
    const struct nsurl *current_src; /* Borrowed until the next engine operation. */
};

bool html_image_is_element(dom_node *node);
/* Source selection is read-only; search/discovery must not start downloads. */
bool html_image_select_source(dom_node *node, struct html_content *html, struct nsurl **url, double *density);
/* Read-only source-set parser; density belongs to this selection, not the cache. */
bool html_image_source_parse(dom_string *src, dom_string *srcset, dom_string *sizes,
    const css_unit_ctx *units, const css_media *media, double target_density, struct nsurl *base,
    struct nsurl **url, double *density);
/* Correct supplied decoded dimensions; true if both source lengths are known. */
bool html_image_dimensions(const struct box *box, int *width, int *height);
nserror html_image_schedule(struct html_content *html, dom_node *node, bool attribute_changed);
nserror html_image_bind(struct html_content *html, dom_node *node, struct box *box);
void html_image_get_state(struct html_content *html, dom_node *node, struct html_image_state *state);
/* HTML dimension attributes: the synchronized content box wins when rendered;
 * otherwise use a valid attribute, then the density-corrected natural size. */
dom_exception html_image_get_dimension(struct html_content *html, dom_node *node,
    const struct box *box, bool height, uint32_t *value);
void html_image_update_at_checkpoint(struct html_content *html, dom_node *node, uint32_t revision);
void html_image_stop(struct html_image *image);
void html_image_fini(struct html_image *image);

/* Shared rendering update, with the request already reflected in object. */
void html_object_image_changed(struct content_html_object *object, bool changed);
#endif
