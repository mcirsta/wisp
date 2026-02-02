find_package(PkgConfig)

include_directories(
	"${CMAKE_SOURCE_DIR}/src"
	"${CMAKE_SOURCE_DIR}/include"
	"${CMAKE_BINARY_DIR}"
)

if(WISP_USE_JPEG)
	pkg_check_modules(LIBJPEG REQUIRED libjpeg)
	include_directories(${LIBJPEG_INCLUDE_DIRS})
endif()

if(WISP_USE_PNG)
    pkg_check_modules(LIBPNG REQUIRED libpng)
    include_directories(${LIBPNG_INCLUDE_DIRS})
endif()

pkg_check_modules(LIBCURL REQUIRED libcurl)
include_directories(${LIBCURL_INCLUDE_DIRS})
add_definitions(-DWITH_CURL)

pkg_check_modules(LIBCRYPTO REQUIRED libcrypto)
include_directories(${LIBCRYPTO_INCLUDE_DIRS})

pkg_check_modules(LIBPSL REQUIRED libpsl)
include_directories(${LIBPSL_INCLUDE_DIRS})

pkg_check_modules(LIBUTF8PROC REQUIRED libutf8proc)
include_directories(${LIBUTF8PROC_INCLUDE_DIRS})

if(WISP_USE_WEBP)
    pkg_check_modules(LIBWEBP REQUIRED libwebp)
    include_directories(${LIBWEBP_INCLUDE_DIRS})
endif()

if(WISP_USE_RSVG)
    pkg_check_modules(LIBRSVG REQUIRED librsvg-2.0)
    include_directories(${LIBRSVG_INCLUDE_DIRS})
    pkg_check_modules(CAIRO REQUIRED cairo)
    include_directories(${CAIRO_INCLUDE_DIRS})
    set(RSVG_SRC content/handlers/image/rsvg246.c)
endif()

pkg_check_modules(ZLIB REQUIRED zlib)
include_directories(${ZLIB_INCLUDE_DIRS})

pkg_check_modules(LIBSSL REQUIRED libssl)
include_directories(${LIBSSL_INCLUDE_DIRS})

pkg_check_modules(LIBXML2 libxml-2.0)
if(LIBXML2_FOUND)
	include_directories(${LIBXML2_INCLUDE_DIRS})
endif()

pkg_check_modules(LIBREGEX regex)
if(LIBREGEX_FOUND)
    include_directories(${LIBREGEX_INCLUDE_DIRS})
endif()

string(REPLACE "." ";" LIBSSL_VERSION_STRING ${LIBSSL_VERSION})

list(GET LIBSSL_VERSION_STRING 0 LIBSSL_VERSION_MAJOR)
if(LIBSSL_VERSION_MAJOR EQUAL 1)
message(FATAL_ERROR "Wisp does not support OpenSSL 1.x.x")
endif()

# QuickJS-ng bindings
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/quickjs_bindings.cmake)

 
 
 
set(WISP_LIB_TYPE SHARED)
if(WISP_ENABLE_GPROF OR WISP_ENABLE_FINSTRUMENT)
    set(WISP_LIB_TYPE STATIC)
endif()

