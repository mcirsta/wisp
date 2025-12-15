find_package(Iconv REQUIRED)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/src/charset/aliases.inc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/src/charset
    COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/contrib/libparserutils/building/make-aliases.py ${CMAKE_CURRENT_BINARY_DIR}/src/charset/aliases.inc
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/contrib/libparserutils
    DEPENDS ${CMAKE_SOURCE_DIR}/contrib/libparserutils/building/make-aliases.py
)
add_custom_target(parserutils_aliases DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/src/charset/aliases.inc)

add_library(parserutils SHARED
	src/input/filter.c
	src/input/inputstream.c
	src/charset/aliases.c
	src/charset/codec.c
	src/charset/encodings/utf8.c
	src/charset/encodings/utf16.c
	src/charset/codecs/codec_8859.c
	src/charset/codecs/codec_ascii.c
	src/charset/codecs/codec_ext8.c
	src/charset/codecs/codec_utf16.c
	src/charset/codecs/codec_utf8.c
	src/utils/buffer.c
	src/utils/stack.c
	src/utils/vector.c
	src/utils/errors.c
)
set_target_properties(parserutils PROPERTIES SOVERSION ${NEOSURF_ABI})

add_dependencies(parserutils parserutils_aliases)

if(Iconv_FOUND)
	if(TARGET Iconv::Iconv)
		target_link_libraries(parserutils Iconv::Iconv)
	else()
		target_link_libraries(parserutils ${Iconv_LIBRARIES})
		include_directories(${Iconv_INCLUDE_DIRS})
	endif()
endif()

install(TARGETS parserutils
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

include_directories(${CMAKE_SOURCE_DIR}/contrib/libparserutils/src)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src/charset)
