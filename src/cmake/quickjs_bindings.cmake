# QuickJS-ng JavaScript bindings CMake configuration
#
# This module:
# 1. Checks for required Python widlparser module
# 2. Generates bindings from WebIDL files to ${CMAKE_BINARY_DIR}/quickjs/
# 3. Sets up the quickjs library target

# Check for Python3 and widlparser module
find_package(Python3 REQUIRED COMPONENTS Interpreter)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import widlparser; print('ok')"
    OUTPUT_VARIABLE WIDLPARSER_CHECK
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT WIDLPARSER_CHECK STREQUAL "ok")
    message(FATAL_ERROR 
        "Python 'widlparser' module not found.\n"
        "Install it via:\n"
        "  - Arch Linux: pacman -S python-widlparser\n"
        "  - pip: pip install widlparser\n"
        "This is required for generating JavaScript bindings from WebIDL files."
    )
endif()

message(STATUS "Found Python widlparser module")

# Output directory for generated quickjs bindings
set(QUICKJS_GEN_DIR ${CMAKE_BINARY_DIR}/quickjs)

# WebIDL source files
file(GLOB WEBIDL_SOURCES ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/WebIDL/*.idl)

# Generated binding sources
set(QUICKJS_GEN_SOURCES
    ${QUICKJS_GEN_DIR}/console.c
)

# Custom command to generate console.c binding
add_custom_command(
    OUTPUT ${QUICKJS_GEN_DIR}/console.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${QUICKJS_GEN_DIR}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/utils/qjs_binding_generator.py
        ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/WebIDL/console.idl
        -o ${QUICKJS_GEN_DIR}
    DEPENDS 
        ${CMAKE_SOURCE_DIR}/utils/qjs_binding_generator.py
        ${CMAKE_SOURCE_DIR}/src/content/handlers/javascript/WebIDL/console.idl
    COMMENT "Generating QuickJS Console binding"
)

# Target to generate all quickjs bindings
add_custom_target(quickjs_bindings DEPENDS ${QUICKJS_GEN_SOURCES})

# Include directories for generated files
include_directories(${QUICKJS_GEN_DIR})
