# Partial document updates: implementation plan

Status: milestone A validated and enabled by default, 2026-09-29. The reviewed baseline is `eb3e03df`
(`Use stack scratch for small class selector lists`).

The foundation is committed as `fdd92975`, and milestone A as `180d7009`.
Milestone A now reuses composed styles
outside the changed controls' parent subtrees and still reconstructs and lays out
the whole document. `WISP_ENABLE_PARTIAL_UPDATES` now defaults to ON; configuring
it OFF still removes the HTML tracking state and selects the full-refresh path.
Existing CMake caches explicitly set to OFF keep that setting until reconfigured.
Milestone B's allocation preparation has started; boxes are not yet retained.
Milestone C has not started. The steps below remain the implementation and
acceptance checklist, with the concrete implementation choices recorded here.

### Milestone A implementation

- Libcss classifies parsed selectors at insertion, with explicit combinator,
  pseudo and rule allowlists. Each stylesheet retains one conservative unsafe
  flag; each query checks its imports, including late registrations and disabled
  sheets. A shared monotonic revision clock covers stylesheet and selection-context
  changes without hashing inputs or storing per-element dependencies.
- `css_select_results_clone()` allocates only a result container and references
  every computed-style slot. Composition resolves a new style before interning;
  published payloads are read-only. Ownership tests cover both destruction orders,
  aliases across all pseudo slots, strings, backgrounds/gradients, tracks,
  transforms, generated content and calculated lengths. No generator changes or
  per-property equality classifier are needed.
- HTML uses a sorted temporary node/result vector, borrowed from the old boxes
  until conversion completes. It invalidates each dirty DOM subtree, including
  hidden descendants. Nodes without a reusable result are selected normally;
  their own node cache is cleared first, since hidden elements can retain cache
  data despite having no box-owned result.
- The first native refresh is full and establishes the reuse snapshot. Later
  refreshes check DOM generations, stylesheet/import revisions, media and unit
  inputs, dimensions, font preferences/availability, base URL and visited-link
  history. Scripts-enabled or still-loading pages remain on the full path.
  Native text edits and option selection invalidate reuse when they happen.
- Between construction slices and before publication, changed inputs discard the
  partial attempt and restart full selection. Old presentation ownership remains
  intact until normal retirement. Table allocation failure falls back before
  detachment; clone allocation failure selects that element normally. Errors after
  detachment retain the existing conversion-error behavior, not a rollback promise.
- Closing a window cancels queued refreshes. Content with additional users may
  finish an active construction without a window; its next partial slice restarts
  fully. Active references survive until that restart/completion. Removing the
  last user cancels construction, clears update references and marks the incomplete
  presentation unusable. Destruction then owns both box contexts and resources.
  This also fixes immediate shutdown during the existing full rebuild path.
- Profiling distinguishes `partial_css` and `full`, counts fresh/reused results,
  references and restarts, and reports table capacity and cloned-container bytes.
  `--capture-restart-partial-restyle` injects invalidation during construction in
  profiling builds to exercise cleanup and compare with the full reference.

### Milestone A validation and measurements

Before rollout, the enabled Debug (ASan/UBSan) and Release builds each passed 113 selected CSS,
layout, Qt capture, form, DOM, URL-history and construction tests; the disabled
Release build passed all 111 applicable tests. The three focused
scope/sharing/tracking targets also passed with LeakSanitizer enabled. The small
differential fixture exercises normal reuse and a forced restart; the extensive
checked-state fixture also runs with restart injection to cover images and iframes.
Unit tests cover close bookkeeping, newer edits and input-generation changes.
The follow-up rollout checks below add deterministic allocation failures and
real window close/navigation between construction slices.

Saved-page checks used independent runs of the same Release/LTO binary, scripts
disabled, 1280x900, 96 DPI. All 13 states in each run matched across 930 Tion and
1,361 Bihon probe elements, including geometry, state, scroll and resource counts.
Final PNG/DOM output matched for **both** sidebar panels. Resource counts stayed
constant after the first reveal; warmed updates actually used `partial_css`, with
zero fallbacks or restarts.

The lightweight timing runs inspected only the two controls, avoiding retention
of hundreds of overlapping box-tree inspections. Each site used four fresh
processes in partial/full/full/partial order, 12 alternating switches per process;
the first two switches were excluded, leaving 20 samples per mode. No builds or
other regression jobs ran during measurement. Times exclude capture inspection,
quiet waits and artifact writing.

| Saved page | Full total median / p90 | Partial total median / p90 | Full / partial CSS median | Full / partial fresh selections | Reused results |
| --- | --- | --- | --- | --- | --- |
| Tion | 99.21 / 104.98 ms | 53.86 / 60.72 ms | 58.29 / 6.52 ms | 1,334 / 138 | 1,196 |
| Bihon | 150.81 / 155.96 ms | 81.93 / 88.41 ms | 82.24 / 9.61 ms | 1,919 / 196 | 1,723 |

These runs reduce median total refresh time by **45.7%** on both pages and CSS
time by **88–89%**. The exhaustive-probe runs also improved (93.93 to 43.98 ms on
Tion, 152.89 to 73.72 ms on Bihon), but retain substantial capture data and are
not the memory reference. Layout still runs in full and accounts for about 79%
of the lightweight partial-update time; its timings vary between runs, and this
milestone makes no claim of reduced layout work.