add_library(wisp ${WISP_LIB_TYPE}
	content/content.c
	content/content_factory.c
	content/fetch.c
	content/hlcache.c
	content/llcache.c
	content/mimesniff.c
	content/textsearch.c
	content/urldb.c
	content/no_backing_store.c
	content/fs_backing_store.c
	content/fetchers/data.c
	content/fetchers/resource.c
	content/fetchers/curl.c
	content/fetchers/about/about.c
	content/fetchers/about/choices.c
	content/fetchers/about/testament.c
	content/fetchers/about/websearch.c
	content/fetchers/about/blank.c
	content/fetchers/about/certificate.c
	content/fetchers/about/chart.c
	content/fetchers/about/config.c
	content/fetchers/about/imagecache.c
	content/fetchers/about/nscolours.c
	content/fetchers/about/query.c
	content/fetchers/about/query_auth.c
	content/fetchers/about/query_fetcherror.c
	content/fetchers/about/query_privacy.c
	content/fetchers/about/query_timeout.c
	content/fetchers/file/dirlist.c
	content/fetchers/file/file.c
	content/handlers/css/css.c
	content/handlers/css/dump.c
	content/handlers/css/internal.c
	content/handlers/css/hints.c
	content/handlers/css/select.c
	content/handlers/html/box_construct.c
	content/handlers/html/box_inspect.c
	content/handlers/html/box_manipulate.c
	content/handlers/html/box_normalise.c
	content/handlers/html/box_special.c
	content/handlers/html/box_textarea.c
	content/handlers/html/css.c
	content/handlers/html/css_fetcher.c
	content/handlers/html/dom_event.c
	content/handlers/html/font.c
	content/handlers/html/font_face.c
	content/handlers/html/form.c
	content/handlers/html/forms.c
	content/handlers/html/html.c
	content/handlers/html/html_svg.c
	content/handlers/html/imagemap.c
	content/handlers/html/interaction.c
	content/handlers/html/layout.c
	content/handlers/html/layout_flex.c
	content/handlers/html/layout_grid.c
	content/handlers/html/object.c
	content/handlers/html/redraw.c
	content/handlers/html/redraw_border.c
	content/handlers/html/redraw_helpers.c
	content/handlers/html/stacking.c
	content/handlers/html/script.c
	content/handlers/html/table.c
	content/handlers/html/textselection.c
	content/handlers/image/image.c
	content/handlers/image/image_cache.c
	content/handlers/image/bmp.c
	content/handlers/image/gif.c
	content/handlers/image/ico.c
	${JPEG_SRC}
	${PNG_SRC}
	content/handlers/image/svg.c
	${RSVG_SRC}
	#content/handlers/image/video.c # requires gstreamer TODO: replace with ffmpeg/libmpv
	${WEBP_SRC}
	content/handlers/javascript/fetcher.c
	content/handlers/javascript/content.c
	# QuickJS-ng sources
	content/handlers/javascript/quickjs/qjs.c
	content/handlers/javascript/quickjs/timers.c
	content/handlers/javascript/quickjs/navigator.c
	content/handlers/javascript/quickjs/location.c
	content/handlers/javascript/quickjs/document.c
	content/handlers/javascript/quickjs/storage.c
	content/handlers/javascript/quickjs/event_target.c
	content/handlers/javascript/quickjs/xhr.c
	content/handlers/text/textplain.c
	desktop/cookie_manager.c
	desktop/knockout.c
	desktop/hotlist.c
	desktop/plot_style.c
	desktop/print.c
	desktop/search.c
	desktop/searchweb.c
	desktop/scrollbar.c
	desktop/textarea.c
	desktop/version.c
	desktop/system_colour.c
	desktop/local_history.c
	desktop/global_history.c
	desktop/treeview.c
	desktop/page-info.c
	desktop/browser.c
	desktop/browser_window.c
	desktop/browser_history.c
	desktop/download.c
	desktop/frames.c
	desktop/wisp.c
	desktop/cw_helper.c
	desktop/save_complete.c
	desktop/save_text.c
	desktop/selection.c
	desktop/textinput.c
	desktop/gui_factory.c
	desktop/save_pdf.c
	desktop/bitmap.c
	utils/bloom.c
	utils/corestrings.c
	utils/file.c
	utils/filepath.c
	utils/hashmap.c
	utils/hashtable.c
	utils/idna.c
	utils/libdom.c
	utils/log.c
	utils/messages.c
	utils/nscolour.c
	utils/nsoption.c
	utils/punycode.c
	utils/ssl_certs.c
	utils/talloc.c
	utils/time.c
	utils/url.c
	utils/useragent.c
	utils/utf8.c
	utils/utils.c
	utils/http/generics.c
	utils/http/primitives.c
	utils/http/parameter.c
	utils/http/cache-control.c
	utils/http/content-disposition.c
	utils/http/content-type.c
	utils/http/strict-transport-security.c
	utils/nsurl/nsurl.c
	utils/nsurl/parse.c
	# QuickJS-ng generated bindings
	${CMAKE_BINARY_DIR}/quickjs/console.c
	${CMAKE_BINARY_DIR}/quickjs/window.c
)
# QuickJS-ng dependencies
add_dependencies(wisp quickjs_bindings)
set_target_properties(wisp PROPERTIES SOVERSION ${WISP_ABI})

