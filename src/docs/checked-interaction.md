# Native checked-state interaction

Radio/checkbox CSS tabs work without JavaScript. This is a document-state
refresh, separate from parsing and page loading; it is not general dynamic DOM
or JavaScript application support.

## Activation and selection

The mouse path resolves an input or its explicit/implicit label through the
DOM, including `display:none` controls. It honors disabled inputs and disabled
fieldsets (including the first-legend exemption). Radio groups use case-sensitive,
nonempty names and form ownership, including formless and explicitly associated
controls. Live checkedness does not change the default `checked` attribute.

The hit-test result retains label candidates like links and gadgets. Text boxes
retain their DOM owner because inline layout flattens nested elements into
siblings; this reference does not make text an element box for CSS transforms
or painting. When checkable candidates overlap, their stacking levels keep a
transparent input from intercepting a label painted above it. This does not
redesign general hit-test stacking order.

Activation dispatches click events, then updates state if not cancelled, and
emits input/change on a change. It schedules rendering after the mouse callback
returns. Multiple requests before that callback runs are coalesced. Full HTML
legacy pre-click activation/cancellation semantics for script listeners,
programmatic clicks, keyboard radio-group navigation, and form reset are separate
work; this change does not claim to implement them.

## Presentation lifetime

`html_request_restyle()` schedules a refresh on the existing main event loop:

1. Invalidate all libcss per-node selection data, including custom-property
   environments and sharing caches. Keep the stylesheet selection context.
2. Clear box-owning selection, caret/focus, drag, search and select-menu state.
   Input values live in the existing DOM. Drop old gadget objects and DOM-to-box
   mappings, including descendants that may become hidden.
3. Construct a new box tree from the same document. Construction still yields
   through the existing scheduler. Painting/input cannot use a partial tree.
4. Reuse resource handles, lay out the new tree, rebind iframe windows, retire the old
   allocation context, and broadcast a reformat. The window retains its scroll
   offset, subject to ordinary clamping if the document becomes shorter.

Cache invalidation and gadget/box detachment each use a single depth-first DOM
walk, including hidden descendants. The shared iterator uses constant storage
without recursion and reads children after its callback, so textarea value
synchronization may replace text children safely.

There is no navigation, stylesheet reload, parser restart, or replay of page-load
scripts/events. Fonts and inline SVG source caches also survive. Destruction
cancels both queued refreshes and in-progress box conversion.

Objects keep a DOM identity/request URL and cache handle while their box is
absent. Reappearing images, backgrounds (including multiple layers), SVGs, and
embedded objects reuse those handles. Anonymous generated images can reuse the
same unbound URL/type/layer entry. First-time content in a revealed panel may
start new fetches; completion schedules layout through the existing deferred
reformat path. Hidden-resource completion has no box to redraw.

Iframe window addresses remain stable. Their array reserves space for the
existing document's iframe elements, including initially hidden ones, but only
visible frames get initialized/loaded. A hidden frame keeps its browsing context
and is rebound when shown. Arbitrary DOM insertion/removal of iframe elements is
outside this state-only refresh contract.

This deliberately rebuilds the whole presentation. It costs one CSS selection,
box-construction and layout pass per changed control; retaining the old allocation
context until rebinding also temporarily increases peak box memory. Selective
invalidation can be added at this boundary if measurements justify it. Neither
parallel rendering nor a new dependency graph is required now.

## Measured refresh cost (2026-09-29)

Release + LTO, 1280 × 900, scripts disabled, Intel Xeon E5-2697A v4. Each
page was loaded in three fresh processes; each process performed 12 alternating
native label clicks. The first two clicks warm both panels and are excluded,
leaving 30 samples per page. Selected-panel state, geometry and stable resource
counts were checked throughout. No compiler jobs ran alongside timed captures.

The optional `WISP_PROFILE_RESTYLE` counters use monotonic wall-clock timestamps
and aggregate in memory. These internal durations exclude the capture quiet
period, inspection and artifact writing. Baseline medians before replacing the
indexed DOM enumeration loops, in milliseconds:

