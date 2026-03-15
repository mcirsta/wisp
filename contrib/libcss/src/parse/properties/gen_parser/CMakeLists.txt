
add_executable(gen_parser
    ../css_property_parser_gen.c
)

# Ensure gen_parser is not compiled with AddressSanitizer (ASAN) on Haiku
# Haiku's libasan doesn't provide dl_iterate_phdr symbol
if(HAIKU AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_target_properties(gen_parser PROPERTIES
        COMPILE_FLAGS "-fno-sanitize=address,undefined -fno-sanitize-recover=all"
        LINK_FLAGS "-fno-sanitize=address,undefined"
    )
endif()


