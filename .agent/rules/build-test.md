---
trigger: always_on
description: build instructions
---

# Build and Test Instructions

---

## Linux

### Build

1. Configure with CMake (from the build directory):
   ```bash
   cmake -DCMAKE_BUILD_TYPE=Debug -GNinja -DWISP_ENABLE_PERF_TRACE=OFF -DWISP_ENABLE_TESTS=ON ..
   ```
   > **Note**: Use `-DNEOSURF_ENABLE_PERF_TRACE=ON` when measuring performance.

2. Build with Ninja:
   ```bash
   ninja
   ```

---

## Windows (MSYS2/MinGW)

These instructions are for building on **Windows** using MSYS2/MinGW.

### Build

1. Set up the PATH environment (PowerShell):
   ```powershell
   $env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH
   ```

2. Configure with CMake:
   ```powershell
   cmake -S . -B build-ninja -G Ninja -DWISP_WERROR=OFF -DPKG_CONFIG_EXECUTABLE=C:/msys64/mingw64/bin/pkgconf.exe -DCMAKE_C_COMPILER=gcc
   ```

3. Build with Ninja:
   ```powershell
   ninja -C build-ninja
   ```

### Test

Run tests with CTest:
```powershell
ctest --test-dir build-ninja -j 8 --output-on-failure
```