The temporary lookup vector peaks at **32 KiB** on each page and is released at
completion. Tion clones 239,200 bytes of result containers (1,254 style references)
and Bihon 344,600 bytes (1,785 references), sharing the existing property payloads.
Those containers replace the new containers normal CSS selection would allocate;
they are not a new permanent DOM cache. Whole-process peak RSS in the lightweight
captures was 106.7–107.2 MiB for both Tion modes, and 97.5–98.2 MiB across the Bihon
modes. This is comparable peak usage, not a precise allocation-difference estimate
or a long-running memory-plateau test.

Local drivers, reports and screenshots are under
`out/render-captures/partial-updates-2026-09-29/`; `lightweight/report.json` holds
the primary timing samples. Exhaustive inspection JSON is gzip-compressed to keep
the development artifacts small. These ignored artifacts are supplementary to
the repository fixtures and the measurements above.

### Milestone A rollout checks (2026-09-29)

The rollout passes 116 selected Debug tests under ASan/UBSan and 113 applicable
Release tests with `WISP_PROFILE_RESTYLE=OFF`, including the allocation-failure
cases below. The three focused scope/sharing/tracking targets also pass with
LeakSanitizer enabled. A fresh option evaluation selects partial updates ON;
profiling remains OFF by default.

Release allocator injection now checks failure of the first reuse-table allocation,
failure during later table growth, and failure to clone a result container. Both
table failures discard transient state and choose full selection; clone failure
leaves the old result untouched and asks construction to select that element
normally. The test restores allocation before making assertions. ASan interposes
allocation itself, so these failure cases run only in Release; sharing/lifetime
tests also pass separately under LeakSanitizer.

`qt_restyle_lifecycle` exercises real close/navigation after a construction slice
has already reused styles. With another content user present, the document safely
restarts fully and retires the old presentation. Two immediate-shutdown cases
drop the last user during selective and full construction. These exposed a locked
content destruction assertion: the new HTML `remove_user` hook cancels abandoned
conversion before cache destruction. All four modes pass under ASan/UBSan.

Long saved-page sequences run 300 alternating switches per site and mode: 1,200
switches across Tion/Bihon and selective/full selection. All inspected control
states, geometry, scroll and resource counts match, as do final PNG/DOM dumps.
Resource counts stay at 201 for Tion and 289 for Bihon after the first reveal,
with no active fetches at settled checkpoints. Every warmed selective update
uses reuse, with no fallbacks or restarts.

Those runs retain about 16 MiB of inspection JSON per process. Their RSS grows
in both modes and Bihon's selective run sits about 11 MiB higher, so they are
not a reliable renderer-only memory-plateau test. A second set of the same
sequences omits element inspections, retaining only small state/profile records;
it still checks resources, update modes and the final output against full selection.
Across these additional 1,200 switches, RSS reaches a stable level after warmup.
Late-run median RSS is 99.1 / 98.9 MiB for selective/full Tion and 97.1 / 96.7 MiB
for Bihon. The selective path is about 0.3–0.4 MiB higher in these runs, with no
continuing growth. This is process-level evidence over 300 switches per run,
not an exhaustive leak proof. The larger offset in the inspection-heavy run
does not persist in this lighter measurement.

Initial-load comparison uses the pre-selective-CSS foundation `fdd92975` and this
rollout, both Release/LTO with capture/profiling enabled, scripts off, 1280x900 and
96 DPI. Each site uses two discarded warmups followed by eight samples per binary
in repeated before/after/after/before order, without concurrent builds or tests.
Every PNG and DOM dump matches. The main metric is capture elapsed time minus
its final quiet interval: a proxy for time to last render activity, not a pure
parse/layout timer. Process CPU includes startup, artifact writing and shutdown.

| Saved page | Before / after last activity median | Before / after process CPU median | Before / after peak RSS median |
| --- | --- | --- | --- |
| Tion | 1,208.5 / 1,221.0 ms | 729.1 / 758.1 ms | 101.4 / 102.8 MiB |
| Bihon | 1,725.0 / 1,750.0 ms | 690.7 / 710.9 ms | 95.8 / 96.7 MiB |

The observed initial-load cost is about 1–1.5% to last render activity and 3–4%
process CPU, with roughly 1 MiB more peak RSS. This is a modest tradeoff for the
measured warmed refresh improvement, not zero overhead. Initial navigation still
uses full CSS selection.

Local rollout drivers, raw captures and reports are under
`out/render-captures/partial-rollout-2026-09-29/`; `initial/report.json` records
the initial-load samples, `stress/report.json` records inspected toggle sequences,
and `memory/report.json` records the lighter memory checks.
This completed the milestone A rollout. Subsequent milestone B work is recorded
below; partial layout remains deferred.

### Milestone B preparation: allocation ownership (2026-09-29)

The first refactor keeps whole-document construction and layout. Generated
content (`::before`/`::after`, text, images and inline-end markers) now uses the
allocation owner already carried by `box_construct_ctx`. Normalization receives
that owner explicitly and propagates it through block, inline, float, flex, grid
and table recursion. Anonymous wrappers therefore no longer silently allocate
under `html_content::bctx`. Current full construction passes the same context as
before, so allocation count, box size and tree ownership are unchanged.

