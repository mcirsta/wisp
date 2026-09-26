# Multiple background layers

`background-image`, `background-position`, `background-repeat` and
`background-attachment` accept comma-separated lists. The `background` shorthand
builds those lists together, supplies defaults for each layer, and accepts a
color only in its final layer. Existing URL, linear-gradient and radial-gradient
syntax can be mixed. Invalid lists discard the whole declaration.

The image list determines how many layers are drawn; `none` occupies a slot.
Position, repeat and attachment lists retain their independent lengths. Indexed
getters cycle shorter lists, and extra values survive for inheritance or later
image overrides. The first image is on top; the color is painted once below all
images. A failed image fetch leaves the other layers visible.

## Storage and ownership

The existing 32-bit bytecode format is retained. Single values keep their old
encoding. A multi-value declaration has an outer OPV, a payload word count, an
item count, and the existing scalar OPV/payload records. The outer OPV alone
participates in the cascade and carries `!important`. The importance walker,
deferred-cascade skip walker and diagnostic dump understand this wrapper. URLs
remain stylesheet string-table IDs; bytecode contains no native pointers.

Computed styles retain the existing first-value fields and getters. One optional
pointer holds the additional values, grouped into independent property lists.
Single-value styles allocate no list storage. Arrays have no fixed layer cap and
are allocated to their required length. Images use a typed URL/linear/radial
union; list copying owns its URL references and gradient allocations. Destruction,
resets, style hashing and equality include all extra values. Style composition
propagates allocation failures and releases partially composed styles. Font-relative
positions and gradient stops are resolved before inheritance and style sharing.

The generator emits the optional computed-style pointer. `properties.toml` marks
the list-aware parsers and cascades as manual; scalar accessors and packed flags
remain generated. Changes belong in those sources, never in generated build
outputs. There is no generic CSS value system or bytecode rewrite here.

Boxes allocate extra handle slots only when a later layer needs a URL fetch.
Each object callback carries its layer index, including for animation redraws.
The document object list owns the handles. Block and inline painting traverse
layers back to front, restoring clipping between them. Table-row painting keeps
lower layers beneath translucent cell backgrounds. Rendering remains single
threaded; no new shared mutable cache is introduced.

## Scope and checks

This adds layering to the supported background syntax. It does not add
`background-size` (including shorthand `/`), `background-origin`,
`background-clip`, additional repeat/position syntax, or new attachment painting
semantics. The existing `fixed` attachment rendering limitation remains. Gradient
syntax limits are described in [gradient-stops.md](gradient-stops.md).

`layout_calc_test` covers list matching, invalid input, shorthand defaults,
variables, importance, replacement/reset, inheritance with different font sizes,
style sharing, lifetime and a 300-layer list. `qt_background_layers` uses three
local PNGs and generated gradients to check real pixels, including alpha, image
loading failure, root backgrounds, inline fragments, pseudo-elements and table
rows. It is available with the optional Qt capture build and ImageMagick.

`test/background-layers.html` can also be passed to
`frontends/qt/compare_capture.py` at 760x700 for Chromium comparison. The pixel
expectations in `frontends/qt/test_background_layers.py` can validate either
browser's captured PNG.

Reference: [CSS Backgrounds: layering](https://www.w3.org/TR/css-backgrounds-3/#layering)
and [the background shorthand](https://www.w3.org/TR/css-backgrounds-3/#the-background).

Validation: the 36 background pixel expectations pass in Wisp and Chromium,
and in Wisp with native gradient plotters disabled. The existing 40 gradient
pixel checks also pass in both plotting modes. Saved HotNews desktop/mobile and
Haiku captures are unchanged from the preceding Wisp baselines.
