/* SPDX-License-Identifier: MIT */
#ifndef dom_html_serialize_h_
#define dom_html_serialize_h_

#include <dom/core/node.h>

/** Serialize the children of an Element, Document or DocumentFragment as HTML.
 * Templates serialize their separate contents; HTML void elements return empty
 * strings. This is HTML serialization, even for nodes owned by an XML document.
 * It neither sanitizes markup nor guarantees a parse/serialize round trip.
 *
 * The caller supplies the scripting setting used for noscript text in ordinary
 * documents. Inert template owner documents always have scripting disabled.
 * On success, result owns a reference (including for empty output); on failure
 * it is NULL. The tree is not changed. Unsupported legacy DOM node types return
 * DOM_NOT_SUPPORTED_ERR rather than silently losing their contents.
 */
dom_exception dom_html_serialize_fragment(dom_node *node, bool scripting, dom_string **result);

#endif