The ownership regression uses real CSS selection, normalization and destructors.
It repeatedly builds/frees a separately owned region containing flex/grid text,
an orphan table cell, or a table inside a float. Anonymous wrappers must belong
to the region; freeing it must return the document's allocation count to its
baseline while an unaffected sibling and its shared styles remain usable.

Validation passes all 116 selected Debug tests (ASan/UBSan), all 113 selected
Release tests, and the focused ownership test with LeakSanitizer enabled. Both
saved sidebar panels on both sites match the pre-refactor captures: PNG and DOM
dumps are identical, complete box dumps match after removing pointer addresses,
and inspected state/geometry/resource counts match across the click sequence.
The local driver and captures are in
`out/render-captures/region-preparation-2026-09-29/`. This is an ownership refactor,
not a new performance claim or an enabled box-retention path.

The saved-page audit found that both `.tabs` roots are block-level **flex**
containers in floated sidebar columns (`probe-div-533` on Tion and `probe-div-655`
on Bihon). Replacing only the revealed panel would omit the radio/label changes.
A whole tab container is the useful boundary to investigate. Restricting roots
to `BOX_BLOCK` would widen Tion to its surrounding module and Bihon to its much
larger sidebar, including unrelated widgets and iframes. Supporting a whole flex
container here requires construction/ownership validation; it does not imply
flex layout independence or require changing the flex algorithm.

Remaining prerequisites before retaining boxes:

- Thread the owner through special-element construction: form controls, link
  targets, object parameters and iframe records. Framesets remain a full fallback.
- Route later text splits, numerical list-marker buffers and select-control text
  updates to their region. Do not discover the arena with `talloc_parent()` in
  hot loops: this bundled allocator walks preceding siblings to find the parent.
  Choose an explicit owner representation before enabling region allocation.
- Prove the complete boundary, including inline continuations, generated boxes,
  float chains and pointers to containing blocks. Stage DOM mappings, form lists
  and object/iframe bindings before publishing a replacement.
- Verify repeated full layout on retained boxes against fresh construction.
  Only then enable actual replacement, with its own measurements and fallback.

## Recommendation and scope

Start with **partial CSS selection for native checkbox/radio changes**, retaining
full box construction and full layout. Then measure before taking on retained box
regions and partial layout. These are separate milestones: the first can ship
without committing us to the more invasive work.

The immediate target is the CSS-driven sidebar tabs in the saved Tion and Bihon
pages. The implementation must use DOM/CSS relationships, not site names, element
IDs or assumptions about a particular sidebar. It should also work on small
standalone fixtures using the same selector patterns.

Rendering stays single-threaded, scheduled through the existing event loop. This
plan adds no JavaScript features, worker threads, general DOM mutation engine,
per-selector dependency graph, CSS bytecode redesign or rendering-engine rewrite.
Hover/focus, arbitrary script mutations and paint-only updates can be separate
extensions once the state-only path is correct.

The full refresh remains both a fallback and a test reference. Detailed existing
behavior and historical measurements remain in [checked-interaction.md](checked-interaction.md).
This document supersedes only its open-ended suggestion to investigate selective
invalidation; it does not replace that implementation record.

## Why this order

Recent profiling identifies CSS selection/composition and layout as the dominant
remaining costs. In the saved `selector-scratch-stack-{tion,bihon}-1/steps.json`
captures under `out/render-captures/checked-timing-2026-09-29/`, the median of
`boxes_ms / total_ms` over ten warmed switches is 3.82% for Tion and 3.59% for
Bihon. Thus "about 4%" refers to other active box construction as a fraction of
the complete refresh before painting, not of the construction phase alone.
Optimizing box allocation alone is not the first goal; retaining boxes becomes
useful mainly because it enables retaining layout results.

Historical complete-refresh medians started at about 332 ms for Tion and 599 ms
for Bihon. Several optimizations have already reduced those substantially. Use a
fresh baseline at the implementation commit, rather than treating measurements
from different runs in the history as a new controlled comparison. Do not promise
a particular millisecond result before measuring how much of each page is eligible.

| Milestone | CSS selection | Box construction | Layout |
| --- | --- | --- | --- |
| Current full refresh | Whole visible presentation | Whole document | Whole document |
| A: selective CSS | Dirty DOM subtrees; reuse other composed styles | Whole document | Whole document |
| B: retained regions | Dirty subtrees | Eligible replacement regions | Whole document |
| C: selective layout | Dirty subtrees | Eligible replacement regions | Affected formatting contexts and dependent ancestors |

The saved pages contain selectors of these forms, including nested ancestors:

```css
.custom-tab-1:checked ~ .custom-panel-1 { /* reveal panel */ }
.custom-tab-1:checked + .custom-tab-label { /* active label */ }
.tabsInput:checked + label + .offererContent { /* reveal panel */ }
.tabsInput:not(:checked) + label { /* inactive label */ }
input:checked + label i::before { /* generated indicator */ }
```

For supported selectors that only proceed toward descendants or following
siblings from the changed control, **the control's entire parent subtree is a
safe initial invalidation scope**. It includes both panels, labels, descendants
and pseudo-elements. It intentionally does more work than an exact selector
dependency system, while avoiding the unrelated article and page chrome.

