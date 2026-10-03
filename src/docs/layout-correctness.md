# Correctness backlog

Only unresolved work belongs here. Completed changes and validation history are
in Git; the regression fixtures remain in the test suite.

## `display:flow-root`

The extended hidden-until-found comparison exposed missing support: a float
escaped its intended container and shifted later content. Implement a proper
block formatting context through the CSS display representation and box/layout
paths, including generated property code. Do not translate it to
`overflow:hidden`: flow-root establishes a context without clipping overflow.

Cover float containment, avoidance of outside floats, margin isolation and
visible overflow. Original evidence:
`out/render-captures/until-found-2026-10-03/chromium-extended/`.

## Image ratio precision

Bihon's `probe-img-62` is 111×99 in Wisp versus 111×100 in Chromium. Another 55
desktop Tion/Bihon images are 202×101 versus Chromium's approximately
202.48×102.25. The stored CSS ratio is 1.98047 rather than 1.98; fixed-point
quantization followed by integer truncation exposes the extra pixel difference.
Normal-flow controls show the same limitation.

Review precision and rounding in the shared ratio-sizing path. Do not compensate
in absolute positioning or stretch images to their containing block. Cover
intrinsic and CSS ratios, min/max constraints and both normal/positioned images.
Evidence: `out/render-captures/absolute-images-2026-10-03/`.

## Fontconfig shutdown allocations

Five font captures report 1,895 bytes in 43 allocations rooted in
`libfontconfig.so.1` / `FcFontRenderPrepare`. Investigate ownership between Qt
font fallback and Fontconfig teardown. Do not disable engine leak detection or
call `FcFini` while Qt still owns fonts.

Reproduce without a suppression:

```sh
ctest --test-dir build-css-vars --output-on-failure -R '^qt_inline_text_baseline$'
```

The existing diagnostic suppression, used only for these captures, is
`out/render-captures/paragraph-fitting-adoption-2026-10-03/fontconfig.lsan`.

## Find in Page coverage

The DOM search mode used for documents containing hidden-until-found does not
discover generated `::before`/`::after` text. Ordinary documents' box search can
find it. Add stable generated-content identity across rebuilding; do not keep
stale box pointers. Cross-node phrases and revealing closed `details` elements
also remain unimplemented. Wrapped fragments of one DOM text node already work.

The supported behavior and repeatable checks are in
[hidden-until-found.md](hidden-until-found.md).
