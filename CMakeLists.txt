cmake_minimum_required(VERSION 3.20)
project(neosurf LANGUAGES C CXX)

# Testing option: ON for Debug builds, OFF for others
set(DEFAULT_ENABLE_TESTS OFF)
if(CMAKE_BUILD_TYPE MATCHES Debug)
  set(DEFAULT_ENABLE_TESTS ON)
endif()
option(NEOSURF_ENABLE_TESTS "Enable unit/integration tests" ${DEFAULT_ENABLE_TESTS})
if(NEOSURF_ENABLE_TESTS)
  include(CTest)
endif()

set(NEOSURF_ABI 1)
set(NEOSURF_MAJOR_VERSION 0)
set(NEOSURF_MINOR_VERSION 1)
set(NEOSURF_VERSION "${NEOSURF_MAJOR_VERSION}.${NEOSURF_MINOR_VERSION}")
math(EXPR NEOSURF_VERSION_NUM "${NEOSURF_MAJOR_VERSION} * 100 + ${NEOSURF_MINOR_VERSION}")

add_definitions(-DNDEBUG -DWITH_OPENSSL -DWITH_BMP -DWITH_GIF -DWITH_NS_SVG -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -DNEOSURF_VERSION=${NEOSURF_VERSION_NUM} 
			-DNEOSURF_VERSION_MAJOR=${NEOSURF_MAJOR_VERSION} -DNEOSURF_VERSION_MINOR=${NEOSURF_MINOR_VERSION})
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wno-unused-parameter -Wno-deprecated-declarations -Wno-unused-but-set-variable)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-strict-prototypes>)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-int-conversion>)

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
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON3 ${Python3_EXECUTABLE})
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

option(NEOSURF_USE_RSVG "Include librsvg SVG renderer" OFF)
if(${NEOSURF_USE_RSVG})
    add_definitions(-DWITH_RSVG)
endif()

set(DEFAULT_GTK_FRONTEND OFF)
set(DEFAULT_VI_FRONTEND OFF)
set(DEFAULT_WINDOWS_FRONTEND OFF)
set(DEFAULT_QT_FRONTEND OFF)
set(DEFAULT_MONKEY_FRONTEND OFF)
if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(DEFAULT_MONKEY_FRONTEND ON)
endif()

if(WIN32)
	set(DEFAULT_WINDOWS_FRONTEND ON)
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
	set(DEFAULT_QT_FRONTEND ON)
endif()

option(NEOSURF_BUILD_GTK_FRONTEND "Build and install the bundled Gtk frontend" ${DEFAULT_GTK_FRONTEND})
option(NEOSURF_BUILD_VI_FRONTEND "Build and install the bundled Visurf frontend" ${DEFAULT_VI_FRONTEND})
option(NEOSURF_BUILD_WINDOWS_FRONTEND "Build and install the bundled Windows frontend" ${DEFAULT_WINDOWS_FRONTEND})
option(NEOSURF_BUILD_QT_FRONTEND "Build and install the bundled Qt frontend" ${DEFAULT_QT_FRONTEND})
option(NEOSURF_BUILD_MONKEY_FRONTEND "Build the Monkey testing frontend" ${DEFAULT_MONKEY_FRONTEND})

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
if(NEOSURF_BUILD_MONKEY_FRONTEND)
    add_definitions(-Dmonkey -Dnsmonkey)
endif()
option(NEOSURF_INSTALL_NSGENBIND "Installs the nsgenbind utility with neosurf" OFF)
option(NEOSURF_INSTALL_GEN_PARSER "Installs the libcss gen_parser utility with neosurf" OFF)
option(NEOSURF_BUILD_XXD "Build and install a complimentary xxd implementation" OFF)
option(NEOSURF_WERROR "Treat warnings as errors" ON)
option(NEOSURF_ENABLE_GPROF "Enable gprof profiling instrumentation (-pg)" OFF)
option(NEOSURF_ENABLE_FINSTRUMENT "Enable function tracing (-finstrument-functions)" OFF)

if(NEOSURF_ENABLE_GPROF AND NEOSURF_ENABLE_FINSTRUMENT)
    message(FATAL_ERROR "Enable only one of NEOSURF_ENABLE_GPROF or NEOSURF_ENABLE_FINSTRUMENT")