## Existing code and constraints

| Area | Current implementation | Implication |
| --- | --- | --- |
| State changes | `src/content/handlers/html/form_state.c`, `interaction.c` | Activation reports each changed radio through a callback, including peers with different parents. |
| Refresh scheduler | `src/content/handlers/html/html.c`, `include/wisp/content/handlers/html/private.h` | Coalesces requests, clears presentation state, replaces the whole tree and retains the old allocation context until completion. |
| CSS integration | `src/content/handlers/css/select.c`, `select.h` | `nscss_get_style()` composes element/pseudo styles with parent/root inputs; `nscss_invalidate_document()` clears every node's libcss data. |
| Libcss cache | `contrib/libcss/src/select/select.c`, `select.h`, `variables.h` | Node data contains partial styles, bloom/sharing data and variable environments; it is not a cache of final composed box styles. |
| Box construction | `src/content/handlers/html/box_construct.c`, `box_normalise.c` | Selection, resources, gadgets, DOM mappings and normalization are coupled to construction. `dom_to_box()` is not a ready-made subtree replacement API. |
| Box lifetime | `src/content/handlers/html/box_manipulate.c`, `include/wisp/content/handlers/html/box.h` | Element boxes own `css_select_results`; generated/anonymous boxes may borrow styles. DOM and box trees are not one-to-one. |
| Layout | `src/content/handlers/html/layout.c`, `layout_internal.h` | `layout_document()` performs intrinsic sizing, block layout, list placement, absolute/relative positioning and descendant bounds. |
| Instrumentation | `WISP_PROFILE_RESTYLE`, Qt capture tools | Existing timing, state, geometry and image outputs provide the starting point. |

Important consequences:

- A `display:none` element has no retained normal box style, and its descendants
  may never have been selected. Revealing it must perform fresh selection.
- Invalidating one libcss node does not invalidate its descendants or siblings.
  The caller must walk the chosen scope. The existing taint flags describe style
  sharing safety; they are not a complete invalidation dependency model.
- Custom-property environments can change even when the parent's visible
  properties do not. Descendants and pseudo-elements must see the new environment.
- The current page-wide talloc context, `bctx`, owns allocations made by
  construction **and layout**, including split text boxes and list-marker text.
  Moving one box pointer does not make its allocation lifetime independent.
- Flattened inline fragments, floats, anonymous table boxes and generated content
  can cross a naive DOM subtree boundary.
- `WISP_ENABLE_INCREMENTAL_REFLOW` controls progressive reflow while resources
  load. It is not selective subtree layout and must not be repurposed for this work.

## Rules shared by every milestone

1. Record every successfully changed control, including the formerly checked
   radio, before discarding activation details. A failed final mutation must not
   hide an earlier successful peer change.
2. Coalesce updates using referenced DOM nodes, not raw box pointers. Keep pending
   requests separate from the batch being processed; a broad request overrides
   narrower pending work.
3. Unknown dependencies mean wider invalidation or a full refresh. Never retain
   stale output because recording a dirty node or allocating bookkeeping failed.
4. Initially enable the partial path only for native state changes with scripting
   disabled and a stable, completed initial presentation. Scripts-enabled pages
   retain the full path until mutation tracking is explicitly implemented.
5. Changes to stylesheets/imports, media/viewport inputs, font availability,
   document structure or other selector-visible state invalidate reuse eligibility.
   This includes form edits that change text children or attributes. Record them
   when they occur, even before a checked-state refresh is requested; inspecting
   only mutations during refresh cannot establish that the old styles are valid.
6. No paint, hit testing or interaction may observe a half-published tree. Resource
   completions and resize requests must be deferred or incorporated before the
   update is published. Closing/navigating cancels queued work; an active rebuild
   with remaining users may finish safely without a window. Removing the last
   user must cancel it before destruction, releasing all update references.
7. Use the same settled output as a full refresh for the same DOM state and inputs.
   Preserve existing allowances for Qt-versus-Chromium font differences; those do
   not justify discrepancies between Wisp's own full and partial paths.

## Phase 0: establish a full-refresh reference

**Deliverable:** a repeatable comparison and work counters, with no rendering change.

1. Add the CMake option `WISP_ENABLE_PARTIAL_UPDATES`, initially OFF during
   development and now ON after milestone A validation. Keep the existing full
   path intact. Allow captures in an enabled
   build to force that full path through a separate runtime capture flag, so the
   same binary can test both implementations. Gate that override with the existing
   capture/development facilities.
2. Extend `WISP_PROFILE_RESTYLE` and capture JSON with update mode, fallback reason,
   dirty roots, invalidated nodes, freshly selected elements, reused styles and
   temporary bookkeeping bytes. Add rebuilt/retained box and layout-work counters
   only when those later phases exist. Keep instrumentation compiled out normally.
3. Extend `frontends/qt/test_checked_interaction.py`, or extract its driver for use
   by a companion differential test. Replay identical actions in independent full
   and partial runs. Verify load completeness and selected state before comparing
   geometry/images; do not compare pointer identities or elapsed times.
