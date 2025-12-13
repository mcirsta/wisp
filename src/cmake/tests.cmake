if(NOT NEOSURF_ENABLE_TESTS)
  return()
endif()

pkg_check_modules(CHECK check)
if(NOT CHECK_FOUND)
  message(FATAL_ERROR "Missing 'check' unit test framework; install it or set NEOSURF_ENABLE_TESTS=OFF")
endif()

include(CTest)

  set(TEST_COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include/neosurf
    ${CMAKE_SOURCE_DIR}/frontends
    ${CMAKE_SOURCE_DIR}/src/content/handlers
    ${CHECK_INCLUDE_DIRS}
  )

  function(add_neosurf_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${TEST_COMMON_INCLUDES})
    target_compile_definitions(${name} PRIVATE nsgtk NEOSURF_BUILTIN_LOG_FILTER=\"level:WARNING\" NEOSURF_BUILTIN_VERBOSE_FILTER=\"level:DEBUG\" WITH_UTF8PROC)
    target_link_libraries(${name} ${NEOSURF_COMMON_LIBS} ${CHECK_LIBRARIES})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/src")
    set(TEST_ENV_PATH "C:/msys64/mingw64/bin\;C:/msys64/usr/bin\;$ENV{PATH}")
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "PATH=${TEST_ENV_PATH};CK_VERBOSITY=verbose;CK_FORK=yes")

    if(WIN32)
      add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libdom/libdom.dll" "$<TARGET_FILE_DIR:${name}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsutils/libnsutils.dll" "$<TARGET_FILE_DIR:${name}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libparserutils/libparserutils.dll" "$<TARGET_FILE_DIR:${name}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libsvgtiny/libsvgtiny.dll" "$<TARGET_FILE_DIR:${name}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsbmp/libnsbmp.dll" "$<TARGET_FILE_DIR:${name}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsgif/libnsgif.dll" "$<TARGET_FILE_DIR:${name}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libcss/libcss.dll" "$<TARGET_FILE_DIR:${name}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libhubbub/libhubbub.dll" "$<TARGET_FILE_DIR:${name}>/"
      )
    endif()
  endfunction()

  set(NSURL_SOURCES
    ${CMAKE_SOURCE_DIR}/src/utils/nsurl/nsurl.c
    ${CMAKE_SOURCE_DIR}/src/utils/nsurl/parse.c
    ${CMAKE_SOURCE_DIR}/src/utils/idna.c
    ${CMAKE_SOURCE_DIR}/src/utils/punycode.c
    ${CMAKE_SOURCE_DIR}/src/utils/corestrings.c
    ${CMAKE_SOURCE_DIR}/src/test/log.c
    ${CMAKE_SOURCE_DIR}/src/test/nsurl.c
  )
  add_neosurf_test(nsurl ${NSURL_SOURCES})

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
  add_neosurf_test(urldbtest ${URLDBTEST_SOURCES})

  add_neosurf_test(messages
    ${CMAKE_SOURCE_DIR}/src/utils/messages.c
    ${CMAKE_SOURCE_DIR}/src/utils/hashtable.c
    ${CMAKE_SOURCE_DIR}/src/test/log.c
    ${CMAKE_SOURCE_DIR}/src/test/messages.c
  )

  add_neosurf_test(nsoption
    ${CMAKE_SOURCE_DIR}/src/utils/nsoption.c
    ${CMAKE_SOURCE_DIR}/src/test/log.c
    ${CMAKE_SOURCE_DIR}/src/test/nsoption.c
  )

  add_neosurf_test(bloom
    ${CMAKE_SOURCE_DIR}/src/utils/bloom.c
    ${CMAKE_SOURCE_DIR}/src/test/bloom.c
  )

  add_neosurf_test(hashtable
    ${CMAKE_SOURCE_DIR}/src/utils/hashtable.c
    ${CMAKE_SOURCE_DIR}/src/test/log.c
    ${CMAKE_SOURCE_DIR}/src/test/hashtable.c
  )

  add_neosurf_test(urlescape
    ${CMAKE_SOURCE_DIR}/src/utils/url.c
    ${CMAKE_SOURCE_DIR}/src/test/log.c
    ${CMAKE_SOURCE_DIR}/src/test/urlescape.c
  )

  add_neosurf_test(utils
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

  add_neosurf_test(time
    ${CMAKE_SOURCE_DIR}/src/utils/time.c
    ${CMAKE_SOURCE_DIR}/src/test/log.c
    ${CMAKE_SOURCE_DIR}/src/test/time.c
  )

  add_neosurf_test(mimesniff
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

  # Renderer unit test: borders geometry via plotter polygons
  add_neosurf_test(html_redraw_borders_test
    ${CMAKE_SOURCE_DIR}/src/content/handlers/html/redraw_border.c
    ${CMAKE_SOURCE_DIR}/src/test/html_redraw_borders_test.c
  )

  add_neosurf_test(layout_flex_test
    ${CMAKE_SOURCE_DIR}/src/test/layout_flex_test.c
  )

  add_neosurf_test(svg_redraw_test
    ${CMAKE_SOURCE_DIR}/src/desktop/plot_style.c
    ${CMAKE_SOURCE_DIR}/src/test/svg_redraw_driver.c
    ${CMAKE_SOURCE_DIR}/src/test/svg_redraw_test.c
  )

  add_neosurf_test(svg_redraw_extlink_test
    ${CMAKE_SOURCE_DIR}/src/desktop/plot_style.c
    ${CMAKE_SOURCE_DIR}/src/content/handlers/image/svg.c
    ${CMAKE_SOURCE_DIR}/src/test/log.c
    ${CMAKE_SOURCE_DIR}/src/test/content_stubs.c
    ${CMAKE_SOURCE_DIR}/src/test/svg_redraw_extlink_test.c
  )

  add_neosurf_test(svg_redraw_comboflush_test
    ${CMAKE_SOURCE_DIR}/src/desktop/plot_style.c
    ${CMAKE_SOURCE_DIR}/src/content/handlers/image/svg.c
    ${CMAKE_SOURCE_DIR}/src/test/log.c
    ${CMAKE_SOURCE_DIR}/src/test/content_stubs.c
    ${CMAKE_SOURCE_DIR}/src/test/svg_redraw_comboflush_test.c
  )

  if(NOT WIN32)
    add_library(malloc_fig SHARED ${CMAKE_SOURCE_DIR}/src/test/malloc_fig.c)
    target_include_directories(malloc_fig PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(malloc_fig dl)

    add_neosurf_test(hashmap
      ${CMAKE_SOURCE_DIR}/src/utils/nsurl/nsurl.c
      ${CMAKE_SOURCE_DIR}/src/utils/nsurl/parse.c
      ${CMAKE_SOURCE_DIR}/src/utils/hashmap.c
      ${CMAKE_SOURCE_DIR}/src/utils/corestrings.c
      ${CMAKE_SOURCE_DIR}/src/test/log.c
      ${CMAKE_SOURCE_DIR}/src/test/hashmap.c
    )
    target_link_libraries(hashmap PRIVATE malloc_fig)

    add_neosurf_test(corestrings
      ${CMAKE_SOURCE_DIR}/src/utils/nsurl/nsurl.c
      ${CMAKE_SOURCE_DIR}/src/utils/nsurl/parse.c
      ${CMAKE_SOURCE_DIR}/src/utils/corestrings.c
      ${CMAKE_SOURCE_DIR}/src/test/log.c
      ${CMAKE_SOURCE_DIR}/src/test/corestrings.c
    )
    target_link_libraries(corestrings PRIVATE malloc_fig)
  endif()
