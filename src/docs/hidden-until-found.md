# Hidden until found

HTML `hidden="until-found"` is supported by default on the existing single-threaded
rendering and restyle paths. No production feature switch or polling is used.

## Behavior and implementation

An until-found element retains its outer box, decoration and size while its
contents are skipped. The UA rule uses `content-visibility:hidden`, implemented
through libcss's property generator. Non-atomic inline and internal-table
exclusions are respected. Ordinary hidden content, author `display:none` and
ordinary `content-visibility:hidden` are not made searchable by an until-found
ancestor.

Fragment navigation and the active Find in Page match reveal eligible ancestors
before scrolling. The shared reveal operation dispatches `beforematch`, rechecks
the DOM, requests the ordinary full restyle, and retains DOM targets across the
asynchronous work. Native revealing works with JavaScript disabled. Attribute
keywords are ASCII case insensitive.

Closed `details` ancestors share this reveal and search path; their summary stays
visible and does not itself trigger expansion. See [details.md](details.md) for
native disclosure behavior and its regression coverage.

Documents containing until-found use DOM text discovery with computed-style
filtering and rendering's whitespace/text-transform rules. Matches retain DOM
text identity and normalized offsets; highlighting is rebound to new layout
boxes after publication. Wrapped fragments of one DOM text node are supported.
Ordinary documents retain rendered-box searching. Text boxes reuse their former
DOM pointer slot rather than growing the box structure.

Generated `::before`/`::after` text participates in DOM discovery in rendering
order: before, child content, after. Matches retain the originating element,
pseudo phase and offsets, so identical strings from those three sources remain
distinct across rebuilding and line splitting. Rendering and search share the
string/attribute/default-quote extraction and formatting-type rules. Unsupported
generated items (such as counters) are not invented by search. Source identity
uses the existing text-node pointer and box flags; it adds no per-box fields.

Object fallback uses the same resource-parameter preflight in construction and
discovery. Missing/empty resources and known unsupported declared types can
expose ordinary and generated fallback text even before boxes exist. Objects
using an embedded resource suppress their fallback children and generated
pseudos in search as in rendering. Discovery does not fetch resources or publish
boxes; later load/decoding failures remain in the correctness backlog. Generated
`attr()` text preserves its complete UTF-8 byte sequence.

Query replacement, clearing, failure, navigation and teardown release references
and cancel pending work. Highlights are destroyed before the frontend window,
because selection cleanup can request a redraw.

References: [HTML hidden and ancestor revealing](https://html.spec.whatwg.org/multipage/interaction.html#the-hidden-attribute),
[find-in-page revealing](https://html.spec.whatwg.org/multipage/interaction.html#interaction-with-details-and-hidden=until-found),
and [CSS content visibility](https://www.w3.org/TR/css-contain-2/#content-visibility).
Viewport-driven `content-visibility:auto` and `contain-intrinsic-size` remain
outside this feature; unsupported values are not silently accepted.

## Repeatable checks

- `qt_hidden_until_found`: preserved decoration/size, flex/grid, replaced images
  and controls, table/inline exclusions, float isolation, CSS variables,
  nested/self-target fragments, search order, show-all, wrapped/transformed text,
  query replacement/clear during reveal, ordinary search and closing with highlights.
- `qt_hidden_attribute`: ordinary hidden, author overrides and hidden radios.
- `qt_generated_search`: generated-source identity/order, strings/attributes/quotes,
  nested reveal, wrapping, hidden flex/grid blockification, JS-disabled canvas
  fallback, object fallback before construction, unsupported MIME types, UTF-8
  attributes, excluded pseudo content/replaced owners, cancellation and native label clicks.
- `box_construct_context_test`: shared object preflight agrees with construction,
  makes no requests or mappings, releases temporary parameters, and preserves
  generated UTF-8 attribute text. Existing detached-resource tests cover both
  normal and retained construction.
- `libcss_parse2_auto_content-visibility_dat`: generated parser and bytecode dump.

Capture builds support `--capture-find TEXT`, `--capture-find-back TEXT`,
`--capture-find-all TEXT`, and `--capture-find-case TEXT`. Empty `--capture-find`
clears the query. Inspection includes the hidden attribute, skipped-content state
and highlighted-fragment counts (total and separately before/after). These
diagnostics use the optional Qt capture build.

Reference artifacts: `out/render-captures/until-found-2026-10-03/`.
Unresolved findings are tracked only in [the correctness backlog](layout-correctness.md).
