# NeoSurf Tests

# Guard: Only enable tests if NEOSURF_ENABLE_TESTS is ON
# This is automatically ON for Debug builds or can be set via cmake option
if(NOT WISP_ENABLE_TESTS)
  return()
endif()

# Check for 'check' unit test framework
pkg_check_modules(CHECK check)
if(NOT CHECK_FOUND)
  message(FATAL_ERROR "Missing 'check' unit test framework; install it or set WISP_ENABLE_TESTS=OFF")
endif()

include(CTest)

# Common includes for all tests
set(TEST_COMMON_INCLUDES
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/include/wisp
  ${CMAKE_SOURCE_DIR}/frontends
  ${CMAKE_SOURCE_DIR}/src/content/handlers
  ${CHECK_INCLUDE_DIRS}
)

# Function to add a NeoSurf test executable
function(add_wisp_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${TEST_COMMON_INCLUDES})
  target_compile_definitions(${name} PRIVATE
    nsgtk
    WISP_BUILTIN_LOG_FILTER=\"level:WARNING\"
    WISP_BUILTIN_VERBOSE_FILTER=\"level:DEBUG\"
    WITH_UTF8PROC
    WISP_TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/src/test/data\"
  )
  target_link_libraries(${name} PUBLIC ${WISP_COMMON_LIBS} ${CHECK_LIBRARIES})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/src")
  set(TEST_ENV_PATH "C:/msys64/mingw64/bin\;C:/msys64/usr/bin\;$ENV{PATH}")
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "PATH=${TEST_ENV_PATH};CK_VERBOSITY=verbose;CK_FORK=yes")

  if(WIN32)
    set(SVGTINY_RUNTIME)
    if(TARGET svgtiny)
      get_target_property(SVGTINY_TYPE svgtiny TYPE)
      if(SVGTINY_TYPE STREQUAL "SHARED_LIBRARY" OR SVGTINY_TYPE STREQUAL "MODULE_LIBRARY")
        set(SVGTINY_RUNTIME "$<TARGET_FILE:svgtiny>")
      endif()
    endif()
    if(SVGTINY_RUNTIME)
      add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SVGTINY_RUNTIME}" "$<TARGET_FILE_DIR:${name}>/"
      )
    endif()
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libdom/libdom.dll" "$<TARGET_FILE_DIR:${name}>/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsutils/libnsutils.dll" "$<TARGET_FILE_DIR:${name}>/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libparserutils/libparserutils.dll" "$<TARGET_FILE_DIR:${name}>/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsbmp/libnsbmp.dll" "$<TARGET_FILE_DIR:${name}>/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsgif/libnsgif.dll" "$<TARGET_FILE_DIR:${name}>/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libcss/libcss.dll" "$<TARGET_FILE_DIR:${name}>/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libhubbub/libhubbub.dll" "$<TARGET_FILE_DIR:${name}>/"
    )
  endif()
endfunction()

# ============================================================================
# URL Tests
# ============================================================================

set(NSURL_SOURCES
  ${CMAKE_SOURCE_DIR}/src/utils/nsurl/nsurl.c
  ${CMAKE_SOURCE_DIR}/src/utils/nsurl/parse.c
  ${CMAKE_SOURCE_DIR}/src/utils/idna.c
  ${CMAKE_SOURCE_DIR}/src/utils/punycode.c
  ${CMAKE_SOURCE_DIR}/src/utils/corestrings.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/nsurl.c
)
add_wisp_test(nsurl ${NSURL_SOURCES})

set(URLDBTEST_SOURCES
  ${CMAKE_SOURCE_DIR}/src/utils/nsurl/nsurl.c
  ${CMAKE_SOURCE_DIR}/src/utils/nsurl/parse.c
  ${CMAKE_SOURCE_DIR}/src/utils/idna.c
  ${CMAKE_SOURCE_DIR}/src/utils/punycode.c
  ${CMAKE_SOURCE_DIR}/src/utils/bloom.c
  ${CMAKE_SOURCE_DIR}/src/utils/nsoption.c
  ${CMAKE_SOURCE_DIR}/src/utils/corestrings.c
  ${CMAKE_SOURCE_DIR}/src/utils/time.c
  ${CMAKE_SOURCE_DIR}/src/utils/hashtable.c
  ${CMAKE_SOURCE_DIR}/src/utils/messages.c
  ${CMAKE_SOURCE_DIR}/src/utils/utils.c
  ${CMAKE_SOURCE_DIR}/src/utils/http/primitives.c
  ${CMAKE_SOURCE_DIR}/src/utils/http/generics.c
  ${CMAKE_SOURCE_DIR}/src/utils/http/strict-transport-security.c
  ${CMAKE_SOURCE_DIR}/src/content/urldb.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/urldbtest.c
)
add_wisp_test(urldbtest ${URLDBTEST_SOURCES})

