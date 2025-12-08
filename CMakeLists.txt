cmake_minimum_required(VERSION 3.20)
project(neosurf LANGUAGES C)

set(NEOSURF_ABI 14)
set(NEOSURF_VERSION 15)

add_definitions(-DNDEBUG -DWITH_OPENSSL -DWITH_BMP -DWITH_GIF -DWITH_NS_SVG -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -DNEOSURF_VERSION=${NEOSURF_VERSION})
add_compile_options(-std=c99 -Wno-unused-parameter -Wno-deprecated-declarations -Wno-strict-prototypes -Wno-unused-but-set-variable -Wno-int-conversion)

include(GNUInstallDirs)

# Abort if source or build directory has white space
if(${CMAKE_SOURCE_DIR} MATCHES "^(.* +.*)+$")
	message(FATAL_ERROR "neosurf's source cannot be in a directory containing white space. Please move to a different location and try again.")
endif()
if(${CMAKE_BINARY_DIR} MATCHES "^(.* +.*)+$")
	message(FATAL_ERROR "neosurf cannot be built in a directory containing white space. Please move to a different location and try again.")
endif()

# Check for required programs
find_program(GPERF gperf)
if(${GPERF} STREQUAL GPERF-NOTFOUND)
	message(FATAL_ERROR "Missing `gperf` utility. Please refer to your distribution package manager for obtaining this.")
endif()
find_program(PYTHON3 python3)
if(${PYTHON3} STREQUAL PYTHON3-NOTFOUND)
	message(FATAL_ERROR "Missing Python3 executable. Please refer to your distribution package manager for obtaining this.")
endif()
find_program(FLEX flex)
if(${FLEX} STREQUAL FLEX-NOTFOUND)
	message(FATAL_ERROR "Missing `flex` utility. Please refer to your distribution package manager for obtaining this.")
endif()
find_program(BISON bison)
find_program(BYACC byacc)
if(${BISON} STREQUAL BISON-NOTFOUND AND ${BYACC} STREQUAL BYACC-NOTFOUND)
	message(FATAL_ERROR "Missing `bison` or `byacc` utility. Please refer to your distribution package manager for obtaining one or the other.")
else()
	if(${BISON} STREQUAL BISON-NOTFOUND)
		set(YACC byacc)
	else()
		set(YACC bison)
	endif()
endif()
message("yacc program found: ${YACC}")

find_program(PKGCONFIG pkg-config)
find_program(PKGCONF pkgconf)
if(${PKGCONFIG} STREQUAL PKGCONFIG-NOTFOUND AND ${PKGCONF} STREQUAL PKGCONF-NOTFOUND)
	message(FATAL_ERROR "Missing `pkg-config` or `pkgconf` utility. Please refer to your distribution package manager for obtaining one or the other.")
endif()
find_program(M4 m4)
if(${M4} STREQUAL M4-NOTFOUND)
	message(FATAL_ERROR "Missing `m4` utility. Please refer to your distribution package manager for obtaining this.")
endif()

option(NEOSURF_USE_PNG "Include png support" ON)
if(${NEOSURF_USE_PNG})
	add_definitions(-DWITH_PNG)
	set(PNG_SRC content/handlers/image/png.c)
endif()
option(NEOSURF_USE_JPEG "Include jpeg support" ON)
if(${NEOSURF_USE_JPEG})
	add_definitions(-DWITH_JPEG)
	set(JPEG_SRC content/handlers/image/jpeg.c)
endif()
option(NEOSURF_USE_WEBP "Include webp support" ON)
if(${NEOSURF_USE_WEBP})
	add_definitions(-DWITH_WEBP)
	set(WEBP_SRC content/handlers/image/webp.c)
endif()

set(DEFAULT_GTK_FRONTEND OFF)
set(DEFAULT_VI_FRONTEND OFF)
set(DEFAULT_WINDOWS_FRONTEND OFF)
set(DEFAULT_QT_FRONTEND OFF)

if(WIN32)
	set(DEFAULT_WINDOWS_FRONTEND ON)
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
	set(DEFAULT_QT_FRONTEND ON)
endif()

option(NEOSURF_BUILD_GTK_FRONTEND "Build and install the bundled Gtk frontend" ${DEFAULT_GTK_FRONTEND})
option(NEOSURF_BUILD_VI_FRONTEND "Build and install the bundled Visurf frontend" ${DEFAULT_VI_FRONTEND})
option(NEOSURF_BUILD_WINDOWS_FRONTEND "Build and install the bundled Windows frontend" ${DEFAULT_WINDOWS_FRONTEND})
option(NEOSURF_BUILD_QT_FRONTEND "Build and install the bundled Qt frontend" ${DEFAULT_QT_FRONTEND})

