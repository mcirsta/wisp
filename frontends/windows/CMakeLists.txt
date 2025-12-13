
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
	qpc_safe.c
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
target_include_directories(neosurf-windows PRIVATE ${GENERATED_MESSAGES_DIR} ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(neosurf-windows neosurf m ${NEOSURF_COMMON_LIBS} ws2_32 comctl32 gdi32 user32 kernel32 msimg32 wininet shlwapi crypt32)

# Copy required runtime DLLs next to the executable for Debug builds on Windows
if(WIN32 AND CMAKE_BUILD_TYPE MATCHES Debug)
    add_custom_command(TARGET neosurf-windows POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libdom/libdom.dll" "$<TARGET_FILE_DIR:neosurf-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsutils/libnsutils.dll" "$<TARGET_FILE_DIR:neosurf-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libparserutils/libparserutils.dll" "$<TARGET_FILE_DIR:neosurf-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libsvgtiny/libsvgtiny.dll" "$<TARGET_FILE_DIR:neosurf-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsbmp/libnsbmp.dll" "$<TARGET_FILE_DIR:neosurf-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsgif/libnsgif.dll" "$<TARGET_FILE_DIR:neosurf-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libcss/libcss.dll" "$<TARGET_FILE_DIR:neosurf-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libhubbub/libhubbub.dll" "$<TARGET_FILE_DIR:neosurf-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/src/libneosurf.dll" "$<TARGET_FILE_DIR:neosurf-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/src/resources/ca-bundle" "$<TARGET_FILE_DIR:neosurf-windows>/ca-bundle.crt"
    )
endif()

if(NEOSURF_ENABLE_GPROF)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(neosurf-windows PRIVATE -pg -g -fno-omit-frame-pointer)
        target_link_options(neosurf-windows PRIVATE -pg)
    endif()
endif()

if(NEOSURF_ENABLE_FINSTRUMENT)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_sources(neosurf-windows PRIVATE profiler.c)
        target_compile_options(neosurf-windows PRIVATE -finstrument-functions -g -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR "NEOSURF_ENABLE_FINSTRUMENT requires GCC or Clang")
    endif()
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
	set(CMAKE_INSTALL_DO_STRIP OFF)
endif()

install(TARGETS neosurf-windows DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY res/ DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf)
install(DIRECTORY ${GENERATED_MESSAGES_DIR}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf)
install(FILES ${CMAKE_SOURCE_DIR}/src/resources/ca-bundle DESTINATION ${CMAKE_INSTALL_DATADIR}/neosurf RENAME ca-bundle.crt)