add_wisp_test(urlescape
  ${CMAKE_SOURCE_DIR}/src/utils/url.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/urlescape.c
)

# ============================================================================
# Utility Tests
# ============================================================================

add_wisp_test(messages
  ${CMAKE_SOURCE_DIR}/src/utils/messages.c
  ${CMAKE_SOURCE_DIR}/src/utils/hashtable.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/messages.c
)

add_wisp_test(nsoption
  ${CMAKE_SOURCE_DIR}/src/utils/nsoption.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/nsoption.c
)

add_wisp_test(bloom
  ${CMAKE_SOURCE_DIR}/src/utils/bloom.c
  ${CMAKE_SOURCE_DIR}/src/test/bloom.c
)

add_wisp_test(hashtable
  ${CMAKE_SOURCE_DIR}/src/utils/hashtable.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/hashtable.c
)

add_wisp_test(utils
  ${CMAKE_SOURCE_DIR}/src/utils/nsurl/nsurl.c
  ${CMAKE_SOURCE_DIR}/src/utils/nsurl/parse.c
  ${CMAKE_SOURCE_DIR}/src/utils/idna.c
  ${CMAKE_SOURCE_DIR}/src/utils/punycode.c
  ${CMAKE_SOURCE_DIR}/src/utils/utils.c
  ${CMAKE_SOURCE_DIR}/src/utils/messages.c
  ${CMAKE_SOURCE_DIR}/src/utils/hashtable.c
  ${CMAKE_SOURCE_DIR}/src/utils/corestrings.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/utils.c
)

add_wisp_test(time
  ${CMAKE_SOURCE_DIR}/src/utils/time.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/time.c
)

add_wisp_test(mimesniff
  ${CMAKE_SOURCE_DIR}/src/utils/nsurl/nsurl.c
  ${CMAKE_SOURCE_DIR}/src/utils/nsurl/parse.c
  ${CMAKE_SOURCE_DIR}/src/utils/idna.c
  ${CMAKE_SOURCE_DIR}/src/utils/punycode.c
  ${CMAKE_SOURCE_DIR}/src/utils/hashtable.c
  ${CMAKE_SOURCE_DIR}/src/utils/corestrings.c
  ${CMAKE_SOURCE_DIR}/src/utils/http/generics.c
  ${CMAKE_SOURCE_DIR}/src/utils/http/content-type.c
  ${CMAKE_SOURCE_DIR}/src/utils/http/primitives.c
  ${CMAKE_SOURCE_DIR}/src/utils/messages.c
  ${CMAKE_SOURCE_DIR}/src/utils/http/parameter.c
  ${CMAKE_SOURCE_DIR}/src/content/mimesniff.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/mimesniff.c
)

# ============================================================================
# Renderer Tests
# ============================================================================

add_wisp_test(html_redraw_borders_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/redraw_border.c
  ${CMAKE_SOURCE_DIR}/src/test/html_redraw_borders_test.c
)

add_wisp_test(layout_flex_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/layout_flex.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/layout_flex_test.c
)
# Define TESTING so layout_flex.c can use test stubs
target_compile_definitions(layout_flex_test PRIVATE TESTING)


add_wisp_test(svg_redraw_test
  ${CMAKE_SOURCE_DIR}/src/desktop/plot_style.c
  ${CMAKE_SOURCE_DIR}/src/test/svg_redraw_driver.c
  ${CMAKE_SOURCE_DIR}/src/test/svg_redraw_test.c
)

add_wisp_test(svg_redraw_extlink_test
  ${CMAKE_SOURCE_DIR}/src/desktop/plot_style.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/image/svg.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/content_stubs.c
  ${CMAKE_SOURCE_DIR}/src/test/svg_redraw_extlink_test.c
)

add_wisp_test(svg_redraw_comboflush_test
  ${CMAKE_SOURCE_DIR}/src/desktop/plot_style.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/image/svg.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/content_stubs.c
  ${CMAKE_SOURCE_DIR}/src/test/svg_redraw_comboflush_test.c
)

