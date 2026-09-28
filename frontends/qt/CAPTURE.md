# Rendering captures

Build the optional Qt developer tool with `-DWISP_QT_CAPTURE=ON` (default OFF).
It reuses the browser's actual Qt widget paint path and existing DOM/box dumps.
All capture code and hooks compile out when the option is OFF. No changes to
the renderer, code generators or threading model are needed.

```sh
cmake -S . -B build -DWISP_QT_CAPTURE=ON
cmake --build build --target wisp-qt
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1 QT_FONT_DPI=96 \
  build/frontends/qt/wisp-qt \
  --capture-dir /tmp/wisp-haiku --capture-size 1280x900 \
  https://www.haiku-os.org/
```

A local HTML filename also works. The output directory must be new or empty,
so an old successful screenshot cannot be mistaken for a failed new capture.
`--capture-help` lists options. Use `QT_QPA_PLATFORM=xcb` under Xvfb if a platform
needs a display server; capture still includes only the page, without window
decorations, tabs or scrollbars.

Each run writes:

- `page.png`: the content viewport, painted by the normal Qt renderer.
- `dom.txt`: the existing DOM structure dump (elements, IDs/classes and title;
  not serialized HTML).
- `boxes.txt`: the existing layout tree, with box geometry, text and computed
  styles. Coordinates are relative to containing boxes; pointer addresses vary
  between runs. The dump covers the document, including content below the image.
- `capture.json`: outcome, URLs, load state, viewport and document dimensions,
  elapsed/quiet time, zoom, DPI, device pixel ratio, font defaults, JavaScript,
  animation and Qt settings, and which artifacts were saved.

## Focused inspection

Add `--capture-id ID` (repeatable, literal ID without `#`) and/or
`--capture-point X,Y` to write `inspection.json` alongside the image:

```sh
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1 build/frontends/qt/wisp-qt \
  --capture-dir /tmp/wisp-inspect --capture-size 1280x900 \
  --capture-id preface-last --capture-id fundraising-progress-bar-filler \
  --capture-point 950,300 https://www.haiku-os.org/
```

The report contains stable DOM paths/IDs, content/padding/border rectangles,
used margins, parent and child boxes, the absolute containing block, computed styles,
and the font selected through Qt's actual font-selection path. Anonymous boxes
also identify their nearest DOM owner. The legacy style dump is supplemented
with flex alignment/direction/growth and grid auto-flow values. This is not a
complete modern CSS serializer. The font report identifies the primary font;
individual glyph fallback can use additional fonts.

