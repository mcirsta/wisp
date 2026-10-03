# Correctness backlog

Only unresolved work belongs here. Completed changes and validation history are
in Git; the regression fixtures remain in the test suite.

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
