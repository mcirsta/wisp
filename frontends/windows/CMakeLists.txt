
add_definitions(-DWISP_WINDOWS_RESPATH=".")

include_directories(${CMAKE_SOURCE_DIR}/include)

set(WISP_WINDOWS_SRCS
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

# Native GDI linear gradient support (uses GradientFill API)
# Radial gradients use fallback triangle decomposition since GDI lacks native radial support
option(WISP_WINDOWS_NATIVE_LINEAR_GRADIENT "Use native GDI linear gradient rendering" ON)

# Resource compilation
set(WISP_WINDOWS_RC res/resource.rc)

add_executable(wisp-windows WIN32
	${WISP_WINDOWS_SRCS}
	${WISP_WINDOWS_RC}
)

# Ensure resource.rc is recompiled when messages change and waits for them to be generated
set_source_files_properties(${WISP_WINDOWS_RC} PROPERTIES OBJECT_DEPENDS "${GENERATED_MESSAGES_DIR}/en/Messages")
# Add quotes to the path so windres treats it as a string
target_compile_definitions(wisp-windows PRIVATE -DWISP_MESSAGES_PATH="\\"${GENERATED_MESSAGES_DIR}/en/Messages\\"")

add_dependencies(wisp-windows windows_messages)
target_include_directories(wisp-windows PRIVATE ${GENERATED_MESSAGES_DIR} ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(wisp-windows wisp m ${WISP_COMMON_LIBS} ws2_32 comctl32 gdi32 user32 kernel32 msimg32 wininet shlwapi crypt32)

# Enable native GDI linear gradient support if option is ON
if(WISP_WINDOWS_NATIVE_LINEAR_GRADIENT)
    target_compile_definitions(wisp-windows PRIVATE WISP_WINDOWS_NATIVE_LINEAR_GRADIENT=1)
    message(STATUS "Windows native GDI linear gradient rendering enabled")
endif()

# WOFF font format decompression (converts WOFF to raw TrueType/OpenType for AddFontMemResourceEx)
option(WISP_WOFF_DECODE "Enable WOFF font format decompression for web fonts" ON)
if(WISP_WOFF_DECODE)
    target_compile_definitions(wisp-windows PRIVATE WISP_WOFF_DECODE=1)
    target_link_libraries(wisp-windows ${ZLIB_LIBRARIES})
    message(STATUS "WOFF font decompression enabled")
endif()

# Copy required runtime DLLs next to the executable for Debug builds on Windows
if(WIN32 AND CMAKE_BUILD_TYPE MATCHES Debug)
    set(SVGTINY_RUNTIME)
    if(TARGET svgtiny)
        get_target_property(SVGTINY_TYPE svgtiny TYPE)
        if(SVGTINY_TYPE STREQUAL "SHARED_LIBRARY" OR SVGTINY_TYPE STREQUAL "MODULE_LIBRARY")
            set(SVGTINY_RUNTIME "$<TARGET_FILE:svgtiny>")
        endif()
    endif()
    if(SVGTINY_RUNTIME)
        add_custom_command(TARGET wisp-windows POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SVGTINY_RUNTIME}" "$<TARGET_FILE_DIR:wisp-windows>/"
        )
    endif()
    add_custom_command(TARGET wisp-windows POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libdom/libdom.dll" "$<TARGET_FILE_DIR:wisp-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsutils/libnsutils.dll" "$<TARGET_FILE_DIR:wisp-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libparserutils/libparserutils.dll" "$<TARGET_FILE_DIR:wisp-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsbmp/libnsbmp.dll" "$<TARGET_FILE_DIR:wisp-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsgif/libnsgif.dll" "$<TARGET_FILE_DIR:wisp-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libcss/libcss.dll" "$<TARGET_FILE_DIR:wisp-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libhubbub/libhubbub.dll" "$<TARGET_FILE_DIR:wisp-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/src/libwisp.dll" "$<TARGET_FILE_DIR:wisp-windows>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/src/resources/ca-bundle" "$<TARGET_FILE_DIR:wisp-windows>/ca-bundle.crt"
    )
endif()

if(WISP_ENABLE_GPROF)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(wisp-windows PRIVATE -pg -g -fno-omit-frame-pointer)
        target_link_options(wisp-windows PRIVATE -pg)
    endif()
endif()

if(WISP_ENABLE_FINSTRUMENT)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_sources(wisp-windows PRIVATE profiler.c)
        target_compile_options(wisp-windows PRIVATE -finstrument-functions -g -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR "WISP_ENABLE_FINSTRUMENT requires GCC or Clang")
    endif()
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
	set(CMAKE_INSTALL_DO_STRIP OFF)
endif()

install(TARGETS wisp-windows DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY res/ DESTINATION ${CMAKE_INSTALL_DATADIR}/wisp)
install(DIRECTORY ${GENERATED_MESSAGES_DIR}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/wisp)
install(FILES ${CMAKE_SOURCE_DIR}/src/resources/ca-bundle DESTINATION ${CMAKE_INSTALL_DATADIR}/wisp RENAME ca-bundle.crt)
install(FILES ${CMAKE_SOURCE_DIR}/src/resources/ca-bundle DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME ca-bundle.crt)