Font diagnostics distinguish `weight` (the CSS request), `selected_weight`
(the weight requested from Qt after CSS fallback matching), and `face_weight`
(the static font's OpenType OS/2 weight). `resolved_style` names the loaded face.
`face_weight` is null for variable fonts or fonts without that table; `variable`
identifies variable fonts. Qt's reported numeric weight alone can echo the request
even when it loaded a different face.

`pixel_size` is the underlying integer Qt font size. `effective_pixel_size`
includes the fractional scale used for measurement and painting (before page
zoom), so a 15.9px CSS font can report a 15px base and approximately 15.9px effective.

ID lookup distinguishes `not_found` from `no_box` (for example `display:none`,
a hidden ancestor, or an SVG subelement without its own HTML box). Wrapped
inline elements can own several boxes. Reports include up to 64 matching boxes,
64 immediate children per box, and 12 ancestors; counts expose truncated box
and child lists. Duplicate IDs select the first DOM match.

Coordinates are the engine's **unscaled document layout coordinates, before
CSS transforms**, including ancestor scrolling. At 100% zoom and scroll position
zero they correspond to screenshot coordinates. Point queries reuse the core
hit-test walk and report its visible candidates; this is not a guarantee of
correct paint/z-order or transformed hit testing. Queries inspect the top-level
document, not the DOM inside child frames. A missing ID is a valid diagnostic
result and does not make a successfully rendered capture fail.

## Reference comparison

`compare_capture.py` runs Wisp and an installed Chromium with page JavaScript
disabled, separate temporary profiles, equal drawable viewports, 100% zoom and
device pixel ratio 1. It collects the same requested element IDs/point from
Chromium through [DevTools](https://chromedevtools.github.io/devtools-protocol/),
including computed styles, box quads and used fonts.
It waits for load and network quiet, records failed Chromium resources, and
keeps logs on failure. No browser is downloaded or installed by the script.

It requires **ImageMagick 7** (`magick`) and **Python websocket-client** in addition
to Chromium. These are optional developer dependencies, outside the browser
build. Use `--chromium` or `--magick` for nonstandard executable locations.

```sh
python3 frontends/qt/compare_capture.py test/render-inspect.html \
  --wisp build/frontends/qt/wisp-qt --output /tmp/wisp-comparison \
  --size 1280x900 --id probe --point 60,80
```

Open `report.html` for side-by-side images, a 50% overlay and an absolute RGB
difference image. `comparison.json` records settings and the differing-pixel
fraction. The `wisp/` and `reference/` directories retain their images, metadata
and optional inspection reports. Different pixels do **not** fail the command;
failed captures, mismatched dimensions/settings or tool errors do. Nothing is
rescaled to disguise a viewport mismatch. The output directory must be empty.

For before/after regression checks, use `--reference /path/to/earlier/capture`
with a directory containing `page.png` and `capture.json`. This mode needs no
Chromium or websocket-client. The earlier capture must have completed at the
same viewport with JS off, DPR 1 and zoom 1. Its URL/settings remain in the report
so deliberately comparing different fixtures is possible.

Chromium receives Wisp's resolved default font families and its default/minimum
font sizes rounded to whole pixels. Fractional font sizing and font rasterization
can still differ. CSS animations are paused in Chromium; animated image frames
may differ from Wisp's disabled image animation. Chromium's box quads include
transforms while Wisp's inspection rectangles describe layout before transforms.
Live sites may serve different content/assets between the two loads: use saved
pages for reliable comparisons. A high pixel-difference percentage alone is not
evidence of a particular rendering bug.

## Capture settings and completion

The requested size is the **drawable content area**, in logical pixels. At
device pixel ratio 1, PNG dimensions match it exactly. Match this area when
comparing with Chromium, whose screenshot size can include its scrollbar.
Use the same assets, fonts, zoom, JavaScript setting and viewport. For stable
regressions, prefer a saved site and keep its source revision alongside the
capture; live pages and network resources can change between runs.

Capture uses a fresh temporary settings/cookie/history/cache profile and 96 DPI.
JavaScript and image animation default to OFF; existing core command-line
options can override these (for example `--enable_javascript=1`). Ordinary
interactive launches retain their usual settings even in a capture-enabled
build. Do not override profile file paths when you want an isolated run.

Completion requires the core's DONE state, stopped loading indicator (including
child frames), redraw readiness, and no viewport paint/resize/invalidation for
`--capture-settle` milliseconds (default 500). The deadline is
`--capture-timeout` milliseconds (default 30000). Exit status is 0 for a complete
capture, 1 for timeout/load/output/viewport failure, or 2 for bad arguments or
initialization failure. On timeout, any safely drawable partial page is saved;
read `result` in the JSON before trusting the image. Browser-generated error or
authentication pages are reported as `load_error`.

This is a rendering aid, not a full network recorder: DONE does not guarantee
that every optional image/font loaded successfully, or that an HTTP response
was 200. Missing resources can still produce a complete capture. Dumps apply
to HTML; standalone images have no DOM/box dump. A JS timer may mutate a page
after the quiet period. The in-process deadline also cannot interrupt a stuck
renderer; an automated caller should impose an outer process timeout. Capture
timings include loading, settling and instrumentation, so use the performance
tracer for engine benchmarks.

## Development workflow

Capture the saved site, identify a visible problem, then reduce it to a small
page under `test/`. Inspect the matching DOM IDs and box dimensions to decide
whether the problem is parsing/style, layout or painting. After fixing it,
capture both the small page and the whole site at the same viewport. Keep a few
known-good sites (including Haiku) as broader checks. Pixel differences help
locate regressions but font rasterization differences need visual judgment.

The integration check uses Python's standard library, Qt's offscreen plugin and
a local HTTP server. It needs no Check library or internet access:

```sh
python3 frontends/qt/test_capture.py build/frontends/qt/wisp-qt
```

It checks exact viewport sizes with scrolling content, repeatable PNG output,
tree dumps, a delayed stylesheet and redirect, unfinished loads, browser error
pages, output failures, and invalid/reused output arguments. If CTest is enabled,
it is also registered as `qt_render_capture`.

The optional Chromium comparison checks additionally exercise JS blocking,
element/point inspection, delayed CSS, identical before/after baselines and
rejection of mismatched or failed captures:

```sh
python3 frontends/qt/test_compare_capture.py build/frontends/qt/wisp-qt
```