4. Capture both saved sites at 1280x900 with scripts disabled and identical fonts,
   adblocking, resources and scale. Compare every settled panel state, not just the
   last screenshot. Use the existing Chromium comparison for disputed behavior.

**Gate:** the forced-full path reproduces the current tests and captures. Capture
failures identify the action, update mode, element and differing field. Tests must
assert from the recorded update mode that a later partial run actually used the
partial path; the compile-time option alone is insufficient. Fallback must not
silently make all comparisons pass.

## Phase 1: record changes and compute conservative CSS scopes

**Deliverable:** correct invalidation scopes, initially still executed as a full refresh.

### 1.1 Preserve change identity

- Extend the internal activation interface in `form_state.h/.c` to report each
  successful checkedness change to its caller. A callback into a small update
  collector is sufficient; avoid allocating a new list for every radio group.
- Only report peers whose checkedness actually changed. Handle unnamed radios,
  form ownership, disabled controls and no-op clicks as today.
- Replace the indexed `dom_nodelist_item()` peer loop with one document-wide
  `libdom_iterate_elements()` traversal, filtering inputs by radio type, name and
  resolved form owner. Indexed lookups restart traversal and can make enumeration
  quadratic. Do not restrict the walk to the form subtree: an input outside it
  can share its owner through `form=`. Preserve that existing behavior and test it
  in both activation directions, including reporting the external peer's change.
- Introduce a state-specific request alongside `html_request_restyle()`, for
  example `html_request_checked_restyle(html, node)`. Keep the existing function
  as the explicit whole-document fallback. Names are proposals, not existing APIs.
- Store a small, bounded collection of referenced nodes/roots on `html_content`.
  Deduplicate overlapping roots; if collection capacity or allocation fails, set
  the full-refresh bit. Release references on completion, cancellation and close.
- Preserve click/input/change event ordering and scheduling after the input
  callback. Do not turn the collector into a synchronous layout call.

### 1.2 Prove the selector scope

- In libcss, classify the **parsed selector representation**, including selectors
  in imported sheets. Do not search CSS source text or inspect only rules that
  matched the previously checked state.
- The initial supported class is checked-state tests (including `:not(:checked)`)
  whose effect propagates only through ordinary descendant, child, adjacent-sibling
  and general-sibling relationships toward the selected element/pseudo-element.
  The invariant is: from the compound testing the changed control, every step
  toward the selector's subject stays inside that control's parent subtree.
  Ancestor conditions to the left are allowed. The current parser accepts one
  simple selector inside `:not()`, not a compound or complex selector, so
  `:not(:checked)` is a test on that same node. Nested `:not()` is rejected.
  Unknown functional/relational forms fail closed; an ancestor-targeting selector
  would invalidate the simple parent-subtree proof. `:has()` is not currently
  supported. Revisit eligibility when selector semantics change, including when
  implementing currently unmatched parser-accepted pseudos such as `:host`.
- Cache a small eligibility summary per stylesheet/selection-context revision,
  not a rule-to-element graph. Include all origins and imports. Recompute or
  conservatively invalidate it after sheet changes; media changes require a full
  update initially. A selector that the parser discards cannot affect rendering,
  but a newly supported selector kind must default to ineligible until classified.
- For every changed control, collect its parent subtree. Union scopes for both
  the old and new radio, which may have different parents. Widen to the document
  when necessary. An unsupported scope simply uses the existing full refresh.
- Add a subtree invalidation wrapper beside `nscss_invalidate_document()` using
  the existing linear DOM iterator. Invalidate all cached nodes in each scope,
  including hidden descendants. Reselect in ancestor-before-descendant order.

Start broad within these roots: do not try to remove unchanged-looking descendants
by comparing a few computed properties. This preserves inheritance, custom-property
environment changes, generated content and selectors that start or stop matching.

The same scope also covers custom properties. With the supported forward selector
relationships, a control's checkedness cannot change a custom-property declaration
on an ancestor. It can change declarations on the control, following siblings
(for example `.tab:checked ~ .panel { --x: ... }`) and their descendants. All are
inside the parent subtree; inherited environments outside it remain valid when
no other selector-visible inputs changed. Live checkedness does not modify the
`checked` attribute, so the toggle itself cannot change `[checked]` matching.

**Tests:** extend `src/test/form_state_test.c`, `src/test/css_checked_test.c` and
libcss selection tests. Cover two radio parents, externally associated form
controls, a linear peer traversal, multiple coalesced changes,
`:not(:checked)`, `+`, `~`, descendant chains, imports, pseudo-elements, unsupported
selector fallback and failure after a peer has changed. Check the actual dirty
scope, including a same-class element elsewhere that must remain outside it.

## Phase 2: reuse unaffected composed styles (milestone A)

**Deliverable:** partial CSS work with the existing whole-document reconstruction.

### 2.1 Add safe result sharing

- Add a public result-container clone API in `contrib/libcss/include/libcss/select.h`,
  with its implementation inside libcss's `src/select/select.c`. A proposed name
  is `css_select_results_clone()`. It allocates a new container and references
  each computed style using the private `css__computed_style_ref()` mechanism;
  the HTML layer must not include internal headers or manipulate refcounts.
  Arena interning already shares identical computed styles, so this extends an
  existing ownership model rather than copying their property storage.
