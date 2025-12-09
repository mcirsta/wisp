cmake_minimum_required(VERSION 3.20)
project(nsgenbind)

add_definitions(-D_POSIX_C_SOURCE=200809L)
add_compile_options(-std=c99 -Wno-void-pointer-to-int-cast -Wno-int-conversion)

find_program(FLEX_EXECUTABLE flex HINTS "C:/msys64/usr/bin" "C:/msys64/mingw64/bin")
find_program(BISON_EXECUTABLE bison HINTS "C:/msys64/usr/bin" "C:/msys64/mingw64/bin")

if (NOT FLEX_EXECUTABLE)
    message(FATAL_ERROR "flex not found")
endif()
if (NOT BISON_EXECUTABLE)
    message(FATAL_ERROR "bison not found")
endif()

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/nsgenbind-lexer.c ${CMAKE_BINARY_DIR}/nsgenbind-lexer.h
    COMMAND ${FLEX_EXECUTABLE} -P=nsgenbind_ --outfile=${CMAKE_BINARY_DIR}/nsgenbind-lexer.c --header-file=${CMAKE_BINARY_DIR}/nsgenbind-lexer.h ${CMAKE_CURRENT_SOURCE_DIR}/src/nsgenbind-lexer.l
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/nsgenbind-lexer.l
)
if(NOT ${YACC} STREQUAL bison)
    message(FATAL_ERROR "nsgenbind requires bison >= 3.x; set -DYACC=bison")
endif()
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/nsgenbind-parser.c ${CMAKE_BINARY_DIR}/nsgenbind-parser.h
    COMMAND ${BISON_EXECUTABLE} -o ${CMAKE_BINARY_DIR}/nsgenbind-parser.c -d ${CMAKE_CURRENT_SOURCE_DIR}/src/nsgenbind-parser.y -p nsgenbind_
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/nsgenbind-parser.y
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/webidl-lexer.c ${CMAKE_BINARY_DIR}/webidl-lexer.h
    COMMAND ${FLEX_EXECUTABLE} -P=webidl_ --outfile=${CMAKE_BINARY_DIR}/webidl-lexer.c --header-file=${CMAKE_BINARY_DIR}/webidl-lexer.h ${CMAKE_CURRENT_SOURCE_DIR}/src/webidl-lexer.l
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/webidl-lexer.l
)
if(NOT ${YACC} STREQUAL bison)
    message(FATAL_ERROR "nsgenbind requires bison >= 3.x; set -DYACC=bison")
endif()
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/webidl-parser.c ${CMAKE_BINARY_DIR}/webidl-parser.h
    COMMAND ${BISON_EXECUTABLE} -o ${CMAKE_BINARY_DIR}/webidl-parser.c -d ${CMAKE_CURRENT_SOURCE_DIR}/src/webidl-parser.y -p webidl_
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/webidl-parser.y
)

add_executable(nsgenbind
	src/nsgenbind.c
	src/utils.c
	src/output.c
	src/webidl-ast.c
	src/nsgenbind-ast.c
	src/ir.c
	src/duk-libdom.c
	src/duk-libdom-interface.c
	src/duk-libdom-dictionary.c
	src/duk-libdom-common.c
	src/duk-libdom-generated.c

	# Generated dynamically
	${CMAKE_BINARY_DIR}/nsgenbind-parser.c
	${CMAKE_BINARY_DIR}/nsgenbind-lexer.c
	${CMAKE_BINARY_DIR}/webidl-parser.c
	${CMAKE_BINARY_DIR}/webidl-lexer.c
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src
)
