add_library(nsgif SHARED
	src/gif.c
	src/lzw.c
)
set_target_properties(nsgif PROPERTIES SOVERSION ${NEOSURF_ABI})

install(TARGETS nsgif
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)