- Include every `CSS_PSEUDO_ELEMENT_COUNT` entry. Audit that final composed styles
  are immutable after publication, including internal composition and arena paths.
  Current composition writes a new result and computes absolute values before
  interning it; there is no public property setter used by layout. This does not replace the
  internal audit. Test release of the old and new containers in both orders,
  including dynamically stored backgrounds, gradients, grid tracks, transforms
  and generated content.

### 2.2 Use a transient reuse table, not a permanent DOM style cache

1. Before detaching the old presentation, collect available composed result sets
   outside the dirty scopes into a temporary node-to-result table. Borrow the old
   results while the old box context remains alive; do not extend the table beyond
   that lifetime. Old boxes also keep the node identities alive.
2. Invalidate libcss node data only in the dirty scopes. Preserve valid node data
   elsewhere so inheritance and sharing retain their normal inputs.
3. Run the existing full construction. At `box_get_style()`/its caller, reuse an
   eligible old result via the reference-sharing API; otherwise select and compose
   normally. A missing entry, newly visible node or hidden node without a saved
   style always uses normal selection.
4. Recompute box types, inherited opacity flags, gadgets and resource bindings
   through normal construction even when CSS results are reused. The cache stores
   styles, not arbitrary box state.
5. Perform full normalization and `layout_document()`. Destroy the transient table
   before retiring the old context. No new per-element cache survives the update.

The table adds temporary memory proportional to the available element styles, on
top of the current overlap of old/new trees. Record table bytes, cloned-container
count/bytes and style-reference increments separately, as well as total peak
memory and initial-load overhead. A reference increment adds no allocation of its
own. Do not add a fixed allocation to every page. The table disappears when
retained regions make whole-document style transfer unnecessary.

On the current 64-bit build, a result container is 25 pointers, or 200 bytes.
An old container plus its clone therefore occupies 400 bytes per reused element,
excluding allocator overhead and the shared style payload. Cloning 1,500–2,100
containers would add 0.30–0.42 MB compared with transferring those containers;
both sets would occupy 0.60–0.84 MB. These are illustrative upper estimates from
page element counts, not measurements of eligible reuse. The existing full rebuild
already overlaps old/new containers, so cloning is not a new doubling relative to
that baseline. Keep cloning initially; consider ownership transfer only if actual
measurements justify its additional coupling to old-box destruction.

### 2.3 Track changes before and during construction

Eligibility includes unchanged stylesheet/font/viewport/document inputs since
the styles being reused were produced, through snapshot creation and publication.
Start with conservative change tracking, not a collection of speculative caches.
Wire every relevant existing change source to invalidate reuse; if a source cannot
be tracked, exclude that situation.

For form edits, the initial rule is explicit: **a value change that can affect
selector matching requires a full refresh at the next scheduled state update**.
Record that requirement when the edit changes the DOM, using the shared
`form_gadget_update_value()` and synchronization paths in `form.c` and their
callers such as `box_textarea.c`. Typing already writes through these paths;
waiting until `html_restyle_detach_element()` would miss changes synchronized
before the tab click. Check mutations during detachment as well, before any saved
styles are consumed.

The concrete dependencies are textarea text children (`:empty`) and input `value`
attributes (`[value]`, `[value="x"]`). A textarea edit does not change its
ancestors' own emptiness, but changed matching can affect other elements through
selectors such as `textarea:empty + .message` or `input[value="x"] ~ .panel`,
including their inherited styles and custom properties. Dirtying only the edited
control would therefore be insufficient.

Clear the full-refresh requirement only after a successful full refresh that
includes those edits. Use the update generation to retain any newer edits queued
during conversion; an older completion must not clear them. Unchanged value
synchronization must not continually disable reuse. Subsequent unchanged tab
switches can return to the partial path. This does not require introducing
immediate general restyling on every keystroke.

Add regressions that edit an input/textarea outside the tab scope, complete DOM
synchronization before clicking a tab, and verify that the next refresh is full
and updates both the control and selector-dependent siblings. Check that the
following unchanged switch is partial, and that failure/cancellation does not
clear pending invalidation.

If eligibility is lost between scheduled construction slices, restart through an
explicit full-refresh cleanup path without publishing the obsolete partial result.
Do not merely clear a flag after some styles have already been reused. Keep newly
queued state changes pending, and retain existing resource/iframe lifetime rules.
Allocation failure while preparing the table can choose full refresh; failures
after detachment need the existing conversion error handling or an explicitly
tested restart, not a claimed rollback that the current code does not provide.

**Gate:** identical full/partial settled output; inherited variables and pseudo
styles update correctly; resource counts remain stable. Warmed Tion/Bihon switches
actually skip selection for unrelated main content. Report the selected/reused
element counts, CSS time, total refresh time and peak memory. If bookkeeping costs
outweigh the saved work, simplify or stop here before changing box ownership.

## Phase 3: replace safe box regions, retain full layout (milestone B)

This is the larger ownership change. Begin only after milestone A is validated
and measured. Its purpose is to enable later layout reuse, not to chase the small
box-allocation percentage alone.

### 3.1 Define an eligible replacement boundary