| Wisp phase | Tion | Bihon |
| --- | ---: | ---: |
| Invalidate CSS caches, including DOM enumeration | 62.4 | 133.7 |
| Detach old gadgets and box mappings, including DOM enumeration | 63.7 | 137.3 |
| Element CSS selection/composition, including inline style parsing | 91.5 | 151.2 |
| Other active box construction | 3.53 | 5.63 |
| Box normalization | 0.045 | 0.090 |
| Scheduler/callback gaps during construction | 0.005 | 0.009 |
| Layout/reformat | 110.3 | 172.0 |
| Retire old boxes, rebind iframes and publish reformat | 0.78 | 1.11 |
| **Complete refresh, before painting** | **331.7** | **599.2** |
| Subsequent core HTML drawing (one call) | 3.54 | 4.75 |
| Refresh start through the end of that draw | 337.4 | 606.2 |

Medians of individual phases do not necessarily add to the median total. The
90th-percentile refresh times were 341.9 and 620.3 ms. Tion reselected styles for
1,327–1,341 elements per click; Bihon for 1,912–1,926, depending on the active panel.
The construction phase's large inclusive duration is mostly CSS selection; it
should not be interpreted as box allocation/structure overhead.

### Repeated DOM enumeration

Both the cache-invalidation loop and the old-box-detachment loop originally called
`dom_nodelist_item(list, i)` for every element. Libdom's implementation in
`contrib/libdom/src/core/nodelist.c` restarts at the root for each index. Enumerating
all indices therefore does quadratic traversal work. Both loops now use
`libdom_iterate_elements()` for linear traversal through the public DOM APIs.

An isolated benchmark parsed each saved document and compared indexed enumeration
with one depth-first walk through the public DOM APIs. Both methods found the
same element count. Seven repetitions, median milliseconds:

| Page | DOM elements | Indexed NodeList enumeration | Single tree walk |
| --- | ---: | ---: | ---: |
| Tion | 1,493 | 66.88 | 0.111 |
| Bihon | 2,099 | 136.77 | 0.176 |

This microbenchmark isolates enumeration; the renderer measurements below include
the actual cache invalidation and gadget detachment work as well.

### Renderer after linear traversal

The same Release configuration and 30 warmed samples per page, after replacing
both indexed scans with the shared iterator. Median milliseconds:

| Phase | Tion before | Tion after | Bihon before | Bihon after |
| --- | ---: | ---: | ---: | ---: |
| Invalidate CSS caches | 62.40 | 0.70 | 133.71 | 1.11 |
| Detach old gadgets and box mappings | 63.72 | 0.25 | 137.33 | 0.37 |
| Combined invalidation + detachment | 125.09 | 0.96 | 268.94 | 1.47 |
| CSS selection/composition | 91.55 | 89.77 | 151.23 | 151.66 |
| Layout/reformat | 110.29 | 109.41 | 172.01 | 171.53 |
| **Complete refresh, before painting** | **331.72** | **205.08** | **599.23** | **332.51** |

Combined rows are medians of per-sample sums. Total refresh time fell by 38.2%
for Tion and 44.5% for Bihon; the 90th percentiles fell to 213.2 and 348.1 ms.
Refresh through the end of drawing fell to 210.8 and 339.4 ms. CSS selection and
layout remain whole-document work and account for almost all the remaining cost;
the traversal fix does not introduce selective invalidation or parallelism.

All six final screenshots and DOM dumps are byte-identical to their baseline
captures. All 13 inspected states per run match the baseline panel geometry,
control state and scroll offsets; resource counts remain stable. The focused
DOM/CSS/form/box and Qt capture tests pass in Debug (ASan/UBSan) and Release.
The traversal unit tests also pass with LeakSanitizer enabled.

### Avoiding empty variable-collection passes

When any stylesheet uses variables, selection first collects custom properties,
then cascades ordinary properties. The collection pass previously searched every
stylesheet's selectors even when that sheet contained no custom properties or
`var()` references. `select_from_sheet()` now skips that work using the parser's
existing `uses_variables` flag. Imported sheets are still visited independently;
ordinary declarations still run in the normal cascade pass. No generator,
bytecode, storage or cache-invalidation changes are needed.

Repeating the same 30-sample protocol after this change gives:

| Phase | Tion after traversal fix | Tion now | Bihon after traversal fix | Bihon now |
| --- | ---: | ---: | ---: | ---: |
| CSS selection/composition (ms) | 89.77 | 63.85 | 151.66 | 106.20 |
| Layout/reformat (ms) | 109.41 | 109.31 | 171.53 | 174.45 |
| **Complete refresh, before painting (ms)** | **205.08** | **177.19** | **332.51** | **287.01** |

CSS selection fell by 28.9% and 30.0%; total refresh fell another 13.6% and
13.7%. All six screenshots and DOM dumps remain byte-identical, as do the
inspected control states, panel geometry and scroll offsets. All 64 Debug CSS/
checked-state/capture tests pass, plus 14 focused Release tests. The new variable
test covers imports through a literal-only sheet, pseudo-element inheritance,
and the outer sheet's ordinary declarations overriding imported declarations.

CPU sampling beginning at the first native state change identified two further
candidates: Qt repeatedly resolves font families on font-cache misses, and the
selector hash keeps only 64 buckets each for classes, IDs and element names.
The font lookup and selective class-table growth changes are recorded below.

### Qt font family lookup

Temporary frontend probes counted only work inside `layout_document()` after
the first native state change. One process per page, 12 switches including
warm-up; these are averages per layout and include probe overhead:

| Operation | Tion | Bihon |
| --- | ---: | ---: |
| Complete-font cache queries | 14,173 | 20,247 |
| Complete-font cache misses | 118 | 168 |
| Family-resolution calls | 132 | 181 |
| Family resolution time (ms) | 51.5 | 71.2 |
| Text-advance measurement time (ms) | 42.1 | 69.7 |
| Ascent/descent queries | 11,805 | 16,493 |

Only seven family names were resolved on either page. The complete-font cache
already hits on over 99% of queries, but a miss repeats family discovery for a
new size, weight or family-list identity. A separate 64-entry LRU now caches
family-name/slant resolution, including missing families, using owned Qt strings.
It preserves CSS fallback order and the existing weight, size and shaping paths.
Both caches clear on Qt font-database changes and after Wisp registers a web-font
alias. Slot ages reset along with the complete-font cache's global age counter,
so empty entries are reused immediately after clearing.

The temporary timing probes are not part of the shipped code. Existing tests
cover aliases, italic foundries and late web fonts; new cases also exercise
cached missing names across sizes and application-font addition/removal.

With the probes removed, the same three-process, 30-warmed-sample measurement
gives these medians in milliseconds:

| Phase | Tion before | Tion now | Bihon before | Bihon now |
| --- | ---: | ---: | ---: | ---: |
| CSS selection/composition | 63.85 | 63.32 | 106.20 | 109.07 |
| Layout/reformat | 109.31 | 61.29 | 174.45 | 104.27 |
| **Complete refresh, before painting** | **177.19** | **130.14** | **287.01** | **222.57** |
| Subsequent core HTML drawing | 3.63 | 1.06 | 4.83 | 0.98 |

Layout fell by 43.9% and 40.2%; total refresh fell by another 26.6% and 22.5%.
All six screenshots and DOM dumps are byte-identical to the previous version,
and all 78 inspected states preserve geometry, control values, scroll and resource
counts. Seven focused font/text/capture tests pass in both Debug (ASan/UBSan) and
Release. The earlier cache-only trial measured 130/219 ms; the final run above
also includes the complete-font cache age reset. Small differences between the
runs, especially in unchanged CSS work, are normal timing variation.

CSS selection and layout now take roughly equal shares. Repeated text-advance
measurement and ascent/descent queries remain candidates for the next focused
investigation; changing shaping or line-breaking behavior is not part of this
cache fix.

### Repeated inline baseline measurements

Inline layout measures baselines before choosing float boundaries, again after
wrapping, and while positioning the resulting boxes. Those passes often request
the same computed style and height. A four-entry stack cache now reuses these
results within one `layout_line()` call. It includes height in the key to preserve
half-leading rounding, makes no allocations, and expires before the next line or
reflow. Computed styles remain alive and unchanged for that call; font loading
and later style changes need no cache invalidation mechanism.

A separate diagnostic counts nonzero-size `font_baseline()` calls during the
12 post-click layouts per page, including warm-up. Queries fell from 11,805 to
2,743 per layout on Tion and from 16,493 to 3,994 on Bihon: about 76% fewer.
No probe is included in the production code or the timing runs below.

