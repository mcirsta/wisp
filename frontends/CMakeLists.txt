include_directories("${CMAKE_SOURCE_DIR}/frontends")

if(WISP_BUILD_GTK_FRONTEND)
    add_subdirectory(gtk)
endif()

if(WISP_BUILD_VI_FRONTEND)
    add_subdirectory(visurf)
endif()

if(WISP_BUILD_WINDOWS_FRONTEND)
    add_subdirectory(windows)
endif()

if(WISP_BUILD_QT_FRONTEND)
    add_subdirectory(qt)
endif()

if(WISP_BUILD_MONKEY_FRONTEND)
    add_subdirectory(monkey)
endif()
