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
    add_executable(qt_font_weight_test test_font_weight.cpp layout.cpp)
    target_compile_definitions(qt_font_weight_test PRIVATE WISP_QT_CAPTURE)
    target_include_directories(qt_font_weight_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/frontends)
    target_link_libraries(qt_font_weight_test PRIVATE Qt6::Widgets wisp)
    add_test(NAME qt_font_weight COMMAND qt_font_weight_test ${CMAKE_SOURCE_DIR}/test/fonts)
    set_tests_properties(qt_font_weight PROPERTIES TIMEOUT 30
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
        add_test(NAME qt_render_capture
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_capture.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_render_capture PROPERTIES TIMEOUT 60)
        add_test(NAME qt_aspect_ratio
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_aspect_ratio.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_aspect_ratio PROPERTIES TIMEOUT 30)
        add_test(NAME qt_logical_spacing
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_logical_spacing.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_logical_spacing PROPERTIES TIMEOUT 30)
        add_test(NAME qt_flex_alignment
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_flex_alignment.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_flex_alignment PROPERTIES TIMEOUT 30)
        add_test(NAME qt_inline_wrapper_margins
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_inline_wrapper_margins.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_inline_wrapper_margins PROPERTIES TIMEOUT 30)
        add_test(NAME qt_last_child_margin
            COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_last_child_margin.py $<TARGET_FILE:wisp-qt>)
        set_tests_properties(qt_last_child_margin PROPERTIES TIMEOUT 30)
        find_program(WISP_TEST_MAGICK magick)
        if(WISP_TEST_MAGICK)
            add_test(NAME qt_inline_baseline
                COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_inline_baseline.py
                    $<TARGET_FILE:wisp-qt> ${WISP_TEST_MAGICK})
            set_tests_properties(qt_inline_baseline PROPERTIES TIMEOUT 30)
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
target_include_directories(wisp-qt PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/frontends/qt
    ${CMAKE_SOURCE_DIR}/src
)

install(TARGETS wisp-qt DESTINATION bin)