Three fresh processes per version/page were run in alternating before/after
order, again retaining 30 warmed samples. Both versions use the same Qt binary
and dependencies; the control core is from `f373628a`. Medians in milliseconds:

| Phase | Tion before | Tion now | Bihon before | Bihon now |
| --- | ---: | ---: | ---: | ---: |
| CSS selection/composition | 67.87 | 72.46 | 110.04 | 110.51 |
| Layout/reformat | 63.11 | 57.29 | 104.49 | 96.40 |
| Complete refresh, before painting | 136.65 | 136.74 | 224.26 | 215.91 |

Layout improves by 9.2% and 7.7%, with lower layout medians in every process
pair. Unchanged CSS work varies noticeably between runs, so there is no clear
overall Tion refresh improvement in this batch. The result supports a modest
layout saving, not another large end-to-end speedup.

All six paired screenshots and DOM dumps are byte-identical. All 78 inspected
states, scroll positions, resource counts and final document extents match.
The baseline unit test covers shared text styles, cache eviction with different
font metrics, odd line heights, and changing metrics between reflows. Twenty
layout/font/capture tests pass in both Debug (ASan/UBSan) and Release.

Text fitting remains the larger layout cost: the Qt splitter estimates a
character position by measuring successive prefixes, then measures word
boundaries. Investigation of repeated baseline queries also exposed redundant
float layout, described below. Changing shaping, wrapping rules or persistent
text-cache lifetimes would need separate justification.

### Repeated float layout and stable text reflow

`layout_line()` was fully laying out float contents in both its initial sizing
pass and its placement pass. The first pass ignores float dimensions and resets
the wrapper width to zero; placement sets up the containing context and lays out
the contents again. The extra early call was introduced in `ddde5d6e` during
inline-grid work. Nested floats multiply these passes: both saved sites' main
article columns were laid out four times per document layout.

Removing the early call makes those columns enter layout once, without adding
cached state or changing float placement. A regression exercises zero to four
nested left/right floats and repeated resizing. With the earlier baseline cache
present, its leaf line makes two metric queries at every depth. Against the
control library, it instead makes 4, 8, 16 and 32 queries at depths one through four.

The removed passes also concealed a text-fitting inconsistency. Tion's
`probe-div-564` measured 135px tall initially, then 120px after reflow at the same
87px width. Its bold 13px text measures `patru victime` as 88px, but retained
fragments add up to 36 + 3 + 48 = 87px. Flex therefore retained a 488px panel
height while its child became 473px tall, leaving a visible 15px bottom gap after
the float optimization. Chromium has no such final gap.

Before laying out an inline container again, `layout_rejoin_text()` now restores
text runs split by wrapping. It joins only adjacent layout-generated clones of
the same styled source text, releases the shallow clones, restores the trailing
space and invalidates the combined width. Actual DOM/style boundaries and normal
space widths remain intact. Splitting also keeps the measured width when the
whole remainder fits, replacing its estimate even when no further box is created.
This makes wrapping independent of previous fragment boundaries, without another
layout settling pass or persistent cache. Re-measuring and splitting restored
runs costs some of the initial float-only speedup.

The final comparison includes both float and text fixes. Its control includes
the per-line baseline cache but neither fix. Three fresh processes per
version/page ran in alternating order, yielding 30 warmed samples per page.
Median times in milliseconds:

| Phase | Tion before | Tion now | Bihon before | Bihon now |
| --- | ---: | ---: | ---: | ---: |
| Layout/reformat | 53.87 | 41.36 | 96.69 | 73.70 |
| Complete refresh, before painting | 124.30 | 111.80 | 218.68 | 200.90 |

Layout improves by 23.2% and 23.8%; total refresh by 10.1% and 8.1%. These final
figures replace the preliminary float-only gains of 41% and 38%, which preceded
the text correction. CSS work remains noisy and is unchanged by these fixes.

Acceptance is based on settled rendering against Chromium, not equality between
intermediate passes. Chromium 153.0.8010.52 used the same saved pages, 1280x900
viewport, disabled scripts and aligned font preferences. Switching away and back
produces identical final inspected geometry in Chromium. Final alternate panels:

| Content-box height / gap | Wisp | Chromium |
| --- | ---: | ---: |
| Tion panel content box | 488px | 503px |
| Tion child content | 488px | 503px |
| Tion empty space below child | 0px | 0px |
| Bihon panel and child content | 980px | 980px |
| Bihon empty space below child | 0px | 0px |

Each panel has an additional 10px top padding. Tion's affected headline now
consistently occupies nine 15px lines, matching Chromium's 135px height. Its
widths are 87px and 86.7188px respectively; other text-wrapping differences still
make exact full-panel height an inappropriate cross-engine baseline. A scrolled
capture confirms that the extra grey strip below the last article is gone.
All six final Live-panel PNGs and DOM dumps match the control exactly.

The Qt text regression covers rounding in both directions, multiple retained
fragments, repeated widths and shrinking/widening, comparing final line breaks
with a fresh layout. The fractional-font capture fixture checks a nested flex
panel's final headline height and absence of a bottom gap. Both new regressions
fail against the preceding float-only library. No temporary probe is part of
the production source. All 42 layout/Qt tests pass in both Debug (ASan/UBSan)
and Release; the additional whole-remainder width regression also passes in
both configurations.

Local artifacts include `run_text_refit.py`, `text-refit-before.json`,
`text-refit-after.json`, `text-refit-final-tion/`, `text-refit-final-bihon/`,
`compare_final_float.py` and `float-final-chromium-{tion,bihon}/` in the artifact
directory below. Earlier `float-layout-*` artifacts describe the preliminary
float-only experiment, not the complete fix.

### Selective class-selector hash growth

Each stylesheet previously kept 64 buckets for class selectors, regardless of
size. A temporary lookup probe found that 93–94% of class-chain entries visited
during these refreshes had a different class name from the query. Only six Tion
sheets and seven Bihon sheets contained more than 128 class selectors; enlarging
every small sheet would waste memory.

At parse completion, class tables now grow in powers of two to target an average
of two selectors per bucket, capped at 1,024 buckets. Tables with at most 128
selectors stay at 64 buckets. Element and ID tables are unchanged. This is a
one-time sizing step, with no extra work or state in the lookup path and no
changes to generated CSS code or bytecode.

Each new chain is a subsequence of one old chain, preserving specificity, source
order, equal-order ties and selector bloom filters. Growth reuses existing
overflow nodes. Both allocations happen before any mutation; failure leaves the
original table usable. Hash size reporting accounts for the enlarged array and
freed overflow nodes. The new removal tests also exposed and fixed an existing
head-removal bug that lost the successor's bloom filter and leaked its node.

Fresh paired Release measurements alternate the saved preceding libcss and the
new library, using the same current core/frontend binary. Three fresh processes
per variant and page, 12 native tab switches each, discard the first two switches
per process: 30 warmed samples per variant and page. Medians in milliseconds:

| Phase | Tion before | Tion now | Bihon before | Bihon now |
| --- | ---: | ---: | ---: | ---: |
| CSS selection/composition | 70.28 | 63.38 | 123.41 | 107.17 |
| Layout/reformat | 42.15 | 43.70 | 75.97 | 75.40 |
| **Complete refresh, before painting** | **118.65** | **115.20** | **212.75** | **194.51** |

CSS work fell 9.8% and 13.2%; total refresh fell 2.9% and 8.6%. Layout is unchanged
by this patch; timing variation there affects the total benefit, especially on
Tion. These paired figures should be used instead of comparing absolute timings
with older runs made under different machine conditions.

Retained selector storage increases by 43,968 bytes (42.94 KiB) on Tion and
46,016 bytes (44.94 KiB) on Bihon, excluding allocator overhead. The earlier
prototype jumped all qualifying tables straight to 1,024 buckets and added
148.34/178.34 KiB. The graduated policy trades some lookup speed for much less
memory. Its largest temporary allocation is 40 KiB on this 64-bit build (new
bucket array plus temporary tail pointers, before freeing the old array).
A separate diagnostic measured all parse-completion sizing calls together at
about 1.0/0.6 ms per page; it is not included in the refresh timing runs.