if(WISP_ENABLE_GPROF)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(wisp PRIVATE -pg -g -fno-omit-frame-pointer)
    endif()
endif()

if(WISP_ENABLE_FINSTRUMENT)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(wisp PRIVATE -finstrument-functions -g -fno-omit-frame-pointer)
    endif()
endif()

if(WISP_BUILD_XXD)
	add_executable(xxd
		xxd.c
	)
endif()

set(WISP_COMMON_LIBS css dom nsutils parserutils nsgif nsbmp svgtiny ${LIBCRYPTO_LIBRARIES} ${LIBPNG_LIBRARIES} ${LIBCURL_LIBRARIES} ${LIBJPEG_LIBRARIES} ${LIBWEBP_LIBRARIES} ${LIBPSL_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBSSL_LIBRARIES} ${LIBUTF8PROC_LIBRARIES} ${LIBREGEX_LIBRARIES})

if(WISP_USE_RSVG)
    list(APPEND WISP_COMMON_LIBS ${LIBRSVG_LIBRARIES} ${CAIRO_LIBRARIES})
endif()

if(WIN32)
	list(APPEND WISP_COMMON_LIBS ws2_32)
endif()

# Add QuickJS-ng library
list(APPEND WISP_COMMON_LIBS qjs)

target_link_libraries(wisp ${WISP_COMMON_LIBS})

target_include_directories(wisp PRIVATE ${CMAKE_SOURCE_DIR}/contrib ${CMAKE_BINARY_DIR})

# Include tests subdirectory (tests only enabled for Debug builds or when NEOSURF_ENABLE_TESTS=ON)
add_subdirectory(test)

install(TARGETS wisp
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY resources/ DESTINATION ${CMAKE_INSTALL_DATADIR}/wisp)

if(WISP_INSTALL_NSGENBIND)
	install(FILES ${CMAKE_BINARY_DIR}/contrib/nsgenbind/nsgenbind DESTINATION ${CMAKE_INSTALL_BINDIR} PERMISSIONS WORLD_EXECUTE)
endif()

if(WISP_INSTALL_GEN_PARSER)
	install(FILES ${CMAKE_BINARY_DIR}/contrib/libcss/gen_parser/gen_parser DESTINATION ${CMAKE_INSTALL_BINDIR} PERMISSIONS WORLD_EXECUTE)
endif()

