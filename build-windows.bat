mkdir build
cd build
cmake -G "MinGW Makefiles" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DIconv_INCLUDE_DIR="C:/msys64/mingw64/include" -DIconv_LIBRARY="C:/msys64/mingw64/lib/libiconv.dll.a" -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release --target wisp-windows
.\frontends\windows\wisp-windows.exe