if(NEOSURF_BUILD_GTK_FRONTEND)
	add_definitions(-Dgtk -Dnsgtk)
endif()

if(NEOSURF_BUILD_VI_FRONTEND)
	add_definitions(-Dvi -Dnsvi)
endif()

if(NEOSURF_BUILD_QT_FRONTEND)
	add_definitions(-Dnsqt)
endif()

if(NEOSURF_BUILD_WINDOWS_FRONTEND)
	add_definitions(-Dnswin32 -DWINVER=0x0600 -D_WIN32_WINNT=0x0600 -D_WIN32_WINDOWS=0x0600 -D_WIN32_IE=0x0600)
endif()
option(NEOSURF_INSTALL_NSGENBIND "Installs the nsgenbind utility with neosurf" OFF)
option(NEOSURF_INSTALL_GEN_PARSER "Installs the libcss gen_parser utility with neosurf" OFF)
option(NEOSURF_BUILD_XXD "Build and install a complimentary xxd implementation" OFF)
option(NEOSURF_WERROR "Treat warnings as errors" ON)

if(NEOSURF_WERROR)
	if(MSVC)
		add_compile_options(/WX)
	else()
		add_compile_options(-Werror)
	endif()
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
	add_definitions(-DNETSURF_LOG_LEVEL=DEEPDEBUG)

	if((WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang") OR UNIX)
		add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
		add_link_options(-fsanitize=address,undefined)
	endif()
endif()

include_directories(
	"${CMAKE_SOURCE_DIR}/contrib/libsvgtiny/include"
	"${CMAKE_SOURCE_DIR}/contrib/libdom/include"
	"${CMAKE_SOURCE_DIR}/contrib/libnsutils/include"
	"${CMAKE_SOURCE_DIR}/contrib/libnsgif/include"
	"${CMAKE_BINARY_DIR}/contrib/libsvgtiny"
	"${CMAKE_SOURCE_DIR}/contrib/libnsbmp/include"
	"${CMAKE_SOURCE_DIR}/contrib/libhubbub/include"
	"${CMAKE_SOURCE_DIR}/contrib/libparserutils/include"
	"${CMAKE_SOURCE_DIR}/contrib/libcss/include"
	"${CMAKE_BINARY_DIR}/contrib/libcss"
	"${CMAKE_SOURCE_DIR}/contrib/libsvgtiny/src"
	"${CMAKE_BINARY_DIR}/contrib/libhubbub/src"
	"${CMAKE_SOURCE_DIR}/frontends"
)

add_subdirectory(contrib)
add_subdirectory(src)
add_subdirectory(frontends)

#TODO: quickjs/wasm3

#FIXME: gtk frontend seems to segfault if no proper GTK cursor theme is installed
#FIXME: will fail in mysterious ways if user does not have write access to their ~/.config/

set(CPACK_PACKAGE_NAME "NeoSurf")
set(CPACK_PACKAGE_VENDOR "NeoSurf")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Web Browser")
set(CPACK_PACKAGE_VERSION "${NEOSURF_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${NEOSURF_VERSION}")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "NeoSurf")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# NSIS Specific Configuration
if(WIN32)
	set(CPACK_GENERATOR "NSIS;ZIP")
	set(CPACK_NSIS_DISPLAY_NAME "NeoSurf Web Browser")
	set(CPACK_NSIS_PACKAGE_NAME "NeoSurf")
	set(CPACK_NSIS_URL_INFO_ABOUT "https://www.neosurf-browser.org/")
	set(CPACK_NSIS_HELP_LINK "https://www.neosurf-browser.org/")
	set(CPACK_NSIS_CONTACT "https://www.neosurf-browser.org/")
	set(CPACK_NSIS_MODIFY_PATH OFF)
	
	# Icon configuration (using the one from src/resources)
	set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/src/resources/neosurf.ico")
	# Use default NSIS uninstall icon
	# set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/src/resources/neosurf.ico")
	
	# Run after install
	set(CPACK_NSIS_MUI_FINISHPAGE_RUN "${CMAKE_INSTALL_BINDIR}/neosurf-windows.exe")
	
	# Uninstall previous versions
	set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
	
	# Executables to create shortcuts for
	# Note: This assumes the executable name is 'neosurf-windows' as defined in frontends/windows/CMakeLists.txt
	# If building for Windows, the executable might be different depending on configuration
	set(CPACK_NSIS_EXECUTABLES "${CMAKE_INSTALL_BINDIR}/neosurf-windows.exe")
	set(CPACK_NSIS_MENU_LINKS "${CMAKE_INSTALL_BINDIR}/neosurf-windows.exe" "NeoSurf")
endif()

include(CPack)
