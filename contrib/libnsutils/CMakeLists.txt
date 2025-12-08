add_library(nsutils SHARED
	src/base64.c
	src/time.c
	src/unistd.c
	src/libwapcaplet.c
)
set_target_properties(nsutils PROPERTIES SOVERSION ${NEOSURF_ABI})

install(TARGETS nsutils
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)