All six final PNGs and DOM dumps are byte-identical to the control. Full box
trees match after normalizing addresses, and all 78 inspected tab states match
geometry, controls, scroll and resources. All 109 CSS, layout and Qt regression
tests pass in both Debug (ASan/UBSan) and Release. `libcss_selector_hash` additionally
passes LeakSanitizer; it covers growth thresholds/cap, pending imports, unchanged
element/ID tables, case-insensitive lookup order, specificity ties, media and
bloom filtering, both allocation failures, repeated finishing, insertion/removal
and storage accounting.

Local drivers and results in the artifact directory below:
`run_selector_growth.py`, `selector-growth-{control,grown}.json`,
`check_selector_growth.py`, `selector-growth-comparison.json`, and
`selector-growth-memory.c`. The temporary memory probe is not shipped.

### Shorter CSS-variable import scans

`css__stylesheet_tree_uses_variables()` already returns immediately when a sheet
contains local custom properties or `var()` references. Otherwise it previously
walked every top-level rule to find imported sheets. It now stops at the end of
the import prefix, using the same rule types as
`css_stylesheet_next_pending_import()`. This follows the current parser and
import-registration contract; future changes to that prefix must keep these
scans in sync. Nested imports are still visited, and unresolved imports are
checked afresh on later selections. There is no new cache, allocation, stored
flag or generated-code change.

A separate read-only probe replayed both variable-presence checks on the real
selection inputs, asserting that their answers agree. Mean rule visits per
warmed refresh, ten switches per page after two warm-ups:

| Variable-presence scan | Tion | Bihon |
| --- | ---: | ---: |
| Before | 169,433 | 243,722 |
| Now | 4,017 | 5,766 |

Fresh alternating Release measurements use the same 30-sample protocol as above.
The control already includes selective class-table growth; only the import scan
differs. These timing runs do not include the temporary probe. Medians:

| Phase | Tion before | Tion now | Bihon before | Bihon now |
| --- | ---: | ---: | ---: | ---: |
| CSS selection/composition (ms) | 57.82 | 55.98 | 96.46 | 94.64 |
| **Complete refresh, before painting (ms)** | **105.71** | **104.02** | **176.82** | **173.37** |

CSS work fell 3.2%/1.9%, and total refresh fell 1.6%/2.0%. This is a modest
incremental improvement; compare within this paired run, not against absolute
timings from the preceding experiment. No additional retained memory is needed.
All six final PNGs, DOM dumps and normalized box trees match the control, as do
all 78 inspected tab states. All 109 selected CSS, layout and Qt tests pass in
both Debug and Release. The extended variable test also passes LeakSanitizer:
it covers charset prefixes, an empty first import, nested variables reached
through a later import, ignored late imports, attaching a previously unresolved
import, cascade order and pseudo-element variable inheritance.

Local artifacts: `run_import_scan.py`, `import-scan-{control,short}.json`,
`check_import_scan.py`, `import-scan-comparison.json`, `import-scan-probe.c`, and
`import-scan-counts.json` in the directory below. The probe is not shipped.

### Reusing Qt text-prefix measurements

The Qt splitter measures the complete string, estimates a character position,
then checks whole-word boundaries. Those stages frequently measure the same
prefix more than once. A callback-local `PrefixWidths` now keeps the complete
width and four recent prefix widths on the stack. The font, residual scale,
spacing and source text stay unchanged for the callback, so UTF-16 prefix length
is sufficient as the key. Nothing survives the callback or needs invalidation
when fonts, text or styles change.

The splitter also shares its decoded string with the position routine and skips
that routine when the full string already fits. Uncached prefixes still use the
same independent Qt measurements; character estimates, word-break searches,
UTF-8 offsets and fractional rounding retain their existing behavior. The
ordinary width callback is unchanged, and there is no retained per-page memory
increase or new allocation for the cache.

Temporary probes counted `text_advance()` calls inside the splitter during ten
warmed refreshes after two warm-ups. Mean counts per refresh:

| Splitter work | Tion before | Tion now | Bihon before | Bihon now |
| --- | ---: | ---: | ---: | ---: |
| Split callbacks | 404 | 404 | 917 | 917 |
| Prefix-width measurements | 2,576 | 1,980 | 5,742 | 4,552 |
| Repeated measurements within one callback | 598 | 2 | 1,194 | 4 |

