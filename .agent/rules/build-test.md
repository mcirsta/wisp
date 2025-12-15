---
trigger: always_on
---

Build  `$env:PATH = "C:\\msys64\\mingw64\\bin;C:\\msys64\\usr\\bin;" + $env:PATH` `cmake -S . -B build-ninja -G Ninja -DNEOSURF_WERROR=OFF -DPKG_CONFIG_EXECUTABLE=C:/msys64/mingw64/bin/pkgconf.exe -DCMAKE_C_COMPILER=gcc` `ninja -C build-ninja`  Test  `ctest --test-dir build-ninja -j 8 --output-on-failure`