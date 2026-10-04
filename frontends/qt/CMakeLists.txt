cmake_minimum_required(VERSION 3.16)

find_package(Qt6 COMPONENTS Widgets Core Gui REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)


set(MESSAGES_LANGUAGES en fr de it nl zh_CN)
set(MESSAGES_PLATFORMS qt all)
set(FAT_MESSAGES "${CMAKE_SOURCE_DIR}/src/resources/FatMessages")
set(SPLIT_MESSAGES_SCRIPT "${CMAKE_SOURCE_DIR}/src/utils/split_messages.py")
set(GENERATED_MESSAGES_DIR "${CMAKE_CURRENT_BINARY_DIR}/res")

set(GENERATED_MESSAGES_FILES)
set(QRC_CONTENT "<RCC>\n")

foreach(LANG ${MESSAGES_LANGUAGES})
    set(MSG_FILE "${GENERATED_MESSAGES_DIR}/${LANG}/Messages")
    list(APPEND GENERATED_MESSAGES_FILES ${MSG_FILE})
    
    if(LANG STREQUAL "en")
        set(QRC_CONTENT "${QRC_CONTENT}    <qresource prefix=\"/\">\n")
    else()
        set(QRC_CONTENT "${QRC_CONTENT}    <qresource prefix=\"/\" lang=\"${LANG}\">\n")
    endif()

    set(QRC_CONTENT "${QRC_CONTENT}        <file alias=\"Messages\">${LANG}/Messages</file>\n")
    set(QRC_CONTENT "${QRC_CONTENT}    </qresource>\n")

    add_custom_command(
        OUTPUT ${MSG_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_MESSAGES_DIR}/${LANG}"
        COMMAND Python3::Interpreter ${SPLIT_MESSAGES_SCRIPT} ${FAT_MESSAGES} ${MSG_FILE} ${LANG} ${MESSAGES_PLATFORMS}
        DEPENDS ${FAT_MESSAGES} ${SPLIT_MESSAGES_SCRIPT}
        COMMENT "Generating Messages for ${LANG}"
    )
endforeach()

set(QRC_CONTENT "${QRC_CONTENT}</RCC>")
file(WRITE "${GENERATED_MESSAGES_DIR}/messages.qrc" "${QRC_CONTENT}")

add_custom_target(qt_messages DEPENDS ${GENERATED_MESSAGES_FILES})


set(QT_SOURCES
    actions.cpp
    application.cpp
    bitmap.cpp
    bookmarks.cpp
    cookies.cpp
    corewindow.cpp
    fetch.cpp
    global_history.cpp
    keymap.cpp
    layout.cpp
    listselection.cpp
    local_history.cpp
    page_info.cpp
    main.cpp
    misc.cpp
    plotters.cpp
    resources.cpp
    scaffolding.cpp
    settings.cpp
    socket.cpp
    statussplitter.cpp
    urlbar.cpp
    window.cpp
    widget.cpp

    # Qt Class Headers needing MOC processing
    actions.cls.h
    application.cls.h
    bookmarks.cls.h
    cookies.cls.h
    corewindow.cls.h
    global_history.cls.h
    listselection.cls.h
    local_history.cls.h
    page_info.cls.h
    scaffoldstyle.cls.h
    scaffolding.cls.h
    settings.cls.h
    statussplitter.cls.h
    urlbar.cls.h
    widget.cls.h
    window.cls.h
)

set(QT_RESOURCES
    res/wisp.qrc
    ${GENERATED_MESSAGES_DIR}/messages.qrc
)

add_executable(wisp-qt ${QT_SOURCES} ${QT_RESOURCES})

