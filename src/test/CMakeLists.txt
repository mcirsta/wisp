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
    foreach(RUNTIME_TARGET dom nsutils parserutils nsbmp nsgif css hubbub)
      if(TARGET ${RUNTIME_TARGET})
        get_target_property(RUNTIME_TARGET_TYPE ${RUNTIME_TARGET} TYPE)
        if(RUNTIME_TARGET_TYPE STREQUAL "SHARED_LIBRARY" OR
            RUNTIME_TARGET_TYPE STREQUAL "MODULE_LIBRARY")
          add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:${RUNTIME_TARGET}>"
              "$<TARGET_FILE_DIR:${name}>/"
          )
        endif()
      endif()
    endforeach()
  endif()
endfunction()

function(wisp_test_allow_libcss_stubs name)
  if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(${name} PRIVATE -Wl,--allow-multiple-definition)
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

# Exercise the real scheme fetchers with a small fetch-core callback harness.
add_wisp_test(fetcher_queue_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/css_fetcher.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/fetcher.c
  ${CMAKE_SOURCE_DIR}/src/utils/nsurl/nsurl.c
  ${CMAKE_SOURCE_DIR}/src/utils/nsurl/parse.c
  ${CMAKE_SOURCE_DIR}/src/utils/idna.c
  ${CMAKE_SOURCE_DIR}/src/utils/punycode.c
  ${CMAKE_SOURCE_DIR}/src/utils/corestrings.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/fetcher_queue_test.c
)