Measurements fall by 23.1%/20.7%. The few remaining repeats follow eviction;
increasing the fixed cache size for them would provide little benefit.

Fresh alternating Release captures compare the preceding Qt binary at
`1e6d41fc` with the optimized binary, using identical current libraries. Both
include the CSS optimizations above. Three processes and 30 warmed samples per
page/variant, without temporary probes; medians in milliseconds:

| Phase | Tion before | Tion now | Bihon before | Bihon now |
| --- | ---: | ---: | ---: | ---: |
| CSS selection/composition | 52.51 | 52.94 | 96.05 | 97.30 |
| Layout/reformat | 41.33 | 34.65 | 72.88 | 62.87 |
| **Complete refresh, before painting** | **98.55** | **93.25** | **180.02** | **170.18** |

Layout falls 16.2%/13.7%, with lower layout medians in every process pair.
Complete refresh falls 5.4%/5.5%; unchanged CSS work still varies between runs.
All six final PNGs, DOM dumps and normalized box trees match, and all 78 inspected
tab states preserve geometry, controls, scroll and resources.

All 44 layout/font/Qt tests pass in both Debug (ASan/UBSan) and Release. The
extended split test compares complete-word results with independent width calls
across fractional sizes, negative/zero/positive letter spacing, accents,
combining marks, supplementary characters, emoji sequences, Arabic text and
alternating narrow/wide words. Existing tests cover zero-size text, hit testing,
late fonts and repeated/resized core layout. Temporary probes are removed from
the production source.

Local artifacts: `run_text_prefix.py`, `text-prefix-{control,cached}.json`,
`check_text_prefix.py`, `text-prefix-comparison.json`, `text-prefix-counts.json`,
and `text-prefix-probe.{cpp,h}` in the directory below. The copied control binary
is `/tmp/wisp-text-prefix-control-qt`.

### Filtering ordinary rules out of variable collection

The parser now marks each rule that successfully stores a custom-property
**definition**. An ordinary declaration using `var()` does not set this marker.
The bit shares the rule's existing parent-type metadata byte: `css_rule`,
`css_rule_selector` and `css_style` retain their previous sizes (32/48/24 bytes
on this 64-bit build). There is no new allocation, per-element state or cache.

During collection, selection checks the marker once per returned selector
candidate, before full selector-chain matching. Inline styles without custom
properties also skip collection. The normal cascade, selector hash traversal,
imports, source order and specificity are unchanged. Custom-property parsing
already has one shared handler, so the generated ordinary-property handlers and
bytecode format need no changes. Storing the marker on the owning rule also
avoids extra bookkeeping when temporary declaration styles are merged/recycled.

A separate count-only probe measures collection-pass candidates before/after
filtering. Means over ten warmed tab switches:

| Full selector-chain matches attempted | Tion | Bihon |
| --- | ---: | ---: |
| Before | 9,156 | 13,847.5 |
| Now | 5,337 | 7,676 |

This removes 41.7%/44.6% of collection-pass matching attempts. Hash lookup work
remains; the percentage is not a reduction in all CSS processing.

Fresh-process timing comparisons were too variable to isolate this small gain.
A temporary benchmark switch therefore alternated filtered and unfiltered
refreshes within each process in ABBA order, with both panels equally represented.
Three processes per page, 28 switches each, discarding the first four, produce
36 pairs of nearby refreshes of the **same panel** per page. Both modes use the
same binary and measurement overhead. Median paired savings:

| Phase | Tion saved (ms) | Bihon saved (ms) |
| --- | ---: | ---: |
| CSS selection/composition | 2.23 (3.88%) | 4.28 (3.94%) |
| Complete refresh, before painting | 2.23 (2.35%) | 4.32 (2.29%) |

CSS time improves in 30/36 Tion pairs and 29/36 Bihon pairs. This is a modest
incremental improvement; it is not an initial-load measurement, and these
figures should not be compared directly with earlier absolute timing tables.
Both the switch and counters are removed from production code.

All six independent final PNGs, DOM dumps and normalized box trees match the
control, as do all 78 inspected tab states. The within-process experiment adds
six matching final captures and 174 matching states. All 104 selected CSS,
layout and Qt tests pass in Debug (ASan/UBSan) and Release. The selector and
variable-resolution tests also pass LeakSanitizer. The broad Debug suite uses
`detect_leaks=0` for DOM/test cleanup leaks reproduced with the unchanged CSS
library; address and undefined-behavior checks remain enabled.