endif()

if(NEOSURF_WERROR)
	if(MSVC)
		add_compile_options(/WX)
	else()
		add_compile_options(-Werror)
	endif()
endif()

if(DEFINED NETSURF_LOG_LEVEL)
    string(TOUPPER "${NETSURF_LOG_LEVEL}" NETSURF_LOG_LEVEL_UPPER)
    if(NETSURF_LOG_LEVEL_UPPER STREQUAL "ERROR")
        add_compile_definitions(NETSURF_LOG_LEVEL=NS_ERROR)
    elseif(NETSURF_LOG_LEVEL_UPPER STREQUAL "WARNING")
        add_compile_definitions(NETSURF_LOG_LEVEL=NS_WARNING)
    elseif(NETSURF_LOG_LEVEL_UPPER STREQUAL "INFO")
        add_compile_definitions(NETSURF_LOG_LEVEL=INFO)
    elseif(NETSURF_LOG_LEVEL_UPPER STREQUAL "DEBUG")
        add_compile_definitions(NETSURF_LOG_LEVEL=DEBUG)
    elseif(NETSURF_LOG_LEVEL_UPPER STREQUAL "VERBOSE")
        add_compile_definitions(NETSURF_LOG_LEVEL=VERBOSE)
    elseif(NETSURF_LOG_LEVEL_UPPER STREQUAL "DEEPDEBUG")
        add_compile_definitions(NETSURF_LOG_LEVEL=DEEPDEBUG)
    elseif(NETSURF_LOG_LEVEL_UPPER STREQUAL "CRITICAL")
        add_compile_definitions(NETSURF_LOG_LEVEL=CRITICAL)
    else()
        add_compile_definitions(NETSURF_LOG_LEVEL=${NETSURF_LOG_LEVEL})
    endif()
elseif(CMAKE_BUILD_TYPE MATCHES Debug)
    add_compile_definitions(NETSURF_LOG_LEVEL=DEEPDEBUG)