# Native event-wait adapters used by Monkey, Haiku and Windows.
add_executable(socket_events_test socket_events_test.c ../utils/socket.c)
target_include_directories(socket_events_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(WIN32)
  target_link_libraries(socket_events_test PRIVATE ws2_32)
endif()
add_test(NAME socket_events_test COMMAND socket_events_test)
set_tests_properties(socket_events_test PROPERTIES TIMEOUT 15)

# Exercise GTK's source lifetime without needing a display or full GTK build.
if(UNIX)
  pkg_check_modules(EVENT_GLIB QUIET IMPORTED_TARGET glib-2.0)
  if(EVENT_GLIB_FOUND)
    add_executable(glib_socket_events_test socket_events_test.c
      ${CMAKE_SOURCE_DIR}/frontends/gtk/socket.c
      ${CMAKE_SOURCE_DIR}/frontends/gtk/schedule.c log.c)
    target_compile_definitions(glib_socket_events_test PRIVATE WISP_TEST_GLIB)
    target_include_directories(glib_socket_events_test PRIVATE
      ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/wisp ${CMAKE_SOURCE_DIR}/frontends)
    target_link_libraries(glib_socket_events_test PRIVATE PkgConfig::EVENT_GLIB)
    add_test(NAME glib_socket_events_test COMMAND glib_socket_events_test)
    set_tests_properties(glib_socket_events_test PROPERTIES TIMEOUT 15)
  endif()
endif()

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

add_wisp_test(libdom_iteration_test
  ${CMAKE_SOURCE_DIR}/src/utils/libdom.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/libdom_iteration_test.c
)

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

add_wisp_test(layout_calc_test
  ${CMAKE_SOURCE_DIR}/src/test/layout_calc_test.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/redraw_helpers.c
)

add_wisp_test(layout_flex_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/layout_flex.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/layout_flex_test.c
  ${CMAKE_SOURCE_DIR}/src/test/flex_layout_style.c
)
set_source_files_properties(flex_layout_style.c PROPERTIES INCLUDE_DIRECTORIES
  "${CMAKE_SOURCE_DIR}/contrib/libcss/src;${CMAKE_BINARY_DIR}/contrib/libcss/src;${CMAKE_BINARY_DIR}/contrib/libcss/src/select;${CMAKE_BINARY_DIR}/contrib/libcss/src/generated")
# Define TESTING so layout_flex.c can use test stubs
target_compile_definitions(layout_flex_test PRIVATE TESTING)
wisp_test_allow_libcss_stubs(layout_flex_test)


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

add_wisp_test(svg_text_test
  ${CMAKE_SOURCE_DIR}/src/desktop/plot_style.c
  ${CMAKE_SOURCE_DIR}/src/desktop/knockout.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/image/svg.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/content_stubs.c
  ${CMAKE_SOURCE_DIR}/src/test/svg_text_test.c
)

add_wisp_test(stacking_test
  ${CMAKE_SOURCE_DIR}/src/test/stacking_test.c
  ${CMAKE_SOURCE_DIR}/src/test/stacking_test_style.c
)
set_source_files_properties(stacking_test_style.c PROPERTIES INCLUDE_DIRECTORIES
  "${CMAKE_SOURCE_DIR}/contrib/libcss/src;${CMAKE_BINARY_DIR}/contrib/libcss/src;${CMAKE_BINARY_DIR}/contrib/libcss/src/select;${CMAKE_BINARY_DIR}/contrib/libcss/src/generated")
target_link_libraries(stacking_test PRIVATE wisp)

add_wisp_test(box_construct_race_test
  ${CMAKE_SOURCE_DIR}/src/utils/talloc.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/box_construct_race_test.c
)

add_wisp_test(grid_layout_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/layout_grid.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/grid_layout_test.c
  ${CMAKE_SOURCE_DIR}/src/test/grid_layout_style.c
)
# Only the style factory includes libcss internals and generated setters.
set_source_files_properties(grid_layout_style.c PROPERTIES INCLUDE_DIRECTORIES
  "${CMAKE_SOURCE_DIR}/contrib/libcss/src;${CMAKE_BINARY_DIR}/contrib/libcss/src;${CMAKE_BINARY_DIR}/contrib/libcss/src/select;${CMAKE_BINARY_DIR}/contrib/libcss/src/generated")
# Define TESTING so layout_grid.c uses test stubs instead of calling layout_minmax_box
target_compile_definitions(grid_layout_test PRIVATE TESTING)
wisp_test_allow_libcss_stubs(grid_layout_test)

add_wisp_test(layout_inline_grid_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/layout_grid.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/layout_inline_grid_test.c
)
target_compile_definitions(layout_inline_grid_test PRIVATE TESTING)
wisp_test_allow_libcss_stubs(layout_inline_grid_test)

add_wisp_test(grid_construct_test
  ${CMAKE_SOURCE_DIR}/src/content/handlers/html/box_normalise.c
  ${CMAKE_SOURCE_DIR}/src/utils/libdom.c
  ${CMAKE_SOURCE_DIR}/src/test/log.c
  ${CMAKE_SOURCE_DIR}/src/test/grid_construct_test.c
)
wisp_test_allow_libcss_stubs(grid_construct_test)
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
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/quickjs_bindings.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/timers.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/navigator.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/location.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/document.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/dom.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/tree.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/tokenlist.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/platform.c
  ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/quickjs/collections.c
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
set_tests_properties(test_quickjs PROPERTIES TIMEOUT 60)

# ============================================================================
# Document-owned font download lifecycle tests
# ============================================================================

add_executable(font_face_test font_face_test.c)
target_include_directories(font_face_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(font_face_test wisp)
add_test(NAME font_face_test COMMAND font_face_test)

# Margin collapse test — two-unit build to avoid enum collisions
# between Wisp headers (box.h) and libcss internal headers (opcodes.h)

# Style factory: only sees libcss internal headers
add_library(margin_collapse_style OBJECT layout_margin_collapse_style.c)
add_dependencies(margin_collapse_style libcss_propreg_gen libcss_select_gen)
target_include_directories(margin_collapse_style PRIVATE
  ${CMAKE_SOURCE_DIR}/contrib/libcss/include
  ${CMAKE_SOURCE_DIR}/contrib/libcss/src
  ${CMAKE_BINARY_DIR}/contrib/libcss/src
  ${CMAKE_BINARY_DIR}/contrib/libcss/src/select
  ${CMAKE_BINARY_DIR}/contrib/libcss/src/generated
)

# Test executable: only sees Wisp headers
add_executable(layout_margin_collapse_test
  layout_margin_collapse_test.c
  $<TARGET_OBJECTS:margin_collapse_style>
)
target_include_directories(layout_margin_collapse_test PRIVATE
  ${TEST_COMMON_INCLUDES}
  ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(layout_margin_collapse_test wisp ${CHECK_LIBRARIES})
add_test(NAME layout_margin_collapse_test COMMAND layout_margin_collapse_test)
set_tests_properties(layout_margin_collapse_test PROPERTIES
  ENVIRONMENT "CK_VERBOSITY=verbose;CK_FORK=no"
)

add_executable(layout_inline_baseline_test
  layout_inline_baseline_test.c
  $<TARGET_OBJECTS:margin_collapse_style>
)
target_include_directories(layout_inline_baseline_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(layout_inline_baseline_test wisp ${CHECK_LIBRARIES})
add_test(NAME layout_inline_baseline_test COMMAND layout_inline_baseline_test)

add_executable(layout_fixed_test layout_fixed_test.c)
target_include_directories(layout_fixed_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(layout_fixed_test wisp ${CHECK_LIBRARIES})
add_test(NAME layout_fixed_test COMMAND layout_fixed_test)

add_executable(layout_sticky_test layout_sticky_test.c sticky_test_style.c)
set_source_files_properties(sticky_test_style.c PROPERTIES INCLUDE_DIRECTORIES
  "${CMAKE_SOURCE_DIR}/contrib/libcss/src;${CMAKE_BINARY_DIR}/contrib/libcss/src;${CMAKE_BINARY_DIR}/contrib/libcss/src/select;${CMAKE_BINARY_DIR}/contrib/libcss/src/generated")
target_include_directories(layout_sticky_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(layout_sticky_test wisp ${CHECK_LIBRARIES})
add_test(NAME layout_sticky_test COMMAND layout_sticky_test)

add_executable(scroll_damage_test scroll_damage_test.c sticky_test_style.c)
target_include_directories(scroll_damage_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(scroll_damage_test wisp ${CHECK_LIBRARIES})
add_test(NAME scroll_damage_test COMMAND scroll_damage_test)

add_executable(textarea_placeholder_test textarea_placeholder_test.c)
target_include_directories(textarea_placeholder_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(textarea_placeholder_test wisp ${CHECK_LIBRARIES})
add_test(NAME textarea_placeholder_test COMMAND textarea_placeholder_test)

add_executable(css_checked_test css_checked_test.c)
target_include_directories(css_checked_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(css_checked_test wisp ${CHECK_LIBRARIES})
add_test(NAME css_checked_test COMMAND css_checked_test)

add_executable(css_direction_test css_direction_test.c)
target_include_directories(css_direction_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(css_direction_test wisp ${CHECK_LIBRARIES})
add_test(NAME css_direction_test COMMAND css_direction_test)

add_executable(css_style_reuse_test css_style_reuse_test.c)
target_include_directories(css_style_reuse_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(css_style_reuse_test wisp ${CHECK_LIBRARIES})
add_test(NAME css_style_reuse_test COMMAND css_style_reuse_test)

add_executable(box_construct_context_test box_construct_context_test.c)
target_include_directories(box_construct_context_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(box_construct_context_test wisp ${CHECK_LIBRARIES})
add_test(NAME box_construct_context_test COMMAND box_construct_context_test)

add_executable(dom_class_test dom_class_test.c)
target_include_directories(dom_class_test PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(dom_class_test dom ${CHECK_LIBRARIES})
add_test(NAME dom_class_test COMMAND dom_class_test)

add_executable(dom_details_test dom_details_test.c)
target_include_directories(dom_details_test PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(dom_details_test dom ${CHECK_LIBRARIES})
add_test(NAME dom_details_test COMMAND dom_details_test)

add_executable(dom_tree_test dom_tree_test.c)
target_include_directories(dom_tree_test PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(dom_tree_test dom ${CHECK_LIBRARIES})
add_test(NAME dom_tree_test COMMAND dom_tree_test)

add_executable(dom_event_test dom_event_test.c)
target_include_directories(dom_event_test PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(dom_event_test dom ${CHECK_LIBRARIES})
add_test(NAME dom_event_test COMMAND dom_event_test)

add_executable(dom_userdata_test dom_userdata_test.c)
target_include_directories(dom_userdata_test PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(dom_userdata_test dom ${CHECK_LIBRARIES})
add_test(NAME dom_userdata_test COMMAND dom_userdata_test)

# ============================================================================
# Unix-only Tests (require malloc_fig)
# ============================================================================

if(NOT WIN32)
  add_library(malloc_fig SHARED ${CMAKE_SOURCE_DIR}/src/test/malloc_fig.c)
  target_include_directories(malloc_fig PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(malloc_fig PUBLIC dl)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_libraries(dom_userdata_test malloc_fig)
    target_compile_definitions(dom_userdata_test PRIVATE WISP_TEST_ALLOC_FAILURE)
  endif()

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

add_executable(form_state_test form_state_test.c)
target_include_directories(form_state_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(form_state_test wisp ${CHECK_LIBRARIES})
add_test(NAME form_state_test COMMAND form_state_test)

add_executable(form_bindings_test form_bindings_test.c)
target_include_directories(form_bindings_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(form_bindings_test wisp ${CHECK_LIBRARIES})
add_test(NAME form_bindings_test COMMAND form_bindings_test)
add_executable(object_bindings_test object_bindings_test.c)
target_include_directories(object_bindings_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(object_bindings_test wisp ${CHECK_LIBRARIES})
add_test(NAME object_bindings_test COMMAND object_bindings_test)
add_executable(object_fallback_test object_fallback_test.c)
target_include_directories(object_fallback_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(object_fallback_test wisp ${CHECK_LIBRARIES})
add_test(NAME object_fallback_test COMMAND object_fallback_test)
add_executable(image_cache_failure_test image_cache_failure_test.c)
target_include_directories(image_cache_failure_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(image_cache_failure_test wisp ${CHECK_LIBRARIES})
add_test(NAME image_cache_failure_test COMMAND image_cache_failure_test)
add_executable(gif_decode_failure_test gif_decode_failure_test.c)
target_include_directories(gif_decode_failure_test PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(gif_decode_failure_test nsgif ${CHECK_LIBRARIES})
add_test(NAME gif_decode_failure_test COMMAND gif_decode_failure_test)
add_executable(bmp_decode_failure_test bmp_decode_failure_test.c)
target_include_directories(bmp_decode_failure_test PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(bmp_decode_failure_test nsbmp ${CHECK_LIBRARIES})
add_test(NAME bmp_decode_failure_test COMMAND bmp_decode_failure_test)
add_executable(restyle_tracking_test restyle_tracking_test.c)
target_include_directories(restyle_tracking_test PRIVATE ${TEST_COMMON_INCLUDES} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(restyle_tracking_test wisp ${CHECK_LIBRARIES})
# ASan interposes malloc itself; exercise real allocator failures in Release.
if(TARGET malloc_fig AND CMAKE_BUILD_TYPE STREQUAL "Release")
  target_link_libraries(restyle_tracking_test malloc_fig)
  target_compile_definitions(restyle_tracking_test PRIVATE WISP_TEST_ALLOC_FAILURE)
  target_link_libraries(box_construct_context_test malloc_fig)
  target_compile_definitions(box_construct_context_test PRIVATE WISP_TEST_ALLOC_FAILURE)
  target_link_libraries(object_bindings_test malloc_fig)
  target_compile_definitions(object_bindings_test PRIVATE WISP_TEST_ALLOC_FAILURE)
  target_link_libraries(form_bindings_test malloc_fig)
  target_compile_definitions(form_bindings_test PRIVATE WISP_TEST_ALLOC_FAILURE)
endif()
add_test(NAME restyle_tracking_test COMMAND restyle_tracking_test)
