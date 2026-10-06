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
