
add_definitions(-DNEOSURF_WINDOWS_RESPATH=".")

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


find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(MESSAGES_LANGUAGES en fr de it nl)
set(MESSAGES_PLATFORMS win all)
set(FAT_MESSAGES "${CMAKE_SOURCE_DIR}/src/resources/FatMessages")
set(SPLIT_MESSAGES_SCRIPT "${CMAKE_SOURCE_DIR}/src/utils/split_messages.py")
set(GENERATED_MESSAGES_DIR "${CMAKE_CURRENT_BINARY_DIR}/res")

set(GENERATED_MESSAGES_FILES)

foreach(LANG ${MESSAGES_LANGUAGES})
    set(MSG_FILE "${GENERATED_MESSAGES_DIR}/${LANG}/Messages")
    list(APPEND GENERATED_MESSAGES_FILES ${MSG_FILE})
    
    add_custom_command(
        OUTPUT ${MSG_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_MESSAGES_DIR}/${LANG}"
        COMMAND Python3::Interpreter ${SPLIT_MESSAGES_SCRIPT} ${FAT_MESSAGES} ${MSG_FILE} ${LANG} ${MESSAGES_PLATFORMS}
        DEPENDS ${FAT_MESSAGES} ${SPLIT_MESSAGES_SCRIPT}
        COMMENT "Generating Messages for ${LANG}"
    )
endforeach()

add_custom_target(windows_messages DEPENDS ${GENERATED_MESSAGES_FILES})

# Resource compilation
set(NEOSURF_WINDOWS_RC res/resource.rc)

add_executable(neosurf-windows WIN32
	${NEOSURF_WINDOWS_SRCS}
	${NEOSURF_WINDOWS_RC}
)

add_dependencies(neosurf-windows windows_messages)
target_include_directories(neosurf-windows PRIVATE ${GENERATED_MESSAGES_DIR})

target_link_libraries(neosurf-windows neosurf m ${NEOSURF_COMMON_LIBS} ws2_32 comctl32 gdi32 user32 kernel32 msimg32 wininet shlwapi crypt32)

if(CMAKE_BUILD_TYPE MATCHES Debug)
	set(CMAKE_INSTALL_DO_STRIP OFF)
endif()

install(TARGETS neosurf-windows DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY res/ DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf-windows)
install(DIRECTORY ${GENERATED_MESSAGES_DIR}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf-windows)

# Install real resources from src/resources
install(FILES 
    ${CMAKE_SOURCE_DIR}/src/resources/en/welcome.html
    ${CMAKE_SOURCE_DIR}/src/resources/en/credits.html
    ${CMAKE_SOURCE_DIR}/src/resources/en/licence.html
    ${CMAKE_SOURCE_DIR}/src/resources/default.css
    ${CMAKE_SOURCE_DIR}/src/resources/adblock.css
    ${CMAKE_SOURCE_DIR}/src/resources/internal.css
    ${CMAKE_SOURCE_DIR}/src/resources/quirks.css
    DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf-windows
)
install(FILES ${CMAKE_SOURCE_DIR}/src/resources/neosurf.png DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf-windows RENAME neosurf.png)
install(FILES ${CMAKE_SOURCE_DIR}/src/resources/ca-bundle DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf-windows RENAME ca-bundle.crt)
install(FILES ${CMAKE_SOURCE_DIR}/src/resources/uninstall.ico DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf-windows)

install(FILES 
    ${CMAKE_SOURCE_DIR}/src/resources/icons/arrow-l.png
    ${CMAKE_SOURCE_DIR}/src/resources/icons/content.png
    ${CMAKE_SOURCE_DIR}/src/resources/icons/directory.png
    ${CMAKE_SOURCE_DIR}/src/resources/icons/directory2.png
    ${CMAKE_SOURCE_DIR}/src/resources/icons/hotlist-add.png
    ${CMAKE_SOURCE_DIR}/src/resources/icons/hotlist-rmv.png
    ${CMAKE_SOURCE_DIR}/src/resources/icons/search.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf-windows/icons
)
