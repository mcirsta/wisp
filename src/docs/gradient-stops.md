# Gradient stop lengths

This extends the existing linear/radial gradient parser and painters. Stop
positions retain a length unit, a percentage, or an explicit omitted-position
flag. Negative positions are valid; they are not used as an omission sentinel.
The shared parser still caps gradients at ten stops and requires at least two.

The handwritten `background_image.c` parser emits color/offset/unit triples.
The cascade, `!important` walker, deferred-cascade skip walker, diagnostic dump,
and computed-style equality all understand that payload. No generated property
layout or generator metadata changes are needed. Font-relative lengths become
pixels during style computation, so explicit inheritance preserves their size.

At paint time, both existing painters use the same stop resolution: percentages
refer to the full gradient line, out-of-order stops move forward, and omitted
stops are distributed between their surrounding explicit positions. Native
plotters receive the visible portion of this color function in their supported
0–1 range. Negative and beyond-end stops are sampled at the boundaries, rather
than simply clamped. Interpolation uses premultiplied alpha; coincident stops
retain a hard edge. The gradient origin and size no longer depend on the clipped
repaint rectangle. Frontends without a native linear plotter use the existing
strip painter with the same resolved positions.
The drawing optimizer keeps earlier backgrounds beneath translucent rectangle
fills, which the strip painter needs for correct alpha compositing.

This covers HotNews's `5px`/`160px` overlays, including variable colors and
`background: ... !important`. It does not complete all CSS gradient syntax:
arbitrary angles are still approximated to cardinal directions, and color-space
clauses, multiple background layers, repeating gradients, transition hints,
double-position stops, `currentColor` stops and `calc()` positions remain
unsupported. Existing radial shape/size and background sizing/attachment limits
are not redesigned here.

`layout_calc_test` exercises real parsing and selection, mixed/negative/omitted
positions, resizing, zoom, font-relative inheritance, style sharing, important
and variable declarations, invalid input and the stop-count bound.
`qt_gradient_stops` (optional capture build, with ImageMagick available) checks
40 rendered pixel samples from `test/gradient-length-stops.html`, including
clipping, opacity, hard edges and radial stops. The fixture also works with
`frontends/qt/compare_capture.py` for Chromium comparison.

Reference: [CSS Images color stops and fixup](https://www.w3.org/TR/css-images-3/#color-stop-syntax).
