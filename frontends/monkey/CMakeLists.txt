

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

if(WISP_ENABLE_TESTS)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import yaml"
        RESULT_VARIABLE MONKEY_YAML_RESULT OUTPUT_QUIET ERROR_QUIET)
    if(NOT MONKEY_YAML_RESULT EQUAL 0)
        message(FATAL_ERROR "Monkey tests require PyYAML for ${Python3_EXECUTABLE}")
    endif()
    set(MONKEY_TEST_DIR "${CMAKE_SOURCE_DIR}/src/test")
    foreach(TEST_NAME start-stop start-stop-no-js resource-scheme local-navigation local-layout)
        add_test(NAME monkey_${TEST_NAME}
            COMMAND Python3::Interpreter ${MONKEY_TEST_DIR}/monkey_driver.py
                -m $<TARGET_FILE:nsmonkey> -t ${MONKEY_TEST_DIR}/monkey-tests/${TEST_NAME}.yaml)
        set_tests_properties(monkey_${TEST_NAME} PROPERTIES TIMEOUT 40 LABELS monkey)
    endforeach()
    add_test(NAME monkey_driver COMMAND Python3::Interpreter
        ${MONKEY_TEST_DIR}/monkey_driver_test.py $<TARGET_FILE:nsmonkey>)
    set_tests_properties(monkey_driver PROPERTIES TIMEOUT 40 LABELS monkey)
endif()

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

    foreach(RUNTIME_TARGET dom nsutils parserutils nsbmp nsgif css hubbub)
        if(TARGET ${RUNTIME_TARGET})
            get_target_property(RUNTIME_TARGET_TYPE ${RUNTIME_TARGET} TYPE)
            if(RUNTIME_TARGET_TYPE STREQUAL "SHARED_LIBRARY" OR
                    RUNTIME_TARGET_TYPE STREQUAL "MODULE_LIBRARY")
                add_custom_command(TARGET nsmonkey POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "$<TARGET_FILE:${RUNTIME_TARGET}>"
                            "$<TARGET_FILE_DIR:nsmonkey>/"
                    COMMENT "Copying ${RUNTIME_TARGET} runtime next to nsmonkey")
            endif()
        endif()
    endforeach()
endif()

install(TARGETS nsmonkey DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${GENERATED_MESSAGES_DIR}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/wisp)
