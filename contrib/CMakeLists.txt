add_subdirectory(libcss)
add_subdirectory(libdom)
add_subdirectory(libhubbub)
add_subdirectory(libnsbmp)
add_subdirectory(libnsgif)
add_subdirectory(libnsutils)
add_subdirectory(libparserutils)
add_subdirectory(libsvgtiny)

# QuickJS-ng needs to be compiled with -fPIC for linking into shared library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(quickjs-ng)
