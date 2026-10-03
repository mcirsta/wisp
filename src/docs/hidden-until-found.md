# Hidden until found

Implement HTML `hidden="until-found"` without JavaScript, on the existing single-threaded rendering and restyle paths. Ordinary `hidden` support is committed separately as `e148fb9c`.

## Required behavior

An until-found element keeps its outer box, background, borders and padding while its contents are skipped. A fragment target or the active Find in Page match reveals the relevant ancestors before scrolling. Ordinary hidden content, author `display:none`, and ordinary `content-visibility:hidden` must not become searchable merely because another ancestor uses until-found. Attribute keywords are ASCII case insensitive.

Follow [HTML hidden and ancestor revealing](https://html.spec.whatwg.org/multipage/interaction.html#the-hidden-attribute), [find-in-page revealing](https://html.spec.whatwg.org/multipage/interaction.html#interaction-with-details-and-hidden=until-found), and [CSS content visibility](https://www.w3.org/TR/css-contain-2/#content-visibility). Dispatch `beforematch` through the existing DOM event mechanism; native revealing must work with scripting disabled. Recheck the DOM after dispatch because a handler may modify it.

## Implementation sequence

1. Add `content-visibility:visible|hidden` through `contrib/libcss/properties.toml`, its generated parser/cascade/computed storage, public value enums/getters, and diagnostic dumps. Keep viewport-driven `auto` and `contain-intrinsic-size` outside this feature; unsupported values remain unsupported rather than silently advertising different semantics.
2. Apply the until-found UA style separately from ordinary hidden. Centralize whether a box can skip contents, respecting non-atomic inline and internal-table exclusions. Preserve the outer box and its formatting-context behavior while omitting descendants and generated contents. Replaced content needs the same paint/hit-test suppression and appropriate intrinsic sizing.
3. Add one shared ancestor-reveal operation. Resolve fragment IDs in the DOM, remove eligible until-found attributes, request the existing full restyle, and retry scrolling after the new layout is published. Keep references to DOM targets across asynchronous work; do not retain old box pointers.
4. Make hidden text discoverable by Find in Page. Reuse existing rendered-box searching on ordinary documents. For documents containing until-found, discover matches from DOM text with computed-style filtering and the same whitespace/text-transform rules used for rendering. Retain DOM text identity and character ranges for these matches. Resolve them to current layout boxes for highlighting and scrolling; reveal only the active match's ancestors.
5. Extend the existing search lifecycle to suspend highlights before a reveal rebuild and rebind stable matches after publication. A changed or cleared query, navigation, failure, or teardown must release references and cancel pending work. Preserve existing plain-text searching and normal-page behavior.

Text-box identity should reuse existing storage where possible. Generated parser output is not edited by hand. No feature flag, alternate production restyle path, polling, or rendering thread is introduced.

## Validation

- Parser/cascade: defaults, inheritance, author overrides, invalid values and generated round trips.
- Rendering against Chromium: preserved padding/borders/background; hidden descendants and pseudo-elements; explicit/minimum sizes; flex/grid; inline and table exclusions; replaced content; float isolation.
- Fragments: initial navigation, same-document clicks, nested ancestors, missing targets, ordinary-hidden barriers, explicit display overrides, repeat navigation.
- Search: hidden and visible matches in document order, next/previous, wrapped text, case/whitespace, nested sections, show-all without revealing inactive matches, highlighting and scroll bounds after reveal, query replacement/clear and teardown.
- Existing hidden, checked interaction, retained restyle, capture, layout and text-search tests. Debug sanitizers and Release captures. Saved Tion/Bihon comparisons remain regression checks.

## Implementation status

The native implementation is connected and enabled by default, with no production feature switch. Fragment navigation includes a target that is itself an until-found element. Search reveals use the ordinary full restyle; the selected match keeps DOM text identity and normalized offsets while old selections and box references are released. Ordinary documents retain their existing rendered-box search. Text boxes reuse their previous DOM pointer slot, so their structure does not grow.

`beforematch` is dispatched through the existing DOM event mechanism. Native reveal/search behavior is tested with JavaScript disabled; this work does not expand the scripting engine. Search across separate DOM text nodes, generated text discovery in the DOM search mode, viewport-driven `content-visibility:auto`, `contain-intrinsic-size`, and closed-details revealing remain outside this change. Text across wrapped fragments of one DOM text node is supported.

The existing search cleanup also needed a lifecycle fix: destroy highlights before destroying the frontend window, because selection destruction requests a redraw.

## Validation results

Debug and Release builds succeeded. The focused Release suite passed all eight checks. The full Debug run exercised 196 tests: 188 passed immediately; the eight failures subsequently passed after updating the custom-property opcode expectations, repeating two tests interrupted by a relink, and applying the previously established Fontconfig-only LSan suppression to five font fixtures. Engine leak detection and ASan/UBSan remained enabled. The new until-found test passed without suppression in both builds.

Tion/Bihon desktop and mobile captures were pixel-identical to the committed ordinary-hidden baseline, with unchanged inspected box geometry.

## Repeatable checks

- `qt_hidden_until_found` covers preserved decoration/size, flex/grid, replaced images and controls, table/inline exclusions, float isolation, CSS variables/defaulting, nested and self-target fragments, search order, show-all, wrapped matches, transformed text, query replacement/clear during a pending reveal, ordinary-page search and closing with active highlights.
- `qt_hidden_attribute` retains the ordinary hidden, author override and hidden-radio regression checks.
- `libcss_parse2_auto_content-visibility_dat` exercises the generated parser and bytecode diagnostic. Adding a property advances the two internal custom-property opcodes, so their bytecode golden expectations were updated too.
- Chromium reference captures verify principal-box sizes and nested-fragment geometry. Saved Tion/Bihon captures at 1280 and 390 pixels remain pixel-identical to the ordinary-hidden baseline.

Capture builds support `--capture-find TEXT`, `--capture-find-back TEXT`, `--capture-find-all TEXT`, and `--capture-find-case TEXT`. An empty `--capture-find` value clears the query. Element inspection includes the hidden attribute, skipped-content state and highlighted-fragment count. These diagnostics remain within the existing optional Qt capture build.

Validation artifacts: `out/render-captures/until-found-2026-10-03/`.


## Findings reserved for another run

1. **Fontconfig shutdown allocations:** five existing font captures report 1,895 bytes in 43 allocations, all rooted in `libfontconfig.so.1` / `FcFontRenderPrepare`. This matches the earlier paragraph-fitting validation. Investigate ownership between Qt font fallback and Fontconfig teardown before changing cleanup; do not disable engine leak detection or call `FcFini` while Qt still owns fonts. Reproduce with `ctest --test-dir build-css-vars --output-on-failure -R '^qt_inline_text_baseline$'` without a suppression. The existing diagnostic suppression is `out/render-captures/paragraph-fitting-adoption-2026-10-03/fontconfig.lsan`.
2. **`display:flow-root`:** the extended Chromium fixture exposed missing support: a preceding float escaped that container and shifted later content. The final fixture uses the already supported `overflow:hidden` formatting context to isolate this feature. Implement a proper formatting-context representation in a separate change; avoid an exception in hidden-content layout. The original evidence remains in `chromium-extended/` beneath the artifact directory.
3. **Search coverage:** the new DOM search mode intentionally searches DOM text nodes, including wrapped fragments, but does not discover generated `::before`/`::after` text. Ordinary documents keep their original box search, which can find such text. A follow-up should preserve stable generated-content identity across rebuilding rather than retain dangling boxes. Cross-node phrases and closed-details revealing also need their own search work.

No workaround for these findings is enabled in the production browser.
