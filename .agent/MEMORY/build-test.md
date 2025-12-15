Build
`C:\msys64\usr\bin\bash.exe -l -c "cd /c/proj/neosurf-mcirsta && export PATH=/mingw64/bin:$PATH && ninja -C build-ninja"`

Test
`ctest --test-dir build-ninja -j 8 --output-on-failure`