Select a stable ancestor containing the entire CSS dirty scope and its generated,
anonymous and flattened boxes. Initially require a closed, ordinary block region:
no inline continuation or normalization dependency crossing its boundary, no
document root/frameset, and no external borrowed storage whose lifetime replacement
would break. Otherwise widen or fall back. A hidden/revealed panel's own DOM node
is not automatically a safe boundary; the persistent parent may be the boundary.

Flex/grid/table participation does not imply local layout independence. Initially
widen through unsupported construction/normalization cases; do not rewrite their
layout algorithms just to make more regions eligible. Record fallback reasons and
whether the saved sidebar structures actually qualify.

### 3.2 Introduce region ownership on a full construction first

- Use the first eligible full reconstruction to establish talloc parent contexts
  at the chosen replacement roots. Subsequent toggles can replace those regions.
  Regions need not exist for every block or on pages that never use this path.
- Thread an explicit construction allocation owner through helpers that currently
  reach directly for `content->bctx`. Route later text splits, list-marker buffers
  and other layout allocations to the region owning their source box.
- Keep a small record for each prepared boundary: DOM identity, box boundary,
  allocation context and validity generation. Do not add an arena per box.
- Audit inherited strings, style references, `CLONE` text boxes, inline-end links,
  float chains, gadgets, scrollbars, form lists and object/iframe associations.
  Retained boxes must not point into a retired region. A new box must not borrow
  unowned storage from the region it replaces.
- Audit destructor versus explicit-free paths and cancellation together. Normalize
  ownership only where needed for safe retirement; avoid unrelated cleanup.

Retirement means `talloc_free(region_ctx)` and running its descendants' destructors.
Before doing so, no retained box, DOM mapping, interaction state or resource
association may point to an allocation under that context. Check non-talloc
resources released by those destructors too. This is the concrete lifetime
condition behind the region audit.

### 3.3 Extract a bounded builder and publish replacement safely

Extend `box_construct.c`/its internal interface to accept a subtree boundary,
inherited parent/root styles, containing-block/inline context, allocation owner
and explicit traversal stop. Reuse normal element conversion and normalization;
do not create a second implementation of CSS-to-box rules.

Build into a detached region. DOM mapping writes, form registration, object-to-box
bindings and iframe rebinding must be staged rather than overwriting live links
during construction. Resolve and validate the replacement before unlinking the old
region. On success:

1. Clear or remap interaction state that points into the replaced region. Retain
   unaffected state where ownership permits; never leave a stale selection, caret,
   search, drag or select-menu pointer.
2. Splice the box region and publish mappings/resource bindings together while
   input and painting are excluded. Preserve existing resource handles and iframe
   browsing contexts, including hide/show behavior.
3. Reset dependent layout caches and run the **full** layout pipeline, including
   positioning, bounds and scrollbar updates. Retire old storage only once no
   live associations reference it.

Before publication, a failed build can discard the detached result and choose
full refresh. After publication, failure recovery must enter a tested full rebuild
or normal content-error path; do not promise to restore an already freed tree.

**Gate:** repeated layout of retained boxes matches fresh full reconstruction.
Exercise shrinking/widening, relative offsets, float chains, text split clones,
list numbering, pseudo content, replaced objects and scrolling bounds. Hundreds
of alternating switches must reach a memory/resource plateau. If a safe boundary
requires broad restructuring, retain milestone A and defer this phase.

## Phase 4: skip proven-clean layout work (milestone C)

**Deliverable:** one narrow, measurable layout reuse path, with full layout fallback.

1. Separate intrinsic-size validity from used-layout validity at retained region
   roots. A changed subtree invalidates its ancestors' dependent min/max results;
   a parent allocation change can invalidate a previously clean child's layout.
2. Begin with ordinary block formatting contexts whose external inputs can be
   compared explicitly. The initial key must include available/containing-block
   dimensions and definiteness, relevant styles/units, viewport/font generation
   and intrinsic resource sizes. A formatting context alone does not prove that
   its size is independent. Exclude incoming float effects, escaping margin
   collapse and other dependencies until accounted for.
3. Reuse an eligible region's internal layout only when its inputs and content
   are unchanged. Let the parent perform normal placement of siblings and margin
   collapse. Do not implement a blanket vertical-delta translation of all later
   boxes; floats, percentages, positioned descendants and overflow can invalidate it.
4. Initially relayout the whole owning flex, grid or table context when an item
   changes. Likewise, changes to floats/clear, absolute containing blocks, list
   counter sequences or viewport-fixed content widen the required work. The
   closure may legitimately reach the document root.
5. Keep page-wide final positioning/bounds passes until skipping each is separately
   justified. Validate descendant overflow, document extent, scroll clamping,
   iframe geometry and hit testing, not just panel width/height.
6. On resize, late font/image load or unsupported layout conditions, run full
   layout and invalidate/refresh cached input signatures. Layout fallback need
   not discard valid styles; choose the smallest fallback that is proven safe.

**Gate:** settled geometry, line wrapping and pixels match full reconstruction
after long toggle sequences and input changes. Counters must show fewer intrinsic
and full-layout visits on the saved pages, not just fewer font API calls. Measure
the final settled pass and report any additional passes separately. Keep a fully
uncached reference run; running both variants sequentially on the same mutated box
tree is not an independent correctness check.

## Generators and CSS data ownership