add_wisp_test(svg_dash_rects_test
  ${CMAKE_SOURCE_DIR}/src/desktop/plot_style.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/image/svg.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/content_stubs.c
  ${CMAKE_SOURCE_DIR}/src/test/svg_dash_rects_test.c
)

add_wisp_test(stacking_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/stacking.c
  ${CMAKE_SOURCE_DIR}/src/utils/utils.c
  ${CMAKE_SOURCE_DIR}/src/utils/messages.c
  ${CMAKE_SOURCE_DIR}/src/utils/hashtable.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/stacking_test.c
)

add_wisp_test(box_construct_race_test
  ${CMAKE_SOURCE_DIR}/src/utils/talloc.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/box_construct_race_test.c
)

add_wisp_test(grid_layout_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/layout_grid.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/grid_layout_test.c
)
# Define TESTING so layout_grid.c uses test stubs instead of calling layout_minmax_box
target_compile_definitions(grid_layout_test PRIVATE TESTING)

add_wisp_test(layout_inline_grid_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/layout_grid.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/layout_inline_grid_test.c
)
target_compile_definitions(layout_inline_grid_test PRIVATE TESTING)

add_wisp_test(grid_construct_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/box_normalise.c
  ${CMAKE_SOURCE_DIR}/src/utils/libdom.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/grid_construct_test.c
)
# Disable leak detection for grid_construct_test since libdom parsing has internal leaks
set_property(TEST grid_construct_test PROPERTY ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

add_wisp_test(transform_clip_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/redraw_helpers.c
  ${CMAKE_SOURCE_DIR}/src/test/transform_clip_test.c
)

# ============================================================================
# JavaScript Tests
# ============================================================================

add_wisp_test(test_quickjs
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/qjs.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/timers.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/navigator.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/location.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/document.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/storage.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/event_target.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/xhr.c
  ${CMAKE_BINARY_DIR}/quickjs/console.c
  ${CMAKE_BINARY_DIR}/quickjs/window.c
  ${CMAKE_SOURCE_DIR}/src/test/test_quickjs.c
)
add_dependencies(test_quickjs quickjs_bindings)
target_include_directories(test_quickjs PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/quickjs-ng
  ${CMAKE_SOURCE_DIR}/contrib/libnsutils/include
  ${CMAKE_BINARY_DIR}/quickjs
)
target_link_libraries(test_quickjs PRIVATE qjs nsutils wisp)

# ============================================================================
# Font-face Tests (simple test linked to neosurf lib)
# ============================================================================

add_executable(font_face_test font_face_test.c)
target_include_directories(font_face_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(font_face_test wisp)
add_test(NAME font_face_test COMMAND font_face_test)

# ============================================================================
# Unix-only Tests (require malloc_fig)
# ============================================================================

if(NOT WIN32)
  add_library(malloc_fig SHARED ${CMAKE_SOURCE_DIR}/src/test/malloc_fig.c)
  target_include_directories(malloc_fig PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(malloc_fig PUBLIC dl)

  add_wisp_test(hashmap
    ${CMAKE_SOURCE_DIR}/src/utils/nsurl/nsurl.c
    ${CMAKE_SOURCE_DIR}/src/utils/nsurl/parse.c
    ${CMAKE_SOURCE_DIR}/src/utils/idna.c
    ${CMAKE_SOURCE_DIR}/src/utils/punycode.c
    ${CMAKE_SOURCE_DIR}/src/utils/hashmap.c
    ${CMAKE_SOURCE_DIR}/src/utils/corestrings.c
    ${CMAKE_SOURCE_DIR}/src/test/log.c
    ${CMAKE_SOURCE_DIR}/src/test/hashmap.c
  )
  target_link_libraries(hashmap PRIVATE malloc_fig)

  add_wisp_test(corestrings
    ${CMAKE_SOURCE_DIR}/src/utils/nsurl/nsurl.c
    ${CMAKE_SOURCE_DIR}/src/utils/nsurl/parse.c
    ${CMAKE_SOURCE_DIR}/src/utils/idna.c
    ${CMAKE_SOURCE_DIR}/src/utils/punycode.c
    ${CMAKE_SOURCE_DIR}/src/utils/corestrings.c
    ${CMAKE_SOURCE_DIR}/src/test/log.c
    ${CMAKE_SOURCE_DIR}/src/test/corestrings.c
  )
  target_link_libraries(corestrings PRIVATE malloc_fig)
endif()
