# Wisp application icons

`wisp-small.png` is the source for the simplified application/taskbar mark.
Its thicker blue curves and transparent background are intended for small UI
sizes. The large logo (`Wisp-Reference.png`), wordmarks (`wisp.png` and
`wisp_no_gradient.png`), and page favicon resources remain separate.

The small mark is exported to:

- `wisp.ico`: 16, 20, 24, 32, 40, 48 and 64 pixel frames, used by Qt and Windows.
- `../../appimage/wisp.png`: 48 pixel desktop/launcher icon, also installed by
  the Qt and GTK builds.
- `../../frontends/gtk/res/wisp.xpm` and `wisp-16x16.xpm`: legacy GTK window icons.
- `../../frontends/beos/res.rdef`: 32/16 pixel application icon resources in
  Haiku's standard B_CMAP8 palette (transparent index 255). Toolbar icons are
  unrelated.

Run these exports from the repository root with ImageMagick 7:

```sh
magick src/resources/wisp-small.png -define icon:auto-resize=64,48,40,32,24,20,16 src/resources/wisp.ico
magick src/resources/wisp-small.png -filter Lanczos -resize 48x48 appimage/wisp.png
magick appimage/wisp.png -channel A -threshold 50% +channel frontends/gtk/res/wisp.xpm
magick src/resources/wisp-small.png -filter Lanczos -resize 16x16 -channel A -threshold 50% +channel frontends/gtk/res/wisp-16x16.xpm
```

For Haiku, resize the source with the same Lanczos filter to 32/16 pixels,
map alpha below 128 to index 255, and map each remaining RGB pixel to the
nearest opaque entry by squared RGB distance in Haiku's standard palette.
Only replace `BEOS:L:STD_ICON` and `BEOS:M:STD_ICON` in `res.rdef`.

## Source artwork

Generated with the built-in imagegen tool on 2026-10-06, using
`Wisp-Reference.png` as the edit reference and a transparent background.
Generation prompt:

> Use case: logo-brand. Edit target: the attached existing Wisp browser wind logo. Create one simplified optical-size variant for a 16–48 pixel desktop taskbar icon, keeping the recognizable Wisp identity: three flowing wind ribbons with a large curl at the upper right and a smaller opposing curl below. Make it compact, balanced, and much easier to recognize when tiny: bolder solid curves, fewer thin tapered tips, wider clear gaps between the three ribbons, large open curl counters. Flat clean silhouette in a single saturated medium azure blue, approximately #2785D5. Preserve the overall left-to-right flowing gesture and curl arrangement. Make the blue shape occupy roughly 84 percent of the square canvas with even safe margins. Sharp smooth vector-like edges, no gradients, no pale highlights, no bevels, no shadow, no outlines, no background tile or enclosing circle, no text or wordmark. Genuine transparent background. Output only the single centered finished icon, no mockup, no grid, no labels. This is a small-size derivative; the original full-size logo must remain recognizable.
