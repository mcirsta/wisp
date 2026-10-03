# Native details and search revealing

Wisp implements the reading-oriented core of HTML `details` by default, with
JavaScript disabled as well as enabled. Presence of `open` controls expansion;
`open="false"` is still open because this is a boolean attribute.

The first direct `summary` is rendered first and remains visible when closed.
Other children, including additional summaries and raw text, belong to the
collapsible contents. A missing summary gets an anonymous, localized “Details”
legend. The contents use one anonymous block, including when the details element
is flex/grid, so each DOM child does not become a separate flex/grid item.
Closed contents have no descendant layout boxes. This is native construction
behavior, not an author-overridable `display:none` rule on the children.

The first summary's mouse activation toggles `open` and requests an ordinary full
restyle. Descendant links, controls and labels retain their own activation.
The anonymous legend can be clicked too. No synthetic DOM nodes, per-box fields,
renderer threads or production feature switches were added. Summary references
are held only on the construction ancestry stack and released on completion,
failure or cancellation.

The UA stylesheet gives the first summary `list-style:disclosure-closed inside`
and switches to `disclosure-open` while expanded. Author `list-style` and
`display` overrides still apply. The default legend uses the same disclosure
glyphs. These are font-rendered triangles, so their shape and advance can differ
from Chromium's native symbol.

Inside markers are ordinary inline children, with nonwrapping generated text
(the UA marker whitespace default) or a replaced image; they contribute to
intrinsic sizing and wrapping. The existing `list_marker` pointer identifies
them for numbering, while outside markers remain
separate tree edges. Shared traversal helpers prevent double painting, selection
and ownership checks. Numbering is prepared before intrinsic layout, including
retained lists whose preceding items changed. No box fields or style-storage bits
were added: the new CSS values fit the existing six-bit list-style field. Parser
keywords are regenerated from `propstrings.h`; generated files are not edited.

Marker text uses the selected `::marker` font and colour, composed into an
anonymous style that excludes positioning, dimensions, borders and backgrounds.
The same construction applies to inside and outside list markers. The originating
item still supplies `list-style-type`, `list-style-image` and placement. Custom
marker `content`, whitespace interoperability and outside-marker baseline sizing
remain in the correctness backlog.

Documents containing details use the same DOM search path as hidden-until-found.
Discovery visits the summary first, then the body regardless of its open state,
while respecting ordinary hidden/display/content-visibility barriers. Only the
active match reveals ancestors. Searching or linking to the summary, or linking
to the details element itself, does not open that element. Nested details and
hidden-until-found ancestors share one reveal/restyle operation; beforematch
listeners are followed by an ancestry recheck. Highlights are rebound to the
published box tree and no old boxes survive through the reveal.

## Keyboard focus follow-up

Keyboard activation needs shared document focus support before adding summary
handling. The current `html_keypress` delegates to embedded content or textareas;
Tab traversal in `box_textarea.c` only visits text fields in the same form.
`node_is_focus` in CSS selection always returns false. Full restyling clears the
box-backed focus before freeing the old tree, and retained publication currently
requires document-self focus. A separate summary-only Tab loop would leave these
ownership and navigation problems in place.

Implement this in two batches:

1. **Document focus foundation.** Hold a referenced DOM focus target independently
   of layout boxes, with explicit cleanup on navigation/destruction and rebinding
   after full or partial publication. Define focus loss when a target becomes
   hidden, disabled or disconnected; preserve textarea values/carets across updates.
   Add shared sequential traversal for links, controls and the first summary,
   including `tabindex`, hidden/inert/disabled filtering, collapsed bodies and an
   explicit identity for the anonymous default legend. Integrate `:focus` and a
   visible focus indicator with existing invalidation rather than rebuilding the
   page on every Tab. Resolve frame boundaries through the browser-window focus
   routing rather than a second traversal in HTML.
2. **Keyboard activation and frontend integration.** Route cancelable keyboard
   events to the focused target, then use shared native activation for Enter and
   Space without intercepting interactive summary descendants. The current core
   API accepts only keypresses: add the key-release/modifier information needed for
   correct Space activation. Qt must pass Tab/Backtab into document traversal and
   return focus to browser chrome at document boundaries; `keyPressEvent` alone is
   insufficient because QWidget handles Tab traversal first. Add named capture-key
   actions and a real Qt event-dispatch test, alongside other frontend adapters.

Validation must cover forward/backward order, positive/negative `tabindex`,
collapsed/hidden/disabled targets, default legends, no-JS activation, canceled
events, repeated toggles, focus surviving publication, removal of a focused node,
form editing, frame/chrome boundaries and reference/allocation stability. This is
a separate feature pass, not part of the native mouse/search implementation.

## Validation

`qt_details_search` exercises initial collapse, boolean `open`, summary placement,
native clicks, default legends, interactive summary descendants, details-only
search, closing an active match, forward/backward search, show-all, nested and
mixed reveals, generated content, fragments, barriers and cancellation. It also
checks that an open flex details groups its body children in one content slot.
`qt_disclosure_markers` checks marker clicks, CSS overrides, wrapping, empty and
block-first summaries, list images, numbered/reversed lists, resizing and text
selection. `qt_marker_styling` checks marker fonts/colours, forbidden box-property
isolation, images, resizing, native restyling and actual painted colour when
ImageMagick is available. The construction tests compare retained inside/outside
list updates against fresh layouts, including numbering changes, marker styling
and allocation stability.
Libcss tests cover parsing, composition, glyph formatting and CSS direction.

Reference captures are in `out/render-captures/details-2026-10-03/`. The prior
Release capture exposes the closed body; the new fixture's inspected visible
rectangles match Chromium with author-disabled summary markers. Native marker
captures and test logs are in `out/render-captures/disclosure-markers-2026-10-03/`.
The primary marker cases match Chromium's line heights and wrapping, with small
font advance differences. The additional eight-pixel-wide Korean counter exposes
a marker-whitespace interoperability difference, documented alongside RTL and
remaining marker work in [the correctness backlog](layout-correctness.md).
The initial disclosure validation passed 98 selected Debug tests (with sanitizers)
and 11 Release tests; the extra decorated-summary/counter capture passed in both
builds. Marker styling subsequently passed 79 selected Debug tests (with ASan and
UBSan) and eight Release tests, including painted colours and retained-list
updates. Logs and the Chromium comparison are in
`out/render-captures/marker-styling-2026-10-03/`; the outside-marker baseline
difference is tracked in the backlog.

References: [details and summary behavior](https://html.spec.whatwg.org/multipage/interactive-elements.html#the-details-element),
[native rendering](https://html.spec.whatwg.org/multipage/rendering.html#the-details-and-summary-elements),
[counter styles](https://www.w3.org/TR/css-counter-styles-3/#disclosure-open),
[marker layout and default styles](https://drafts.csswg.org/css-lists/#marker-pseudo),
and [ancestor revealing](https://html.spec.whatwg.org/multipage/interaction.html#ancestor-revealing-algorithm).