if(BUILD_TESTING AND WISP_ENABLE_TESTS)
    if(UNIX AND WISP_QT_CAPTURE)
        add_executable(qt_fetch_events_test test_fetch_events.cpp socket.cpp)
        target_include_directories(qt_fetch_events_test PRIVATE
            ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/frontends)
        target_link_libraries(qt_fetch_events_test PRIVATE Qt6::Core wisp)
        add_test(NAME qt_fetch_events COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/test_fetch_events.py
            $<TARGET_FILE:qt_fetch_events_test> $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_fetch_events PROPERTIES TIMEOUT 30)
    endif()

    add_executable(qt_bitmap_cache_test test_bitmap_cache.cpp bitmap.cpp)
    target_compile_definitions(qt_bitmap_cache_test PRIVATE WISP_QT_CAPTURE)
    target_include_directories(qt_bitmap_cache_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/frontends)
    target_link_libraries(qt_bitmap_cache_test PRIVATE Qt6::Widgets wisp)
    add_test(NAME qt_bitmap_cache COMMAND qt_bitmap_cache_test)
    set_tests_properties(qt_bitmap_cache PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1;QT_SCREEN_SCALE_FACTORS=1;QT_FONT_DPI=96")

    add_executable(qt_text_cache_test test_text_cache.cpp layout.cpp)
    target_compile_definitions(qt_text_cache_test PRIVATE WISP_QT_CAPTURE)
    target_include_directories(qt_text_cache_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/frontends)
    target_link_libraries(qt_text_cache_test PRIVATE Qt6::Widgets wisp)
    add_test(NAME qt_text_cache COMMAND qt_text_cache_test ${CMAKE_SOURCE_DIR}/test/fonts/normal-400.ttf)
    set_tests_properties(qt_text_cache PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1;QT_SCREEN_SCALE_FACTORS=1;QT_FONT_DPI=96")

    add_executable(qt_font_weight_test test_font_weight.cpp layout.cpp)
    target_compile_definitions(qt_font_weight_test PRIVATE WISP_QT_CAPTURE)
    target_include_directories(qt_font_weight_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/frontends)
    target_link_libraries(qt_font_weight_test PRIVATE Qt6::Widgets wisp)
    add_test(NAME qt_font_weight COMMAND qt_font_weight_test ${CMAKE_SOURCE_DIR}/test/fonts)
    set_tests_properties(qt_font_weight PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1;QT_SCREEN_SCALE_FACTORS=1;QT_FONT_DPI=96")

    add_executable(qt_font_fallback_test test_font_fallback.cpp layout.cpp)
    target_compile_definitions(qt_font_fallback_test PRIVATE WISP_QT_CAPTURE)
    target_include_directories(qt_font_fallback_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/frontends)
    target_link_libraries(qt_font_fallback_test PRIVATE Qt6::Widgets wisp)
    add_test(NAME qt_font_fallback COMMAND qt_font_fallback_test ${CMAKE_SOURCE_DIR}/test/fonts)
    set_tests_properties(qt_font_fallback PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1;QT_SCREEN_SCALE_FACTORS=1;QT_FONT_DPI=96")

    add_executable(qt_fractional_fonts_test test_fractional_fonts.cpp layout.cpp)
    target_include_directories(qt_fractional_fonts_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/frontends)
    target_link_libraries(qt_fractional_fonts_test PRIVATE Qt6::Widgets wisp)
    add_test(NAME qt_fractional_fonts COMMAND qt_fractional_fonts_test ${CMAKE_SOURCE_DIR}/test/fonts/normal-400.ttf)
    set_tests_properties(qt_fractional_fonts PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1;QT_SCREEN_SCALE_FACTORS=1;QT_FONT_DPI=96")

    add_executable(qt_text_split_test test_text_split.cpp test_text_reflow.c layout.cpp
        $<TARGET_OBJECTS:margin_collapse_style>)
    target_include_directories(qt_text_split_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/frontends)
    target_link_libraries(qt_text_split_test PRIVATE Qt6::Widgets wisp)
    add_test(NAME qt_text_split COMMAND qt_text_split_test ${CMAKE_SOURCE_DIR}/test/fonts/normal-400.ttf)
    set_tests_properties(qt_text_split PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=1;QT_SCREEN_SCALE_FACTORS=1;QT_FONT_DPI=96")
endif()

option(WISP_QT_CAPTURE "Build the optional Qt rendering capture tool" OFF)
if(WISP_QT_CAPTURE)
    target_sources(wisp-qt PRIVATE capture.cpp capture_inspect.cpp)
    target_compile_definitions(wisp-qt PRIVATE WISP_QT_CAPTURE)
    if(BUILD_TESTING)
        find_program(WISP_TEST_XVFB_RUN NAMES xvfb-run)
        if(WISP_TEST_XVFB_RUN)
            add_test(NAME qt_scroll_reuse COMMAND ${WISP_TEST_XVFB_RUN} -a -s "-screen 0 1920x1600x24"
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_scroll_reuse.py $<TARGET_FILE:wisp-qt>)
            set_tests_properties(qt_scroll_reuse PROPERTIES TIMEOUT 120)
            add_test(NAME qt_paint_damage COMMAND ${WISP_TEST_XVFB_RUN} -a -s "-screen 0 1920x1600x24"
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_paint_damage.py $<TARGET_FILE:wisp-qt>)
            set_tests_properties(qt_paint_damage PROPERTIES TIMEOUT 90)
            add_test(NAME qt_sticky_scroll_reuse COMMAND ${WISP_TEST_XVFB_RUN} -a -s "-screen 0 1920x1600x24"
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_sticky_scroll_reuse.py $<TARGET_FILE:wisp-qt>)
            set_tests_properties(qt_sticky_scroll_reuse PROPERTIES TIMEOUT 120)
            add_test(NAME qt_frame_scroll_reuse COMMAND ${WISP_TEST_XVFB_RUN} -a -s "-screen 0 1920x1600x24"
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_frame_scroll_reuse.py $<TARGET_FILE:wisp-qt>)
            set_tests_properties(qt_frame_scroll_reuse PROPERTIES TIMEOUT 120)
            set_tests_properties(qt_scroll_reuse qt_paint_damage qt_sticky_scroll_reuse qt_frame_scroll_reuse
                PROPERTIES RESOURCE_LOCK qt_native_screen)
        endif()
        add_test(NAME qt_render_capture
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_capture.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_render_capture PROPERTIES TIMEOUT 60)
        add_test(NAME qt_checked_interaction
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_checked_interaction.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_checked_interaction PROPERTIES TIMEOUT 40)
        add_test(NAME qt_retained_restyle
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_retained_restyle.py
                $<TARGET_FILE:wisp-qt> $<BOOL:${WISP_PROFILE_RESTYLE}>)
        set_tests_properties(qt_retained_restyle PROPERTIES TIMEOUT 120)
        add_test(NAME qt_retained_scroll
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_retained_scroll.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_retained_scroll PROPERTIES TIMEOUT 90)
        if(WISP_PROFILE_RESTYLE)
            add_test(NAME qt_restyle_reference
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_restyle_reference.py
                    $<TARGET_FILE:wisp-qt>)
            set_tests_properties(qt_restyle_reference PROPERTIES TIMEOUT 70)
            add_test(NAME qt_checked_interaction_restart
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_checked_interaction.py
                    $<TARGET_FILE:wisp-qt> --capture-css-only-restyle --capture-restart-partial-restyle)
            set_tests_properties(qt_checked_interaction_restart PROPERTIES TIMEOUT 40)
            add_test(NAME qt_restyle_lifecycle
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_restyle_lifecycle.py
                    $<TARGET_FILE:wisp-qt>)
            set_tests_properties(qt_restyle_lifecycle PROPERTIES TIMEOUT 70)
            add_test(NAME qt_retained_lifecycle
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_restyle_lifecycle.py
                    $<TARGET_FILE:wisp-qt> --retain)
            set_tests_properties(qt_retained_lifecycle PROPERTIES TIMEOUT 70)
        endif()
        add_test(NAME qt_absolute_sizing
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_absolute_sizing.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_absolute_sizing PROPERTIES TIMEOUT 30)
        add_test(NAME qt_positioned_stacking
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_positioned_stacking.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_positioned_stacking PROPERTIES TIMEOUT 30)
        add_test(NAME qt_absolute_images
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_absolute_images.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_absolute_images PROPERTIES TIMEOUT 30)
        add_test(NAME qt_aspect_ratio
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_aspect_ratio.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_aspect_ratio PROPERTIES TIMEOUT 30)
        add_test(NAME qt_logical_spacing
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_logical_spacing.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_logical_spacing PROPERTIES TIMEOUT 30)
        add_test(NAME qt_flex_alignment
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_flex_alignment.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_flex_alignment PROPERTIES TIMEOUT 30)
        add_test(NAME qt_flex_out_of_flow
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_flex_out_of_flow.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_flex_out_of_flow PROPERTIES TIMEOUT 30)
        add_test(NAME qt_flex_column_sizing
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_flex_column_sizing.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_flex_column_sizing PROPERTIES TIMEOUT 30)
        add_test(NAME qt_flex_image_sizing
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_flex_image_sizing.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_flex_image_sizing PROPERTIES TIMEOUT 30)
        add_test(NAME qt_flex_minimum_sizing
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_flex_minimum_sizing.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_flex_minimum_sizing PROPERTIES TIMEOUT 45)
        add_test(NAME qt_checked_selector
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_checked_selector.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_checked_selector PROPERTIES TIMEOUT 30)
        add_test(NAME qt_hidden_attribute
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_hidden_attribute.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_hidden_attribute PROPERTIES TIMEOUT 30)
        add_test(NAME qt_hidden_until_found
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_hidden_until_found.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_hidden_until_found PROPERTIES TIMEOUT 120)
        add_test(NAME qt_generated_search
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_generated_search.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_generated_search PROPERTIES TIMEOUT 120)
        add_test(NAME qt_disclosure_markers
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_disclosure_markers.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_disclosure_markers PROPERTIES TIMEOUT 90)
        add_test(NAME qt_marker_styling
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_marker_styling.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_marker_styling PROPERTIES TIMEOUT 90)
        add_test(NAME qt_outside_marker_baseline
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_outside_marker_baseline.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_outside_marker_baseline PROPERTIES TIMEOUT 60)
        add_test(NAME qt_empty_marker_lines
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_empty_marker_lines.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_empty_marker_lines PROPERTIES TIMEOUT 60)
        add_test(NAME qt_block_marker_fallback
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_block_marker_fallback.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_block_marker_fallback PROPERTIES TIMEOUT 40)
        add_test(NAME qt_flex_baseline
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_flex_baseline.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_flex_baseline PROPERTIES TIMEOUT 40)
        add_test(NAME qt_details_search
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_details_search.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_details_search PROPERTIES TIMEOUT 120)
        add_test(NAME qt_class_whitespace
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_class_whitespace.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_class_whitespace PROPERTIES TIMEOUT 30)
        add_test(NAME qt_inline_wrapper_margins
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_inline_wrapper_margins.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_inline_wrapper_margins PROPERTIES TIMEOUT 30)
        add_test(NAME qt_calc_inline_margins
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_calc_inline_margins.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_calc_inline_margins PROPERTIES TIMEOUT 30)
        add_test(NAME qt_inline_spacing
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_inline_spacing.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_inline_spacing PROPERTIES TIMEOUT 30)
        add_test(NAME qt_inline_block_alignment
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_inline_block_alignment.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_inline_block_alignment PROPERTIES TIMEOUT 30)
        add_test(NAME qt_last_child_margin
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_last_child_margin.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_last_child_margin PROPERTIES TIMEOUT 30)
        add_test(NAME qt_empty_block_margins
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_empty_block_margins.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_empty_block_margins PROPERTIES TIMEOUT 30)
        add_test(NAME qt_float_clearance
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_float_clearance.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_float_clearance PROPERTIES TIMEOUT 30)
        add_test(NAME qt_float_only_margins
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_float_only_margins.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_float_only_margins PROPERTIES TIMEOUT 40)
        add_test(NAME qt_float_boundaries
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_float_boundaries.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_float_boundaries PROPERTIES TIMEOUT 30)
        add_test(NAME qt_empty_inline_lines
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_empty_inline_lines.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_empty_inline_lines PROPERTIES TIMEOUT 30)
        add_test(NAME qt_inline_wrapping
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_inline_wrapping.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_inline_wrapping PROPERTIES TIMEOUT 30)
        add_test(NAME qt_inline_intrinsics
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_inline_intrinsics.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_inline_intrinsics PROPERTIES TIMEOUT 30)
        add_test(NAME qt_inline_alignment
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_inline_alignment.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_inline_alignment PROPERTIES TIMEOUT 30)
        add_test(NAME qt_flow_root
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_flow_root.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_flow_root PROPERTIES TIMEOUT 30)
        add_test(NAME qt_empty_min_height
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_empty_min_height.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_empty_min_height PROPERTIES TIMEOUT 30)
        add_test(NAME qt_scrolling_bounds
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_scrolling_bounds.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_scrolling_bounds PROPERTIES TIMEOUT 60)
        find_program(WISP_TEST_MAGICK magick)
        if(WISP_TEST_MAGICK)
            add_test(NAME qt_zero_opacity
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_zero_opacity.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_zero_opacity PROPERTIES TIMEOUT 30)
            add_test(NAME qt_visible_overflow
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_visible_overflow.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_visible_overflow PROPERTIES TIMEOUT 30)
            add_test(NAME qt_positioned_pseudo
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_positioned_pseudo.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_positioned_pseudo PROPERTIES TIMEOUT 30)
            add_test(NAME qt_generated_boxes
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_generated_boxes.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_generated_boxes PROPERTIES TIMEOUT 30)
            add_test(NAME qt_fractional_font_capture
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_fractional_fonts.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_fractional_font_capture PROPERTIES TIMEOUT 30)
            add_test(NAME qt_zero_font_size
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_zero_font_size.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_zero_font_size PROPERTIES TIMEOUT 30)
            add_test(NAME qt_input_placeholders
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_input_placeholders.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_input_placeholders PROPERTIES TIMEOUT 30)
            if(WISP_USE_WEBP)
                add_test(NAME qt_webp_first_frame
                    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_webp_first_frame.py
                        $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
                set_tests_properties(qt_webp_first_frame PROPERTIES TIMEOUT 40)
            endif()
            add_test(NAME qt_fixed_position
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_fixed_position.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_fixed_position PROPERTIES TIMEOUT 45)
            add_test(NAME qt_sticky_position
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_sticky_position.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_sticky_position PROPERTIES TIMEOUT 45)
            add_test(NAME qt_sticky_constraints
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_sticky_constraints.py
                    $<TARGET_FILE:wisp-qt>)
            set_tests_properties(qt_sticky_constraints PROPERTIES TIMEOUT 120)
            add_test(NAME qt_sticky_scroll
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_sticky_scroll.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_sticky_scroll PROPERTIES TIMEOUT 120)
            add_test(NAME qt_sticky_lifecycle
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_sticky_lifecycle.py
                    $<TARGET_FILE:wisp-qt> $<$<BOOL:${WISP_PROFILE_RESTYLE}>:--interruptions>)
            set_tests_properties(qt_sticky_lifecycle PROPERTIES TIMEOUT 120)
            add_test(NAME qt_sticky_resources
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_sticky_resources.py
                    $<TARGET_FILE:wisp-qt>)
            set_tests_properties(qt_sticky_resources PROPERTIES TIMEOUT 60)
            add_test(NAME qt_inline_baseline
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_inline_baseline.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_inline_baseline PROPERTIES TIMEOUT 30)
            add_test(NAME qt_inline_text_baseline
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_inline_text_baseline.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_inline_text_baseline PROPERTIES TIMEOUT 30)
            add_test(NAME qt_svg_text
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_svg_text.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_svg_text PROPERTIES TIMEOUT 30)
            add_test(NAME qt_gradient_stops
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_gradient_stops.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_gradient_stops PROPERTIES TIMEOUT 30)
            add_test(NAME qt_background_layers
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_background_layers.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_background_layers PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()

add_dependencies(wisp-qt qt_messages)

# Determine default cache state based on build type
set(DEFAULT_QT_DISK_CACHE ON)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEFAULT_QT_DISK_CACHE OFF)
endif()

option(WISP_QT_DISK_CACHE "Enable disk caching in the Qt frontend" ${DEFAULT_QT_DISK_CACHE})

if(WISP_QT_DISK_CACHE)
    target_compile_definitions(wisp-qt PRIVATE WISP_QT_DISK_CACHE_ENABLED)
endif()

target_compile_definitions(wisp-qt PRIVATE
    nsqt
    QT_RESPATH="${CMAKE_INSTALL_PREFIX}/share/wisp/res"
)

target_link_libraries(wisp-qt PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui wisp)

# Use the same alias configuration as Qt on Fontconfig platforms. Native Qt
# backends on other platforms keep using installed families and Qt substitutions.
if(QT_FEATURE_fontconfig)
    find_package(Fontconfig REQUIRED)
    foreach(font_target wisp-qt qt_font_weight_test qt_font_fallback_test qt_text_split_test qt_fractional_fonts_test
            qt_text_cache_test)
        if(TARGET ${font_target})
            target_link_libraries(${font_target} PRIVATE Fontconfig::Fontconfig)
        endif()
    endforeach()
endif()

target_include_directories(wisp-qt PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/frontends/qt
    ${CMAKE_SOURCE_DIR}/src
)

install(TARGETS wisp-qt DESTINATION bin)
