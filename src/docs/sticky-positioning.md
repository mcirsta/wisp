# Sticky positioning: implementation plan

Status: stages 1–3 implemented and validated for the scope below.
Updated 2026-10-01.

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
Reuse the existing inset parsing and test its supported forms; do not add a
separate sticky-specific property parser. The parser currently expands
`inset-block` to top/bottom and `inset-inline` to left/right at parse time. The
latter is correct only for horizontal LTR. Start/end longhands and direction-aware
logical inset mapping need separate CSS work; RTL sticky coverage here uses
physical left/right insets. This corrects the original plan's assumption that
complete horizontal logical-inset mapping already existed.

All ordinary inline sticky boxes (including single-line inlines), sticky table
rows/cells, a floated sticky box itself, vertical writing modes, and transformed
coordinate systems requiring new transform-aware hit testing are follow-up work.
Preserve their current in-flow
behavior and report unsupported sticky geometry in developer inspection; do not
claim full CSS sticky conformance. Page zoom is in scope. Existing transform and
fixed-position behavior must retain its regression coverage.
Keep the existing completed-line reuse exclusion for positioned inline children.
Ordinary text inside a supported sticky block can still use eligible line caches.

## Existing implementation

Paths in this document are relative to the repository root.

| Area | Current code and implication |
| --- | --- |
| CSS | `contrib/libcss/properties.toml`, `[position]`, already accepts `STICKY` and uses three computed bits. `contrib/libcss/src/select/select_config.py` also reserves three bits. No bytecode/storage redesign is needed. |
| Layout | `layout_document()` runs normal layout, absolute positioning, relative positioning, then descendant bounds. The existing final box walk now prepares sticky constraints, without moving layout boxes. |
| Containing blocks | The first implementation slice adds sticky to the general ancestor branch in `layout_position_absolute()`. The existing immediate flex/inline-flex-parent branch remains compatible; direct and deeper descendants of block, flex/inline-flex and grid containers have capture coverage. |
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
With both insets specified, oversized boxes require the specification's end-inset
adjustment. A sole specified inset remains binding, following the browser behavior
documented below. Containment uses its margin-sensitive position box. Sticky also
establishes an absolute containing block and a stacking context, including with
`z-index: auto`.
[CSS Positioned Layout, sections 2–3](https://www.w3.org/TR/css-position-3/#stickypos-insets).

Scroll-container discovery must use overflow semantics, not scrollbar existence.
`hidden`, `auto` and `scroll` can establish a scroll container even without a
visible scrollbar. `visible` does not. `overflow: clip` is unsupported in the
current parser/property enum and is outside this work; do not map it to `hidden`.
Account for cross-axis overflow computation and root/body overflow
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
  Reconcile this with the existing immediate flex-parent branch. Test both direct
  and deeper absolute descendants of block, flex/inline-flex and grid containers.
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
Keep the pixel regression showing that a sticky header's ordinary background
remains within its box. In `redraw.c`, viewport background expansion depends on
background attachment and the root/body check. The first slice removes the unused
`pos_enum` local and the stale claim that `position: fixed` always expands. No
sticky-specific background exception is needed.

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

## Implementation record: first foundation slice (2026-10-01)

The initial foundation slice implemented the following parts:

- `box_sticky.c/.h` contains an allocation-free axis solver with explicit auto
  flags, signed margins, containing-block limits, logical-end adjustment and
  64-bit intermediate arithmetic. It takes resolved geometry only and is not
  yet called by the renderer.
- `layout_sticky_test` covers fifteen Chromium/Firefox reference sequences (90 positions),
  plus auto/zero/negative insets, no movement room, oversized logical-end behavior
  and coordinate overflow. Each sequence returns to its original scroll offset.
- Sticky establishes an absolute containing block and an auto-z stacking context.
  The Qt regression checks direct/deeper absolute children of block, flex,
  inline-flex and grid containers, in-flow size, stacking isolation and bounded
  backgrounds at 100% and 150% zoom.
- The grid fixture exposed a separate prerequisite bug: absolute/fixed children
  were excluded from intrinsic sizing but still entered grid placement. Placement
  and item counting now skip them, and reflow resets their fallback origin.
  Two unit cases check cell occupancy, dimensions and repeated layout. Explicit
  row sizing keeps the sticky fixture independent of the existing grid issue
  where an auto-sized track can overwrite a specified container height.
- The existing generated position parser, three-bit computed representation and
  sticky parser fixture were verified. No generator or CSS storage changes were
  needed; the Qt fixture exercises the real parsing/selection path.

Reference measurements use Chromium 153.0.8010.52 and Firefox 157.0, scripts
disabled, an 800x1200 viewport and device scale 1. Both browsers agree on all
90 positions and all containing-block rectangles. Values are recorded alongside
the numeric unit cases. Local raw measurements are in
`out/render-captures/sticky-foundation-2026-10-01/`, in
`chromium-reference.json` and `firefox-reference.json`.

### Oversized single-inset compatibility decision

The investigation resolved the previous `tall-bottom` discrepancy. With a 180px
box, a 120px scrollport and only `bottom: 10px`, both browsers place the top at
-70px at scroll 100. Our original solver produced -20px: treating the auto top as
zero during end-inset adjustment weakened the specified bottom inset.

Only apply logical-end reduction when both insets are specified. With one auto
inset, retain the other inset unchanged, subject to the containing-block limits.
This also fixes right-only positioning in LTR and left-only positioning in RTL.
It is a deliberate compatibility choice: the current
[Position 3 wording](https://drafts.csswg.org/css-position-3/#stickypos-insets)
describes a broader adjustment, while
[Chromium's implementation](https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/layout/layout_box_model_object.cc)
explicitly gates it on both insets being present. Local Firefox measurements
independently confirm the same behavior.

The new `tall-bottom`, `wide-right` and `wide-rtl-left` numeric cases all fail
against the original solver and pass with the fix. `tall-both` and
`wide-rtl-both` preserve opposing-inset precedence; the horizontal fixtures use a
sibling to establish the normal position so large margins cannot mask movement.
The existing WPT
[LTR opposing-inset case](https://github.com/web-platform-tests/wpt/blob/master/css/css-position/sticky/position-sticky-left-and-right-overconstrained.html)
and [RTL counterpart](https://github.com/web-platform-tests/wpt/blob/master/css/css-position/sticky/position-sticky-horizontal-rtl-overconstrained.html)
were also reviewed; they exercise both specified edges, not the single-auto case.
The former spec-only assertion has been replaced by measured browser regressions.
Fix validation: all 18 unit cases in `layout_sticky_test` and the
`qt_sticky_position` capture pass in Debug ASan/UBSan and Release. The captures
retain their 100%/150% zoom checks. `git diff --check` is clean.

Foundation validation: Qt builds in Debug ASan/UBSan and Release, and Monkey
builds in Debug.
All 128 selected Debug and 117 selected Release tests pass, covering CSS, layout,
grid, stacking, construction, retained updates and Qt captures, plus Monkey in
Debug. The Debug capture test required a rerun with local loopback access because
the sandbox initially blocked its HTTP server. Leak detection was disabled for
the existing sanitizer suite. Python compilation and `git diff --check` pass.
The selection expression is
`^(libcss_|layout_|grid_|stacking_test|box_construct_|transform_clip_test|qt_|css_|restyle_|monkey_)`.

### Settled constraint preparation (2026-10-01)

Stage 1 now includes box-owned metadata and preparation in the existing final
layout walk, after descendant bounds settle. Normal boxes and sticky boxes with
all-auto insets allocate no record. Supported principal boxes resolve separate
containing-block and scroll-container ancestors; anonymous inline wrappers are
skipped. The root itself and generated boxes without a principal DOM node also
remain unsupported. Root/body overflow propagation, mixed overflow axes, hidden/auto without
a scrollbar and reserved scrollbar space have focused coverage. Insets resolve
percentages and `calc()` against the scrollport dimension. Layout positions remain
unchanged; prepared geometry uses unscrolled document coordinates and rejects
unresolved or out-of-range integers.

Each record is owned by its box and borrows only ancestors and the content owner.
Text clones explicitly clear the owning pointer. Layout reset, live region
publication, full rebuild/restart and tree destruction invalidate the document's
constraints before modifying or freeing referenced geometry. Constraints become
visible only after successful finalization. Repeated layout reuses allocations;
unsupported boxes release existing records. Allocation failure propagates as a
layout failure. The derived descendant flag is refreshed during preparation.

`layout_sticky_test` now has 21 cases: the 18 scalar cases plus ancestor/geometry,
lifetime and lazy-scrollbar tests. `qt_sticky_constraints` uses real parsed CSS
and checks supported containers, nested sticky boxes, percentage/calculated and
supported shorthand insets, RTL physical insets, unsupported geometry, root/body
overflow, and default retained/full/uncached parity across repeated toggles,
hide/reveal, resize and document scroll. It checks an unaffected sticky box is
retained and allocations plateau after warm-up. Developer inspection reports
prepared constraints and explicit unsupported reasons.

Storage measured on this 64-bit build: `struct box` grows from 432 to 440 bytes
(8 bytes, 1.85%); each active sticky record is 120 bytes plus an 80-byte talloc
header. The following totals count every structural box and list marker in the
settled top-level tree, including text clones. System malloc overhead/rounding and
the small content-level validity field are excluded.

| Saved page, 1280×900 | Boxes | Sticky records | Added bytes |
| --- | ---: | ---: | ---: |
| Tion | 2,587 | 2 | 21,096 |
| Bihon | 3,779 | 1 | 30,432 |

Both main headers prepare successfully against the viewport. Release captures
with scripts disabled have identical PNG bytes and document extents before and
after this preparation slice. Captures and `storage.json` are under
`out/render-captures/sticky-constraints-2026-10-01/`.

Validation: all 129 selected Debug ASan/UBSan tests and 118 selected Release tests
pass with the same suite selection above, including the new Qt constraint test.
Qt builds in both configurations and Monkey builds in Debug. The unit executable
reports 21 passing cases. Sanitizer leak detection remains disabled as in the
existing suite. Release reports the existing GCC serial LTO compilation notice;
no new source warnings were introduced.

At the end of this preparation slice, headers still had no visual displacement.
The following slice connects those constraints to scrolling. Final timing
comparisons belong to stage 3; preparation reuses an existing layout traversal.

### Visual coordinates, painting and input (2026-10-01)

Stage 2 resolves each queried ancestor chain from root to leaf using stack-local
records. Nested sticky constraints reuse resolved ancestors; no heap allocation,
CSS selection, box construction or layout is performed by the offset resolver.
Explicit-scroll queries retain the supplied document scroll throughout.
Ordinary descendants inherit displacement, absolute descendants use their
containing block, viewport-fixed descendants remain independent, and float
wrappers account for their distinct coordinate parent. A derived ancestor flag
keeps ordinary coordinate queries on their existing path. Unscrolled coordinates
and scroll extents remain layout data.

Painting now applies displacement before culling and stacking. Marked branches
can traverse beyond normal layout bounds while preserving actual overflow and
CSS clips. Deferred stacking collection includes element scroll and normalized
absolute origins. Floats structurally inside a stacking context paint there,
even when layout associates them with an outer float container; otherwise a
sticky header's background could cover its own float. Absolute painting retains
integer layout coordinates across zoom instead of recovering them from rounded
device pixels.

Mouse actions choose the highest painted hit branch before accumulating links
and controls. This prevents an underlying link from surviving when the sticky
header wins. Nearest-text queries use visual coordinates and exclude clipped
text. Frame positions are resolved on demand without reformatting. Frame tests
also exposed shared static mouse-action state and duplicate parent activation;
the state is now call-local and delegated input ends in the child document.
Checkbox toggles and text entry exercise normal repaint and caret/control paths.

An integration test exposed block width solving's use of the end margin for
unused line space. Sticky containment now resolves explicitly specified CSS
margins itself; auto margins retain their actual resolved share. Treating leftover
line space as a specified zero margin had incorrectly prevented horizontal travel.

`qt_sticky_scroll` checks 90 positions across 15 browser-referenced axis cases,
invariant layout rectangles/allocations, solid-color paint probes, real ancestor
clips, nested sticking, container limits and exact return-to-start PNGs. Native
controls, overlapping links, iframe clicks and scrolling are exercised at 100%
and 150% zoom. Two RTL flex cases use centered items to isolate sticky from an
existing RTL flex-placement difference; Chromium measurements confirm those
adjusted cases. The fixtures match client scrollport sizes: Wisp's existing
horizontal-scrollbar layout adds reserved space to the specified height, unlike
Chromium's outer-height accounting. This scrollbar-layout difference is separate
from sticky positioning.

The unit suite adds explicit-scroll ancestry and clipped text-selection cases,
bringing it to 23 cases. Capture inspection now distinguishes visual rectangles
from `layout_border_box`; viewport-point click/scroll actions allow overlap and
frame tests without auto-scrolling to an element.

Release smoke captures of saved Tion and Bihon pass at 1280×900 (initial, scroll
1000 and clamped page end), and 800×600 (scroll 1000), with scripts disabled.
Both 73px headers remain at viewport y=0. Initial PNG bytes and document extents
match the preparation-only build. Captures and `site-smoke.json` are under
`out/render-captures/sticky-scroll-2026-10-01/`. These establish working saved-site
headers; they do not assess the sites' script-driven compact header states.

Validation: all 130 selected Debug ASan/UBSan tests and 119 selected Release
tests pass using the suite expression above. Debug completed in two batches
after the outer command was terminated during the sticky-scroll capture; that
capture and all remaining tests passed on rerun. The 23-case sticky unit suite
was also rerun after adding the empty-selection guard. Qt builds in both
configurations and Monkey builds in Debug. Leak detection remains disabled as in
the existing suite. Python compilation and `git diff --check` pass. Release
reports only the existing GCC serial LTO notice.

### Lifecycle, clipping and reference validation (2026-10-01)

`qt_sticky_lifecycle` exercises native checked-state updates at nonzero document
scroll, changes inside the header and to its containing block, hide/reveal,
replacement, repeated resize and return-to-start scrolling. Default retained and
uncached updates agree with independent forced-full construction on every
inspected geometry and the final image. Unchanged sticky boxes retain their
identity, allocations plateau, and warmed updates actually use retained boxes.
Profiling builds also interrupt real yielded builds by closing, navigating or
shutting down the window, with live sticky records still in the old tree.
`qt_sticky_resources` reveals a delayed image while the header is at its container
limit and checks the subsequent decoding/layout updates its sticky constraints.

The extended scroll fixture covers stretched/non-stretched flex and grid items,
positive/negative stacking levels, two nested scrollports, and an absolute
ancestor's explicit CSS clip at 100% and 150% zoom. An SVG image moves and paints with its header. Native text dragging inside a
stuck header is checked at both zoom levels, alongside an ordinary static-header
reference. Existing control, iframe, selection and overflow tests remain active.

This validation found two existing bugs, fixed with regressions:

- Dragging updated the selected byte range without publishing document selection
  ownership. The tracking path now publishes it after movement and release.
- The generated `clip` getter used unparenthesized masks and shifts, so pixel
  lengths could be read as `em`. Its custom packing also used five unit bits
  where the generator reserves six. The fix is in `select/overrides.py`, with
  parsing, selection and composition tests for mixed units and automatic edges.
  Generated outputs are not edited, and computed-style storage does not grow.

Chromium 153.0.8010.52, with scripting disabled and matching saved DOM, fonts,
viewport and actual scroll offsets, agrees exactly on all eight header rectangles:
Tion and Bihon at 1280×900 (initial, scroll 1000 and page end) and 800×600 (scroll
1000). The wide headers are 1010×73 at viewport (135, 0); the narrow headers are
800×73 at (0, 0). This validates header geometry, not whole-page pixel equivalence
or the sites' scripted compact-header transitions. Reference images and
`chromium-sites.json` are under `out/render-captures/sticky-validation-2026-10-01/`.

### Performance method and diagnostics

Release measurements compare `c1df9926` (before settled metadata and visual
integration) with the completed implementation. The baseline has only the same
optional document-lifetime timing counters added, so both use identical
`WISP_PROFILE_RESTYLE=ON` instrumentation. Captures use scripting disabled,
1280×900, 96 DPI, offscreen Qt, disabled verbose tracing and CPU affinity to one
core. Six alternating before/after pairs warm disk/font caches; reported values
are medians of the last five. Each run loads once, performs fourteen scrolls and
ten native tab switches. Initial layout includes all loading passes (two on each
saved site, one on the ordinary control); warmed update figures use the last six
switches. CPU time is reported alongside wall time because scheduling strongly
affected the unpinned samples. These are core work timings, not display latency.

Tion and the ordinary 500-paragraph control use retained boxes on warmed
switches. Bihon uses CSS reuse with full box reconstruction in both versions;
its numbers must not be described as retained-box timings. All measured scroll
sequences leave the layout-call counter unchanged. Tion averages 67 sticky
queries and 765 ancestry frames per paint, Bihon 18 queries and 123 frames. The
ordinary control has no sticky records or queries. Inspection's own queries are
excluded, and these counters compile out with the existing profiling option.

Measurements exposed unnecessary calls to float and scrollbar helpers in
ordinary stacking collection. Simple type/pointer guards remove that work.
The normal path still has constant eligibility checks, but gains no scroll-time
tree traversal, CSS selection or layout pass. Raw alternating runs are under
`out/render-captures/sticky-validation-2026-10-01/benchmark-final-guarded/`.

| Page | Initial layout CPU, before → after (ms) | Warmed update layout CPU (ms) | Scroll paint CPU per draw (ms) |
| --- | ---: | ---: | ---: |
| Tion | 85.23 → 84.45 | 21.15 → 20.58 | 13.20 → 13.90 |
| Bihon | 151.92 → 152.48 | 37.28 → 38.25 | 3.93 → 5.58 |
| Ordinary control | 10.93 → 11.03 | 1.64 → 1.66 | 0.38 → 0.43 |

Initial and warmed layout medians remain within approximately 3%. Total warmed
update wall times are 24.61 → 24.14 ms, 50.77 → 52.43 ms and 2.65 → 2.71 ms,
respectively. Ordinary repainting retains about 0.05 ms of constant overhead in
this sample; it does not resolve sticky ancestry. The saved-site repaint increases
include drawing headers that previously scrolled out of view, so they are not
isolated measurements of the positioning solver.

An additional Bihon comparison replaces the sticky header with a viewport-fixed
header of identical size and position, preserving its 73px flow space and 33px
bottom margin with a placeholder. Both variants use the final binary and render
identical final PNGs after the same scroll sequence. Five warmed alternating
pairs give 5.88 ms CPU per paint for sticky versus 6.30 ms for fixed (7.23 versus
7.65 ms wall time). This controls for the visible header's drawing cost: sticky
is not slower than that reference in this sample. It does not establish an exact
solver-only cost or imply a general advantage over fixed positioning. Results
and fixtures are in `paint-compare-final/` under the same validation directory.

### Final validation

All 132 selected Debug ASan/UBSan tests and 121 selected Release tests pass using
the suite expression above, split into bounded batches. This includes the full
libcss selection suite after the generator fix, all sticky captures, existing
layout/partial-update regressions, and Monkey in Debug. The sticky unit executable
has 23 cases. Qt and Monkey build successfully; Release emits only the existing
GCC serial-LTO notices. Sanitizer leak detection remains disabled as in the
existing suite. Python compilation and `git diff --check` pass.

The final Release suite runs with `WISP_PROFILE_RESTYLE=OFF`, restored after the
measurements. Sticky behavior needs no profiling or capture option. Final saved
Tion/Bihon captures reproduce all eight Chromium header rectangles and retain
the stage-2 PNG bytes; results are in `final-sites/` under the validation directory.
The planned implementation is complete. Ordinary inline/floated sticky boxes,
sticky table rows/cells, vertical writing modes, transformed ancestry and full
logical-inset mapping remain the explicit exclusions described under scope.

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