The new tests cover successful/invalid custom declarations, definitions before
and after ordinary/generated declarations, variable references that define
nothing, nested media rules and pseudo-element inheritance. A callback-counting
regression test verifies that ordinary/deferred selectors are matched once and
custom-property selectors twice; it fails against the previous library.

Local artifacts: `run_variable_candidates.py`, `check_variable_candidates.py`,
`variable-candidates-{control,filtered,comparison}.json`,
`run_variable_candidates_toggle.py`, `check_variable_candidates_toggle.py`,
`variable-candidates-toggle{,-summary}.json`, `variable-candidates-toggle.{c,patch}`,
`instrument_variable_rules.py`, `variable-rules-probe.c` and
`variable-candidates-counts.json` in the directory below. The unchanged library
is saved in `/tmp/wisp-variable-rules-control/` for local reproduction.

### Chromium comparison and measurement limits

Chromium 153.0.8010.52, same files/viewport, page scripts disabled, aligned font
preferences, three fresh processes and 30 warmed native clicks per page. DevTools
`Performance.getMetrics` with `timeDomain=timeTicks` measured deltas around the
press/release, forced panel geometry and screenshot completion:

| Chromium phase | Tion median (ms) | Bihon median (ms) |
| --- | ---: | ---: |
| Style recalculation | 1.75 | 2.16 |
| Layout | 1.36 | 1.87 |
| Combined style + layout | 3.15 | 4.06 |

Each sample recorded four style passes and one layout pass. These counters are
internal work durations, not complete click-to-screen latency. Chromium paint,
raster and display presentation are not included; Wisp's drawing figures are
also offscreen CPU measurements. The timing boundaries of the two engines differ,
so this is evidence of the scale of their work, not a precise overall speed ratio.

The capture loop still waits at least 50 ms after activity to obtain a painted,
settled state. Internal profile timers exclude that wait. Using identical capture
options, uninstrumented/instrumented interval medians were 396/397 ms for Tion and
654/667.5 ms for Bihon. This is a rough overhead check, including run-to-run noise,
not a precise estimate of timer cost. The earlier 450–830 ms figures included a
100 ms quiet period and should not be used as renderer-only timings.

Local raw samples, reproducible drivers and the DOM microbenchmark are under
`out/render-captures/checked-timing-2026-09-29/` (`baseline.json`, `profiled.json`,
`linear.json`, `variable-pass.json`, `family-cache-final.json`,
`line-baselines-before.json`, `line-baselines-after.json`,
`line-baselines-query-counts.json`, `float-layout-before.json`,
`float-layout-after.json`, `float-layout-query-counts.json`,
`float-layout-state-differences.json`, `chromium.json`,
`nodelist-*.json`, `run_wisp.py`, `run_chromium.py`,
`nodelist_benchmark.c`). See the capture guide for counter definitions.

## Checks

- `libdom_iteration_test`: document/subtree order, hidden descendants, non-element
  nodes, early callback errors, child replacement, empty roots and deep trees.
- `form_state_test`: groups, explicit/implicit labels, interactive descendants,
  disabled controls/fieldsets, explicit form ownership and live/default state.
- `css_checked_test`: repeated checked-state invalidation with the same selection
  context.
- `libcss_selector_hash`: bounded class-table growth, stable lookup order,
  allocation failure, removal and memory accounting.
- `qt_checked_interaction`: real mouse/keyboard input with JS disabled, repeated
  radio/checkbox switches, CSS variables and geometry, typed values in hidden
  panels, scroll preservation, generated/layered images, SVG reuse, and hidden/
  newly revealed iframes. Runs under the normal Debug sanitizers.

See [Qt capture instructions](../../frontends/qt/CAPTURE.md) for ordered actions
and `steps.json`. Saved-site checks use Tion's Live/Comentate and Bihon's
Live/Distribuiri sidebar labels; these are native radio/CSS tabs. Four alternating
desktop clicks agree with Chromium's script-disabled behavior. Bihon also works
at 390 px; Tion's containing element is hidden there in both browsers.
