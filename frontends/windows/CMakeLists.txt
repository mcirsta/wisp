
add_definitions(-DNETSURF_WINDOWS_RESPATH=".")

include_directories(${CMAKE_SOURCE_DIR}/include)

set(NEOSURF_WINDOWS_SRCS
	about.c
	bitmap.c
	clipboard.c
	cookies.c
	corewindow.c
	download.c
	drawable.c
	fetch.c
	file.c
	findfile.c
	font.c
	global_history.c
	gui.c
	hotlist.c
	local_history.c
	main.c
	plot.c
	pointers.c
	prefs.c
	schedule.c
	windbg.c
	window.c
)

# Resource compilation
set(NEOSURF_WINDOWS_RC res/resource.rc)

add_executable(neosurf-windows WIN32
	${NEOSURF_WINDOWS_SRCS}
	${NEOSURF_WINDOWS_RC}
)

target_link_libraries(neosurf-windows neosurf m ${NEOSURF_COMMON_LIBS} ws2_32 comctl32 gdi32 user32 kernel32 msimg32 wininet shlwapi crypt32)

if(CMAKE_BUILD_TYPE MATCHES Debug)
	set(CMAKE_INSTALL_DO_STRIP OFF)
endif()

install(TARGETS neosurf-windows DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY res/ DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf-windows)
