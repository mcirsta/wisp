cmake_policy(SET CMP0079 NEW)

add_library(dom-hubbub-bindings SHARED
	parser.c
)

target_link_libraries(dom-hubbub-bindings nsutils hubbub dom)

install(TARGETS dom-hubbub-bindings
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

include_directories(${CMAKE_SOURCE_DIR}/contrib/libdom/src)