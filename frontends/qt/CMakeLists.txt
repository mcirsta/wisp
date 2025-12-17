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
    res/netsurf.qrc
    ${GENERATED_MESSAGES_DIR}/messages.qrc
)

add_executable(nsqt ${QT_SOURCES} ${QT_RESOURCES})

add_dependencies(nsqt qt_messages)

target_compile_definitions(nsqt PRIVATE
    nsqt
    QT_RESPATH="${CMAKE_INSTALL_PREFIX}/share/neosurf/res"
)

target_link_libraries(nsqt PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui neosurf)
target_include_directories(nsqt PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/frontends/qt
    ${CMAKE_SOURCE_DIR}/src
)

install(TARGETS nsqt DESTINATION bin)