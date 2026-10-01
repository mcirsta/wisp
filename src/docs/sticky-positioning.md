# Sticky positioning: implementation plan

Status: proposed, not implemented. Updated 2026-10-01.

## Goal and scope

Implement CSS sticky positioning in the shared HTML renderer, starting with the
headers in the saved Tion and Bihon pages. Preserve normal layout and apply a
separate visual displacement during scrolling. Scrolling must not reconstruct
boxes, reselect CSS, measure text, or run document layout.

This is a bounded renderer feature, smaller than the partial-update project.
Use the existing box, scrolling, stacking and capture infrastructure. Keep the
renderer single-threaded and make the feature standard code, without a new
compile-time or runtime feature switch. Developer captures remain optional.

The initial implementation covers unfragmented block, flex, grid and inline-block
containers, including flex/grid items, in horizontal writing modes (LTR and RTL).
Implement both axes and all four physical insets through one axis calculation.
Include viewport and element scrolling, nested sticky containers, native controls,
and ordinary/absolute/fixed/float descendants within that scope.
Reuse the existing logical-inset mapping for these writing modes and test it;
do not add a separate sticky-specific property parser.

Fragmented inline sticky boxes, sticky table rows/cells, a floated sticky box
itself, vertical writing modes, and transformed coordinate systems requiring new
transform-aware hit testing are follow-up work. Preserve their current in-flow
behavior and report unsupported sticky geometry in developer inspection; do not
claim full CSS sticky conformance. Page zoom is in scope. Existing transform and
fixed-position behavior must retain its regression coverage.

## Existing implementation

Paths in this document are relative to the repository root.

| Area | Current code and implication |
| --- | --- |
| CSS | `contrib/libcss/properties.toml`, `[position]`, already accepts `STICKY` and uses three computed bits. `contrib/libcss/src/select/select_config.py` also reserves three bits. No bytecode/storage redesign is needed. |
| Layout | `layout_document()` in `src/content/handlers/html/layout.c` runs normal layout, absolute positioning, relative positioning, then descendant bounds. Sticky has no positioning pass. |
| Containing blocks | `layout_position_absolute()` recognizes relative ancestors and transforms; sticky ancestors are missing. |
| Coordinates | `box_coords()`, `box_coords_at_scroll()` and `box_coords_unscrolled()` in `box_inspect.c` already distinguish viewport-fixed geometry and nested scrollbar offsets. |
| Painting | `redraw.c` mixes inherited coordinates with containing-block coordinate queries, then culls against descendant bounds. |
| Interaction | `box_next_xy()`/`box_at_point()` and `interaction.c` walk boxes separately from painting. Matching coordinates alone do not establish matching paint order. |
| Scroll events | Qt's `NS_Widget::setHorizontalScroll()` and `setVerticalScroll()` already repaint without layout. Nested scrolling uses `html_overflow_scroll_callback()`. |
| Scrollbars | `box_handle_scrollbars()` in `box_manipulate.c` can create/update scrollbars during paint. `html_restyle_scrollbars()` also updates retained ones after layout. |
| Partial updates | Retained publication replaces one region, frees its old owner, and then resets/layouts the surviving tree. Sticky metadata must respect these lifetimes. |
| Tests | `src/test/layout_fixed_test.c`, `frontends/qt/test_fixed_position.py` and ordered Qt capture actions provide useful patterns. |

Both saved sites contain `#main-header { position: sticky; ... }`. Their saved
markup/styles also contain a `stuck` class that changes header contents and
height. Compare identical saved DOM states with scripting disabled; CSS sticky
support does not implement a site's scripted class changes.

## Behavioral reference

