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
marker `content`, whitespace interoperability and the remaining special marker
cases are in the correctness backlog.

Outside markers use the first content baseline. A taller marker ascent pushes
the first line (or first block child with a baseline) down; a smaller marker moves
down to meet the content. The marker's descent does not enlarge the content line.
This follows the observed Chromium behavior; outside-marker placement is not
fully specified by CSS Lists. Ordinary block descendants export their first
baseline through the existing `baseline` field, while inline containers keep
their last baseline for inline-block alignment. No box fields were added.
Floats introduced on the pushed line or inside a pushed block move with it.
Their shared formatting-context list is reordered for subsequent float/clearance
queries, and following float placement respects their new positions.

First-baseline propagation supports outside markers, table cells and flex/grid
alignment. Nested inline containers reuse intrinsic widths, but reformat lines
when their first baseline is needed because the line cache stores only the last
baseline. Direct list-item line reuse is also excluded. Block-only items reserve
space for their markers, and flex/grid/table descendants export their baselines.

An empty item, or one containing only floats, positioned content or collapsed
inline whitespace, still has a marker line. The item's normal line metrics and
marker ascent determine its height; this line also separates its top and bottom
margins. Floats and positioned static anchors follow the line's baseline push.
CSS height/min-height/max-height constraints still apply normally. Items with no
marker keep their ordinary empty-block behavior.

This reuses the existing line layout when there is an inline container. Truly
empty items reserve the same line during block sizing, before applying height
constraints, and export its baseline for enclosing inline-blocks. No anonymous
boxes, per-box fields or alternate rendering paths are added.

Outside image markers also reformat their content after their dimensions are
resolved, as ordinary image boxes do. This matters for SVG: loading reads the
intrinsic dimensions, while reformat parses the shapes at the resolved viewport.
Detached markers previously skipped that step and painted an empty diagram.
The existing content layer skips repeated reformats at unchanged dimensions.

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
`qt_outside_marker_baseline` uses the bundled test font to check first-line pushes,
smaller markers, wrapping, padding, nested blocks, zero line-height, relative
positioning, float-only attempts before a real line, clearing line breaks, image
geometry and resize stability. Pixel checks compare text/image-marker and content
baselines; `qt_marker_styling` also checks inside SVG marker paint. Unit tests
change marker font sizes repeatedly; retained-list tests also cover nested,
wrapped content over 100 updates per variant.
Libcss tests cover parsing, composition, glyph formatting and CSS direction.

`qt_empty_marker_lines` checks empty/float-only items, positioned content,
whitespace, text/image markers, positive and negative margins, constrained
heights, inline-block baselines, resizing and native marker-style changes.
The C baseline suite repeats marker font changes for empty formatting-context
roots, ordinary items and float-only items. Retained construction tests include
empty outside items in forward/reversed lists with block/inline body styles,
comparing fresh layout and allocation stability over 100 updates per variant.

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
difference prompted the follow-up below.

The outside-baseline follow-up has Chromium comparisons and test logs in
`out/render-captures/outside-marker-2026-10-03/`. The original marker-styling
fixture's item heights now match Chromium, including the formerly 24px-tall
outside item, which is now 27px. The extended fixture also caught the one-pixel
float-clearance difference, subsequently fixed by retrying at the exact float
bottom edge. Its missing outside-image paint was fixed
by the subsequent viewport-reformat change described above.
The final extended reference is in `complete/`. Validation passed 33 selected
Debug tests (ASan/UBSan) and eight Release tests. Four Debug capture tests needed
a rerun because their process launches overlapped the final executable relink;
all four passed once the build finished. See `tests-debug.log`,
`tests-debug-rerun.log` and `tests-release.log`.

The outside-image follow-up passed 13 selected tests in both Debug (ASan/UBSan)
and Release. Its pixel assertion failed before the fix; the 12×40 SVG rectangle
now paints all 480 expected pixels. Before/after captures, the saved Chromium
comparison and test logs are in `out/render-captures/outside-marker-image-2026-10-03/`.

The marker-only line follow-up passed 34 selected Debug tests (ASan/UBSan) and
11 Release tests. Fourteen fixture cases match Chromium's item heights, top
offsets and following-sibling positions; the inline-block baseline also aligns.
Comparisons and test logs are in `out/render-captures/empty-markers-2026-10-03/`.
The flow-root/float case retains the intentional float-geometry difference below:
Wisp includes the moved float in its 54px height; Chromium reports 48px.

There is also an intentional difference in the float-containing cases. Chromium
moves the float's visible box down with the marker but still sizes its flow-root
and clears subsequent content using the old float position. For example, in the
`final/` capture `#block-floats-float` ends at y=942, yet the following `clear:both`
block starts at y=936. Wisp keeps exclusion geometry consistent with the moved
float, following the [clearance rule](https://www.w3.org/TR/CSS22/visuren.html#flow-control).
The regression therefore checks clearance and flow-root height against the final
float edge rather than copying Chromium's six-pixel overlap.

References: [details and summary behavior](https://html.spec.whatwg.org/multipage/interactive-elements.html#the-details-element),
[native rendering](https://html.spec.whatwg.org/multipage/rendering.html#the-details-and-summary-elements),
[counter styles](https://www.w3.org/TR/css-counter-styles-3/#disclosure-open),
[marker layout and default styles](https://drafts.csswg.org/css-lists/#marker-pseudo),
[Chromium outside-marker baseline placement](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/third_party/blink/renderer/core/layout/list/unpositioned_list_marker.cc),
and [ancestor revealing](https://html.spec.whatwg.org/multipage/interaction.html#ancestor-revealing-algorithm).