if(WISP_BUILD_XXD)
	install(TARGETS xxd DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(WIN32)
    function(install_dlls_for_package PREFIX LIBS)
        if(NOT PREFIX OR NOT EXISTS "${PREFIX}/bin")
            return()
        endif()
        
        foreach(LIB ${LIBS})
            string(REGEX REPLACE "^lib" "" BASE_NAME "${LIB}")

            set(DLLS)
            file(GLOB DLL_EXACT
                "${PREFIX}/bin/lib${BASE_NAME}.dll"
                "${PREFIX}/bin/${BASE_NAME}.dll"
            )
            file(GLOB DLL_VER
                "${PREFIX}/bin/lib${BASE_NAME}-*.dll"
                "${PREFIX}/bin/${BASE_NAME}-*.dll"
            )
            list(APPEND DLLS ${DLL_EXACT} ${DLL_VER})
            
            if(DLLS)
                foreach(DLL ${DLLS})
                    message(STATUS "  Found DLL for ${LIB}: ${DLL}")
                endforeach()
                install(FILES ${DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
            endif()
        endforeach()
    endfunction()

	if(LIBCURL_FOUND)
		install_dlls_for_package("${LIBCURL_PREFIX}" "${LIBCURL_LIBRARIES}")
		
        file(GLOB RUNTIME_DLLS 
            "${LIBCURL_PREFIX}/bin/libgcc_s_*.dll"
            "${LIBCURL_PREFIX}/bin/libwinpthread-*.dll"
            "${LIBCURL_PREFIX}/bin/libbrotli*.dll" 
            "${LIBCURL_PREFIX}/bin/libidn2*.dll"
            "${LIBCURL_PREFIX}/bin/libunistring*.dll"
            "${LIBCURL_PREFIX}/bin/libiconv*.dll"
            "${LIBCURL_PREFIX}/bin/libintl*.dll"
            "${LIBCURL_PREFIX}/bin/zlib1.dll"
            "${LIBCURL_PREFIX}/bin/libssh2*.dll"
            "${LIBCURL_PREFIX}/bin/libnghttp2*.dll"
            "${LIBCURL_PREFIX}/bin/libnghttp3*.dll"
            "${LIBCURL_PREFIX}/bin/libngtcp2-*.dll"
            "${LIBCURL_PREFIX}/bin/libngtcp2_crypto_ossl-*.dll"
            "${LIBCURL_PREFIX}/bin/libzstd*.dll"
            "${LIBCURL_PREFIX}/bin/libregex*.dll"
            "${LIBCURL_PREFIX}/bin/libtre*.dll"
            "${LIBCURL_PREFIX}/bin/libsystre*.dll"
        )
        if(RUNTIME_DLLS)
            install(FILES ${RUNTIME_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif()
    endif()

	if(LIBPSL_FOUND)
		install_dlls_for_package("${LIBPSL_PREFIX}" "${LIBPSL_LIBRARIES}")
	endif()

	if(LIBJPEG_FOUND)
		install_dlls_for_package("${LIBJPEG_PREFIX}" "${LIBJPEG_LIBRARIES}")
	endif()

	if(LIBPNG_FOUND)
		install_dlls_for_package("${LIBPNG_PREFIX}" "${LIBPNG_LIBRARIES}")
	endif()

	if(LIBWEBP_FOUND)
		install_dlls_for_package("${LIBWEBP_PREFIX}" "${LIBWEBP_LIBRARIES}")
		file(GLOB WEBP_EXTRA_DLLS "${LIBWEBP_PREFIX}/bin/libsharpyuv*.dll")
		if(WEBP_EXTRA_DLLS)
			install(FILES ${WEBP_EXTRA_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
		endif()
	endif()

	if(LIBCRYPTO_FOUND)
		install_dlls_for_package("${LIBCRYPTO_PREFIX}" "${LIBCRYPTO_LIBRARIES}")
	endif()

	if(LIBSSL_FOUND)
		install_dlls_for_package("${LIBSSL_PREFIX}" "${LIBSSL_LIBRARIES}")
	endif()

	if(LIBUTF8PROC_FOUND)
		install_dlls_for_package("${LIBUTF8PROC_PREFIX}" "${LIBUTF8PROC_LIBRARIES}")
	endif()

	if(ZLIB_FOUND)
		install_dlls_for_package("${ZLIB_PREFIX}" "${ZLIB_LIBRARIES}")
	endif()

	if(LIBXML2_FOUND)
		install_dlls_for_package("${LIBXML2_PREFIX}" "${LIBXML2_LIBRARIES}")
	endif()

	if(WISP_USE_RSVG)
		install_dlls_for_package("${LIBRSVG_PREFIX}" "${LIBRSVG_LIBRARIES}")
		install_dlls_for_package("${CAIRO_PREFIX}" "${CAIRO_LIBRARIES}")
	endif()
endif()
