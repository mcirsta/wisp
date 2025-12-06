cmake_minimum_required(VERSION 3.16)

find_package(Qt6 COMPONENTS Widgets Core Gui REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

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
)

set(QT_RESOURCES
    res/netsurf.qrc
)

add_executable(nsqt ${QT_SOURCES} ${QT_RESOURCES})

target_compile_definitions(nsqt PRIVATE
    nsqt
    QT_RESPATH="${CMAKE_INSTALL_PREFIX}/share/neosurf/res"
)

target_link_libraries(nsqt PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui neosurf)
target_include_directories(nsqt PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/frontends/qt
)

install(TARGETS nsqt DESTINATION bin)
