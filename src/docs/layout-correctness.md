# Correctness backlog

Only unresolved work belongs here. Completed changes and validation history are
in Git; the regression fixtures remain in the test suite.

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

## Positioned content loses clicks to later blocks

An absolutely positioned label with `z-index:auto` fails to toggle its checkbox
where a later normal-flow block overlaps it. Chromium identifies the label at
the same point; Wisp includes the label in its hit list but the native click
does not activate it. Setting `z-index:1` makes the click work. The failure
occurs with ordinary `display:block` as well as `flow-root`, with or without
label text.

Review hit-testing order against painting/stacking order, including positioned
descendants inside inline containers. Fix the shared ordering rather than
special-casing labels. Reproductions and native click captures are in
`out/render-captures/flow-root-2026-10-03/label-probe/`; the Chromium comparison
is in the adjacent `label-comparison/` directory (point `241,15`).

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