else()
    add_compile_definitions(NETSURF_LOG_LEVEL=INFO)
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug AND NOT NEOSURF_ENABLE_GPROF)
    if((WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang") OR UNIX)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

if(NEOSURF_ENABLE_GPROF)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Gprof profiling enabled (-pg) for executables")
    else()
        message(FATAL_ERROR "NEOSURF_ENABLE_GPROF requires GCC or Clang compilers")
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
set(CPACK_PACKAGE_VERSION_MAJOR "${NEOSURF_MAJOR_VERSION}")
set(CPACK_PACKAGE_VERSION_MINOR "${NEOSURF_MINOR_VERSION}")
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
	# Use a distinct icon for the uninstaller
	set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/src/resources/uninstall.ico")
	set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_INSTALL_BINDIR}/neosurf-windows.exe")
	# Run after install
	set(CPACK_NSIS_MUI_FINISHPAGE_RUN "neosurf-windows.exe")
	
	# Uninstall previous versions
	set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
	
	# Executables to create shortcuts for
	# Note: This assumes the executable name is 'neosurf-windows' as defined in frontends/windows/CMakeLists.txt
	# If building for Windows, the executable might be different depending on configuration
	set(CPACK_NSIS_EXECUTABLES "${CMAKE_INSTALL_BINDIR}/neosurf-windows.exe")
	set(CPACK_NSIS_MENU_LINKS "${CMAKE_INSTALL_BINDIR}/neosurf-windows.exe" "NeoSurf")
endif()

include(CPack)

if(NEOSURF_ENABLE_TESTS)
  function(add_libcss_test_exe name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE
      ${CMAKE_SOURCE_DIR}/contrib/libcss/src
      ${CMAKE_SOURCE_DIR}/contrib/libcss/include
      ${CMAKE_SOURCE_DIR}/contrib/libcss/test
      ${CMAKE_BINARY_DIR}/contrib/libcss
    )
    target_link_libraries(${name} css nsutils parserutils)
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:css> $<TARGET_FILE_DIR:${name}>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:parserutils> $<TARGET_FILE_DIR:${name}>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:nsutils> $<TARGET_FILE_DIR:${name}>
    )
  endfunction()

  set(LIBCSSL_TEST_DIR ${CMAKE_SOURCE_DIR}/contrib/libcss/test)
  set(LIBCSSL_DATA_DIR ${LIBCSSL_TEST_DIR}/data)

  add_libcss_test_exe(libcss_parse_auto ${LIBCSSL_TEST_DIR}/parse-auto.c)
  file(GLOB LIBCSS_PARSE_DAT "${LIBCSSL_DATA_DIR}/parse/*.dat")
  foreach(f ${LIBCSS_PARSE_DAT})
    get_filename_component(n "${f}" NAME)
    string(REPLACE "." "_" n "${n}")
    add_test(NAME libcss_parse_auto_${n} COMMAND libcss_parse_auto "${f}")
  endforeach()

  add_libcss_test_exe(libcss_parse2_auto ${LIBCSSL_TEST_DIR}/parse2-auto.c)
  file(GLOB LIBCSS_PARSE2_DAT "${LIBCSSL_DATA_DIR}/parse2/*.dat")
  foreach(f ${LIBCSS_PARSE2_DAT})
    get_filename_component(n "${f}" NAME)
    string(REPLACE "." "_" n "${n}")
    add_test(NAME libcss_parse2_auto_${n} COMMAND libcss_parse2_auto "${f}")
  endforeach()

  add_libcss_test_exe(libcss_select ${LIBCSSL_TEST_DIR}/select.c)
  file(GLOB LIBCSS_SELECT_DAT "${LIBCSSL_DATA_DIR}/select/*.dat")
  foreach(f ${LIBCSS_SELECT_DAT})
    get_filename_component(n "${f}" NAME)
    string(REPLACE "." "_" n "${n}")
    add_test(NAME libcss_select_${n} COMMAND libcss_select "${f}")
  endforeach()

  add_libcss_test_exe(libcss_select2 ${LIBCSSL_TEST_DIR}/select2.c)
  file(GLOB LIBCSS_SELECT2_DAT "${LIBCSSL_DATA_DIR}/select2/*.dat")
  foreach(f ${LIBCSS_SELECT2_DAT})
    get_filename_component(n "${f}" NAME)
    string(REPLACE "." "_" n "${n}")
    add_test(NAME libcss_select2_${n} COMMAND libcss_select2 "${f}")
  endforeach()

  add_libcss_test_exe(libcss_lex_auto ${LIBCSSL_TEST_DIR}/lex-auto.c)
  file(GLOB LIBCSS_LEX_DAT "${LIBCSSL_DATA_DIR}/lex/*.dat")
  foreach(f ${LIBCSS_LEX_DAT})
    get_filename_component(n "${f}" NAME)
    string(REPLACE "." "_" n "${n}")
    add_test(NAME libcss_lex_auto_${n} COMMAND libcss_lex_auto "${f}")
  endforeach()

  add_libcss_test_exe(libcss_csdetect ${LIBCSSL_TEST_DIR}/csdetect.c)
  file(GLOB LIBCSS_CSDETECT_DAT "${LIBCSSL_DATA_DIR}/csdetect/*.dat")
  foreach(f ${LIBCSS_CSDETECT_DAT})
    get_filename_component(n "${f}" NAME)
    string(REPLACE "." "_" n "${n}")
    add_test(NAME libcss_csdetect_${n} COMMAND libcss_csdetect "${f}")
  endforeach()

  add_libcss_test_exe(libcss_number ${LIBCSSL_TEST_DIR}/number.c)
  file(GLOB LIBCSS_NUMBER_DAT "${LIBCSSL_DATA_DIR}/number/*.dat")
  foreach(f ${LIBCSS_NUMBER_DAT})
    get_filename_component(n "${f}" NAME)
    string(REPLACE "." "_" n "${n}")
    add_test(NAME libcss_number_${n} COMMAND libcss_number "${f}")
  endforeach()

  add_libcss_test_exe(libcss_css21 ${LIBCSSL_TEST_DIR}/css21.c)
  file(GLOB LIBCSS_CSS_FILES "${LIBCSSL_DATA_DIR}/css/*.css")
  foreach(f ${LIBCSS_CSS_FILES})
    get_filename_component(n "${f}" NAME_WE)
    add_test(NAME libcss_css21_${n} COMMAND libcss_css21 "${f}")
  endforeach()
endif()
