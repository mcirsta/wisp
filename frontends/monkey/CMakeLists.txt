

include_directories(${CMAKE_SOURCE_DIR}/include)

set(MONKEY_SRCS
    main.c
    output.c
    filetype.c
    schedule.c
    bitmap.c
    plot.c
    browser.c
    download.c
    401login.c
    layout.c
    dispatch.c
    fetch.c
)

# Generate Messages per language filtered for the monkey frontend
set(MESSAGES_LANGUAGES en fr de it nl zh_CN)
set(MESSAGES_PLATFORMS monkey all)
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
        COMMENT "Generating Monkey Messages for ${LANG}"
    )
endforeach()
add_custom_target(monkey_messages DEPENDS ${GENERATED_MESSAGES_FILES})

add_executable(nsmonkey ${MONKEY_SRCS})
add_dependencies(nsmonkey monkey_messages)

target_include_directories(nsmonkey PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(nsmonkey PRIVATE MONKEY_RESPATH="${GENERATED_MESSAGES_DIR}")
target_compile_definitions(nsmonkey PRIVATE MONKEY_SRCPATH="${CMAKE_SOURCE_DIR}/src/resources")

target_link_libraries(nsmonkey wisp m ${WISP_COMMON_LIBS})

if(WIN32 AND CMAKE_BUILD_TYPE MATCHES Debug)
    add_custom_command(TARGET nsmonkey POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:wisp>
                $<TARGET_FILE_DIR:nsmonkey>
        COMMENT "Copying libwisp next to nsmonkey")

    set(SVGTINY_RUNTIME)
    if(TARGET svgtiny)
        get_target_property(SVGTINY_TYPE svgtiny TYPE)
        if(SVGTINY_TYPE STREQUAL "SHARED_LIBRARY" OR SVGTINY_TYPE STREQUAL "MODULE_LIBRARY")
            set(SVGTINY_RUNTIME "$<TARGET_FILE:svgtiny>")
        endif()
    endif()
    if(SVGTINY_RUNTIME)
        add_custom_command(TARGET nsmonkey POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SVGTINY_RUNTIME}" "$<TARGET_FILE_DIR:nsmonkey>/"
        )
    endif()

    add_custom_command(TARGET nsmonkey POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libdom/libdom.dll" "$<TARGET_FILE_DIR:nsmonkey>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsutils/libnsutils.dll" "$<TARGET_FILE_DIR:nsmonkey>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libparserutils/libparserutils.dll" "$<TARGET_FILE_DIR:nsmonkey>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsbmp/libnsbmp.dll" "$<TARGET_FILE_DIR:nsmonkey>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libnsgif/libnsgif.dll" "$<TARGET_FILE_DIR:nsmonkey>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libcss/libcss.dll" "$<TARGET_FILE_DIR:nsmonkey>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/contrib/libhubbub/libhubbub.dll" "$<TARGET_FILE_DIR:nsmonkey>/"
        COMMENT "Copying contrib DLLs next to nsmonkey")
endif()

install(TARGETS nsmonkey DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${GENERATED_MESSAGES_DIR}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/wisp)