A sticky box retains its normal-flow space. Insets constrain its visual position
against the nearest scrollport, subject to its containing block. Percentages use
the relevant scrollport dimension. `auto` must remain distinguishable from zero.
Oversized boxes require the specification's end-inset adjustment; containment uses
its margin-sensitive position box. Sticky also establishes an absolute containing
block and a stacking context, including with `z-index: auto`.
[CSS Positioned Layout, sections 2–3](https://www.w3.org/TR/css-position-3/#stickypos-insets).

Scroll-container discovery must use overflow semantics, not scrollbar existence.
`hidden`, `auto` and `scroll` can establish a scroll container even without a
visible scrollbar. `visible` does not; `clip`, if supported, is different from
`hidden`. Account for cross-axis overflow computation and root/body overflow
propagation to the viewport.
[CSS Overflow, sections 3.1 and 3.3](https://www.w3.org/TR/css-overflow-3/#overflow-properties).

Use these specifications for the algorithm and Chromium as an independent fixture
reference. Investigate disagreements rather than adjusting constants to match a
particular screenshot.

## Coordinate and storage design

### Keep layout geometry separate

`box->x/y`, widths, heights and line-cache results remain layout geometry. Do not
add sticky displacement to them, including during `layout_position_relative()`.
For this work, layout geometry includes the engine's existing relative-position
adjustments but excludes sticky displacement and scrolling.

Preserve `box_coords_unscrolled()` as the layout-only query. Extend the visual
coordinate path used by `box_coords()` and `box_coords_at_scroll()` to include
sticky displacement. Document that these remain unscaled document coordinates
before CSS transforms; converting to viewport pixels subtracts document scroll
and applies zoom. The explicit-scroll API must use its supplied scroll values
throughout, without accidentally consulting the live window in a nested helper.
Audit existing visual-coordinate calls inside layout, particularly absolute
static-position calculation and iframe placement: geometry calculations must
explicitly request layout coordinates, and frame presentation origins must be
updated after sticky constraints become valid.

Add a small internal module, provisionally `box_sticky.c/.h`, for eligibility,
constraint preparation and offset resolution. Keep the axis solver independent
of DOM, painting and browser-window APIs so it can be unit tested directly.

### Metadata

The proposed starting representation is one optional `struct box_sticky *` in
`struct box`, with the record allocated as a talloc child of that box only when
needed. It holds the content owner, containing block and scrolling ancestor,
resolved inset information, settled constraint geometry and a validity marker.
The viewport case is explicit and uses `html_content.viewport`.

Keep borrowed box references restricted to ancestors. Do not introduce a global
registry of sticky descendants, DOM/style references, or another tree ownership
scheme. An ancestor with sticky content can be marked with a box flag for culling.
Determine eligibility from the actual principal box, not merely a shared style:
text boxes and anonymous wrappers must not acquire independent sticky records.

Initialize the optional pointer in `box_create()`. Audit structure copies,
especially `layout_text_box_split()`'s `talloc_memdup()`, so clones cannot alias
an owning metadata pointer. Clear derived flags on reset and initialize them on
new boxes. Allocate during layout preparation, never during scrolling or painting;
allocation failure follows the existing layout-error path.

Measure the actual `sizeof(struct box)` change on the target builds. The nominal
cost is one pointer per box (8 bytes on a 64-bit build), plus records for sticky
boxes and allocator overhead. Report the measured page cost; do not conceal the
per-box cost by describing only the sparse records. If padding or measured page
cost makes this excessive, reconsider the representation before renderer
integration, rather than immediately building a general property table.

### Prepare constraints from settled layout

After normal/absolute/relative positioning, identify each eligible sticky box's
formatting-context containing block and nearest scrolling ancestor. These are
different relationships; neither is necessarily `box->parent`. Skip anonymous
wrappers appropriately and check flex/grid items against their actual layout
container. Do not reuse `abs_containing_block` for the sticky box itself.

Prepare metadata in a final layout walk, integrated with an existing final walk
where practical. Resolve lengths with the existing CSS unit helpers. Retain
explicit inset-type bits rather than using `AUTO`/`INT_MAX` in arithmetic. Use
wider intermediates and the existing layout debug checks for coordinate sums.

Scrollbar creation is currently lazy. Derive the scrollport from settled box
geometry and scrollbar-presence rules, with the same reserved space as
`box_handle_scrollbars()`, rather than requiring a scrollbar object to exist.
Finalize any overflow-dependent values after normal descendant bounds are known.
Read current offsets at query time, including after retained scrollbar clamping.

### Resolve the visual offset

Proposed internal responsibilities (names can follow surrounding conventions):

```c
bool box_sticky_prepare(html_content *html, struct box *box);
void box_sticky_offset(const struct box *box,
        const struct box_scroll_context *scroll, int *dx, int *dy);
```

The scroll context carries explicit document scroll and viewport dimensions for
the current paint/query. Nested offsets come from the existing core scrollbars.
The implementation should resolve ancestors before descendants, sharing already
known coordinates during a walk. Avoid recursively recomputing every ancestor
chain for every child or performing heap allocation to answer a coordinate query.

For each axis, pass the box and constraint intervals in one coordinate space to
the solver. Implement the referenced sticky algorithm, including opposing insets,
oversized boxes and margin adjustments; a plain top-edge `max()` is insufficient.
Evaluate from settled geometry on every query, not by incrementally adding the
last scroll delta. A scroll down/up round trip must return exactly to the same
coordinates.

Ordinary descendants inherit the visual shift once. Absolutely positioned
descendants obtain it through their containing block. Viewport-fixed descendants
retain their independent viewport coordinates. Float wrappers require an explicit
audit because their coordinate ancestry can skip a sticky DOM ancestor. Nested
sticky boxes must evaluate their box, containing block and scrollport with the
same already-resolved ancestor shifts.

## Renderer integration

### Positioning and stacking

- Add sticky to the ancestor branch in `layout_position_absolute()` so positioned
  header children use the header as their containing block, even at scroll zero.
- Add sticky to `box_get_z_index()` and its automatic stacking-context handling.
- Keep abs/fixed-only flow exclusion and blockification checks abs/fixed-only.
  Audit position checks individually; do not replace them wholesale with a
  generic “non-static” check.
- Keep the geometry eligibility check separate from positioned/stacking semantics
  so unsupported fragmented geometry does not accidentally become out of flow.

### Painting, bounds and invalidation

Apply the shared offset before visibility rejection, backgrounds, borders, child
painting and stacking-context collection. Audit every path that supplies
`x_parent/y_parent` to a collected stacking entry; reparented painting must not
lose or duplicate an ancestor's sticky offset. Cover both unit and non-unit zoom.

The existing `descendant_*` fields serve culling and scroll extents. Do not
inflate them with a broad possible sticky movement range merely to defeat
culling. Start with a derived `HAS_STICKY_DESCENDANT` flag: when a normal bound
would reject that branch, inspect its sticky content while retaining real
overflow clips. Leave the unmarked fast path unchanged. Sticky cannot use the
viewport-fixed rule that discards inherited clipping.

Keep scrollable extents and visual culling conceptually separate. Check genuine
sticky overflow contributions against the fixtures; add any required contribution
at layout finalization. Do not modify scroll ranges on every paint or create a
scrollbar/offset feedback loop. This is a correctness gate, not permission to
ignore overflow differences.

Qt already repaints the viewport for document scrolling. Confirm the core path
also invalidates the affected nested scrollport and any exposed old position.
For this first implementation, repainting that scrollport is sufficient; damage
rectangle optimization can wait. Check core-managed iframe scrolling separately.
Do not add a full box-tree update on each scroll event.

### Mouse, selection, controls and capture

Integrate the same coordinate resolver with `box_move_xy()`, `box_at_point()`,
nearest-text lookup, selection/caret rectangles and box-based redraw requests.
Preserve real clipping when continuing a hit walk through a branch marked with
sticky descendants.

Audit `get_mouse_action_node()` against the stacking order used by paint. Its
current special handling for checkable controls is not a general hit-order
guarantee. If an underlying link wins over a visible header link, extract/reuse
the existing stacking comparison for those candidates; do not add a header-ID
exception. Test real mouse activation, not only presence in a hit candidate list.

An iframe's window position is currently set during descendant-bound calculation.
If an iframe is inside moving sticky content, synchronize its visual origin
without reformatting the frame on each scroll. Test both forwarding clicks into
the frame and scrolling within it; painting movement alone is not sufficient.

Update `capture_inspect.cpp` and `CAPTURE.md` to distinguish layout and visual
rectangles and, for sticky boxes, expose displacement, constraint owner and
scrolling ancestor. Keep existing rectangle fields compatible and add explicit
layout fields. Point queries stay in documented document coordinates. Recompute
a click target after a capture action scrolls it into view; the current helper
calculates its click before scrolling. Add a fixed-coordinate click action if
needed to test overlap without auto-scrolling away from the case.

## Partial updates and lifecycle

Reuse metadata allocation on retained boxes, but invalidate/recompute its derived
values for every completed layout. A sidebar height change may move a header's
containing-block boundary even when the header's own style and size are unchanged.
Sticky preparation must run even when text intrinsic sizes or completed lines
were reused. Scrolling itself does not invalidate those text caches.

In `layout_reset_document()`/`layout_reset_box()`, invalidate constraints without
dereferencing their old borrowed pointers. Recompute descendant flags over the
current tree. Ensure ordinary resize/image/font reflows also refresh metadata;
the retained reset function alone does not cover those paths.
Use a document-level validity bit to disable use of old constraints before any
layout geometry changes, including ordinary `layout_document()` entry and live
region publication. Enable it only after the final preparation succeeds. This
avoids needing a separate invalidation tree walk before every ordinary reflow.

Review `box_publish.c` and the full replacement paths in `html.c` with the new
ownership rule. Metadata in retired regions dies with its owning box. Surviving
records can reference only surviving ancestors, never boxes inside a replaced
sibling region. Candidate construction must not publish live sticky pointers.
After a failed layout, no partially prepared state may be exposed to paint/hits.
Content lock/error behavior remains the existing authority for presentation.

Do not disable partial updates for every page containing sticky content. Add
specific exclusions only if a demonstrated unsupported relationship requires
them, record the reason here, and verify the full-layout result.

## Implementation sequence and acceptance

### 1. Establish geometry and the test baseline

- Add `test/sticky-position.html` with independent, named cases and simple
  deterministic dimensions. Record Chromium rectangles at each test scroll.
- Add `src/test/layout_sticky_test.c` for the axis solver, coordinate ancestry,
  containing-block selection and scroll-container discovery.
- Implement the small metadata/constraint module and its layout integration.
  Register it in `src/CMakeLists.txt` and its tests in `src/test/CMakeLists.txt`.
- Add containing-block and stacking coverage, including scroll-zero behavior.
- Verify the generated CSS parse/selection path preserves sticky and inset types.
  If a generator change is needed, edit its source inputs and regenerate normally.

Exit: named unit cases pass, layout coordinates remain stable across scroll
queries, and unsupported geometry is identified explicitly. This step alone is
not a usable sticky-header feature.

### 2. Connect painting and interaction

- Integrate visual coordinates, branch culling, stacking, scrolling invalidation,
  selection/controls, frame origins, and the capture changes above.
- Add `frontends/qt/test_sticky_position.py` and register it in
  `frontends/qt/CMakeLists.txt`, following the existing fixed-position test.
- Use geometry assertions, solid-color pixel probes and actual input actions.
  Require capture completion and usable boxes before indexing inspection output.

Exit: viewport and nested-scroll fixtures paint and interact correctly before
sticking, while stuck, at their container limit, and after returning to the start.
Fixed/absolute positioning and ordinary page captures still pass.

### 3. Validate updates, real pages and cost

- Add a native `:checked` fixture that changes content inside a sticky box and
  changes its containing block outside it. Exercise default retained updates,
  forced full reconstruction and uncached layout at nonzero scroll offsets.
- Include repeated resizing, hide/reveal, region replacement and close/navigation
  in sanitizer runs. Check metadata allocation plateaus across repeated updates.
- Capture saved Tion/Bihon with scripts disabled at 1280x900, 96 DPI, matching
  Chromium's saved DOM state and scroll offsets. Check initial, mid-page and
  near-bottom positions; also use a narrower viewport.
- Record initial layout time, warmed partial-update time, scroll/paint time and
  memory before/after. Use Release, identical settings and alternating runs.

Exit: the fixture matrix below passes, saved-site differences are understood,
scrolling causes no CSS/construction/layout work, and no unexplained regression
remains on pages without sticky content. Update this document with results and
any explicitly deferred cases. Commit these coherent stages separately; no
temporary production feature gate is needed.

## Required fixture matrix

| Area | Cases |
| --- | --- |
| Insets | Each edge; both opposing edges; both auto; zero, negative, percentage and supported calculated lengths; an oversized box; LTR/RTL. |
| Containment | Short/tall containing blocks; margins, borders and padding; no movement room; positive/negative margins; end limit; scroll round trip. |
| Scrolling | Document and element scroll; nested scroll containers; auto without overflow; hidden ancestor without a scrollbar; mixed overflow axes; root/body handling. |
| Layout types | Block header; flex/grid container; flex/grid item with stretch and non-stretch alignment; inline-block; nested sticky. |
| Descendants | Text and selection; absolute child; viewport-fixed child; floats; image/SVG; native control; iframe and nested frame scroll. |
| Paint and hits | Auto/positive/negative z-index; overlapping link/control; genuine overflow clip; ordinary ancestor bounds off screen; page zoom. |
| Lifecycle | Resize while stuck; image-driven reflow; checked-state height change; retained/full/uncached agreement; repeated replacement and destruction. |
| Non-sticky | Existing relative/absolute/fixed, float, flex/grid, scrolling-bounds and partial-update regressions. |

Keep assertions named and based on the fixture's geometry. Pixel probes should
avoid glyph edges. Chromium comparisons must use actual clamped scroll offsets,
not merely the requested values. Use native interactions with scripting disabled
in Wisp; Chromium-side inspection code may read geometry without changing the
fixture's behavior.

Run focused unit/capture tests during development, then the existing CSS, layout,
stacking, box construction, retained-update and Qt capture suites in Debug
ASan/UBSan and Release. Build Qt and Monkey to check shared-core integration.
Use Ninja's default parallelism. A documentation-only change to this plan does
not require rebuilding or rerunning those suites.

For performance, separate the expected cost of painting a now-visible header
from positioning overhead. On pages without sticky boxes there should be no
sticky allocations or extra scroll-time tree pass. On sticky pages, count offset
resolutions and ancestor visits in optional diagnostics to catch repeated deep
walks. Prefer existing instrumentation; add counters only where it cannot answer
the question. Do not run verbose logging in timing comparisons.
