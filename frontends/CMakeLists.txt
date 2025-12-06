include_directories("${CMAKE_SOURCE_DIR}/frontends")

if(NEOSURF_BUILD_GTK_FRONTEND)
	add_subdirectory(gtk)
endif()

if(NEOSURF_BUILD_VI_FRONTEND)
	add_subdirectory(visurf)
endif()

if(NEOSURF_BUILD_WINDOWS_FRONTEND)
	add_subdirectory(windows)
endif()

if(NEOSURF_BUILD_QT_FRONTEND)
	add_subdirectory(qt)
endif()