The first milestone needs result-reference sharing and conservative invalidation,
not a per-property visual-difference classifier or new bytecode representation.
This keeps the initial change smaller and avoids hand-maintained property lists.

If later measurements justify style equality or paint/layout effect classification:

- Use `contrib/libcss/properties.toml` as the property metadata source, together
  with `src/select/select_generator.py`, `select_config.py`, `assets.py` and
  `overrides.py` under that library. Registration generation also uses
  `contrib/libcss/build-aux/property_generator.py`; audit the CMake dependencies.
- Modify generator inputs/templates, never just emitted headers under a build
  directory. Verify from a clean generated build.
- Default unclassified properties to the conservative rebuild/layout behavior.
  Add validation that newly registered properties cannot silently become no-ops.
- Account for manually stored and dynamic members: background layers, gradients,
  tracks, transforms, content, strings and calculations. Whole-struct `memcmp`
  is not semantic equality. Internal arena interning/equality must be audited
  before using it as a public correctness contract.
- Track variable-environment validity independently of visible-property equality.
  Equal currently used values do not prove that descendants' `var()` inputs match.

Existing refcounts and mutable layout state remain single-threaded. Explicit
ownership and update batches are useful preparation for future parallelism, but
do not make this implementation thread-safe or eliminate future synchronization
work. The CSS and layout correctness tests remain useful under either execution model.

## Validation and measurement checklist

| Category | Required coverage |
| --- | --- |
| Native behavior | Explicit/implicit labels, disabled input/fieldset rules, external `form=` ownership, both radio peers, no-op clicks and unchanged default attributes. |
| Dependency scope | `+`, `~`, descendants, negation, different radio parents, imports, pseudo content, root/global changes and text/attribute edits affecting siblings outside the tab scope. |
| Inheritance | Custom-property change with unchanged parent geometry; nested `var()` fallback; font/color inheritance; hidden descendants revealed later. |
| Lifetime | Edits synchronized before the update, newer edits during conversion, pending invalidation after failure/cancellation, close/navigation between slices, allocation failure, resources completing while hidden or mid-update, iframe reuse. |
| Layout | Existing flex/grid/table/float/absolute cases, relative positioning, split inline text, margins, list counters, overflow/scroll range and hit testing. |
| Repetition | Coalesced requests, repeated hide/show, resizing between toggles, cold first reveal versus warmed switches, bounded memory/resource use. |

Extend the existing checked-state fixtures and meaningful layout regressions;
avoid creating tests that merely mirror a private helper. Register new tests in
the relevant existing CMake files. Run the CSS, layout and Qt capture suites in
Debug ASan/UBSan and Release, plus clean generator builds when generators change.
Use focused LeakSanitizer tests for newly owned allocations; the broad suite has
known pre-existing cleanup leaks that must not conceal a new leak.

Current local build directories are `build-css-vars` and `build-css-vars-release`.
The local Check installation is under `/tmp/wisp-check-install`; if still needed,
set `LIBRARY_PATH` for builds and `LD_LIBRARY_PATH` for tests accordingly. Example:

```sh
LIBRARY_PATH=/tmp/wisp-check-install/lib cmake --build build-css-vars-release
LD_LIBRARY_PATH=/tmp/wisp-check-install/lib \
  ctest --test-dir build-css-vars-release --output-on-failure \
  -R '^(libcss_|css_.*test|layout_.*test|qt_)'
```

Let Ninja choose its default parallelism. Build and test both feature-enabled and
feature-disabled configurations before enabling a milestone by default.

For timing, follow [the capture profiling protocol](../../frontends/qt/CAPTURE.md).
Use Release/LTO, optional restyle counters and no trace logging. Warm both panels,
interleave full/partial runs, exclude capture quiet time and artifact I/O, and keep
builds or unrelated CPU-heavy jobs out of the measurement. Report medians and p90,
phase times, work counts, fallback frequency and peak temporary/retained memory.
Separate cold resource fetches from warmed refreshes. Timing is evidence, not a
flaky hard-coded CTest threshold.

## Suggested commit sequence and stopping points

1. **Reference capture and counters:** forced-full comparison, no behavior change.
2. **Change collection:** all radio peers through a linear document traversal,
   coalescing and tracking non-checked changes when they occur.
3. **CSS scope classification:** parsed-selector eligibility and subtree invalidation,
   validated while full refresh remains the default.
4. **Style-result sharing:** ownership API and lifetime tests.
5. **Milestone A:** transient style reuse, hidden-node handling and restart rules;
   measure and document results before proceeding.
6. **Region preparation:** explicit allocation owners and bounded construction,
   still producing a full presentation; verify existing output is unchanged.
7. **Milestone B:** staged region replacement with full layout; lifetime and repeated
   relayout tests, then measure again.
8. **Milestone C:** one eligible clean-layout reuse path plus dependency propagation;
   expand only when measured fallback patterns justify it.
9. **Rollout:** enable validated behavior by default, retain the full fallback and
   developer comparison switch, and update the behavior/profiling documentation.

Commits 1–5 and the milestone A rollout are complete. Step 6 has started with
explicit allocation propagation through generated content and normalization.
Steps 7–8 still require their ownership and layout gates; they are not
prerequisites for the already enabled selective CSS work.
