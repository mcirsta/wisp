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

install(TARGETS parserutils DESTINATION ${CMAKE_INSTALL_LIBDIR})

include_directories(${CMAKE_SOURCE_DIR}/contrib/libparserutils/src)

install(DIRECTORY include/parserutils DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
