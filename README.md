# Wisp

This project is a fork of Netsurf with various fixes and improvements, a new JS engine, and a revamped build system.

## Why?
Wisp has a different development vision from Netsurf. While Netsurf is a browser for the "old web", Wisp aspires to be a browser for the "modern web".
We appreciate the philosophy of Netsurf, and intend to keep the spirit of the project alive: a lean, small, and portable browser.

![Wisp](img/wisp_home.png?raw=true "Wisp Homepage")
![GNU.org](img/wisp_gnu.png?raw=true "GNU.org")

## Development
Current development is focused on adding compatibility with modern websites, integrating the QuickJS-ng JS engine and porting to different platforms, most notably modern Windows using Direct2D, OS X, Haiku and Remarkable.

For repeatable page images and DOM/layout diagnostics, see the optional
[Qt rendering capture tool](frontends/qt/CAPTURE.md).

## Biggest differences from Netsurf
* Removed compatibility for super old and/or obscure libraries/software/operating systems
* Dedicated LibreSSL support
* Numerous privacy improvements
* Rewritten build system
* Simplified frontend development

## Known Issues
All other frontends, except for the Qt one on Linux and the GDI one on Windows, are not tested and probably don't work.
The JS engine using QuickJS is just a stub.

## Building and installation
Wisp can be built:
* On Windows (for the Windows frontend) using MSYS2 and the MinGW-w64 toolchain.
* On Linux (for the Qt frontend) using CMake and ninja or make.


Wisp intends to be portable, keeping a lean C99 codebase and minimal dependencies.

At build-time, Wisp requires the following programs:
* python3
* cmake
* any CMake-compatible build utility (typically make or ninja)
* gperf
* pkg-config or pkgconf

WebP support (`WISP_USE_WEBP=ON`, the default) requires `libwebp` and
`libwebpdemux`. Animated WebP images display their fully composited first frame;
playback is not currently supported.

The Qt frontend also requires FreeType development files. FreeType decodes
downloaded font containers so each CSS face can be registered with a private Qt
identity; text shaping and rendering still use Qt. WOFF2 decoding requires a
FreeType build with Brotli support.

Web-font selection uses the CSS family and weight/style descriptors before
constructing a `QFont`. Each downloaded face receives a private in-memory native
name because Qt's public `QFont` API selects by name, not a font resource handle.
The adapter aligns native style metadata with the CSS descriptors so fallback
and synthesis see the CSS request too. Glyphs, advances and shaping tables are
preserved; the original downloaded files are unchanged.

Web-font downloads and CSS family bindings belong to the document, as required
by [CSS Fonts](https://www.w3.org/TR/css-fonts-3/#font-face-rule). Applicable rules
are collected from its loaded stylesheets, including imports; a page waits only
for its own fonts. The existing URL cache shares downloaded bytes. Qt additionally
shares immutable native faces when both file bytes and CSS weight/style descriptors
match, without sharing the document's family bindings. Native resources are freed
when their last document releases them. Font selection caches include the document
identity; measurement and painting reuse Qt's resolved font and glyph caches.

Regression coverage: `font_face_test`, `qt_font_documents`, `qt_font_face_isolation`,
`qt_font_resources`, `qt_font_weight` and `qt_font_fallback`. The resource test also
accepts a font file followed by `--benchmark` to measure repeated document bindings
and their first measurement/paint, checking identical pixels. These are component
timings, not full-page or network benchmarks. The Windows frontend now scopes CSS
aliases and releases downloaded-font handles with the document; its existing GDI
native-name collision handling still needs separate work and native validation.

For an optimized Linux build, select Release explicitly (the Ninja generator
does not select a build type):

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

LTO is enabled by default for non-Debug configurations. With GCC, the link step
uses `-flto=auto` so LTO compilation can use the available CPU cores while keeping
full optimization. Ninja's own parallelism is left at its default.

With CMake 3.29 or newer, an installed alternative linker can be selected using
`-DCMAKE_LINKER_TYPE=GOLD` or `-DCMAKE_LINKER_TYPE=MOLD`. This is optional; parallel
LTO also works with the default GNU linker. LLVM lld supports Clang's LTO format,
not GCC's, so it cannot directly replace the linker in a GCC LTO build.
