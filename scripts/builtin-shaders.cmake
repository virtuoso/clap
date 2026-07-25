# Generate builtin-shaders.c out of compiled shaders
#
# The shader list arrives in a file, one path per line: the per-variant
# list is far too long to pass as a command line argument.
if (NOT DEFINED shaders_file)
    message(FATAL_ERROR "builtin-shaders.cmake requires -Dshaders_file=<list>")
endif ()

file(STRINGS "${shaders_file}" shaders)
list(REMOVE_ITEM shaders "")

if (HEADER)
    LIST(LENGTH shaders NR_SHADERS)
    FILE(WRITE  "${OUTPUT_FILE}" "#include \"librarian-file.h\"\n\n")
    FILE(APPEND "${OUTPUT_FILE}" "const unsigned int nr_builtin_shaders = ${NR_SHADERS};\n")
    FILE(APPEND "${OUTPUT_FILE}" "const struct builtin_file builtin_shaders[${NR_SHADERS}] = {\n")
elseif (FOOTER)
    FILE(APPEND "${OUTPUT_FILE}" "};\n")
else ()
    FOREACH(shader ${shaders})
        get_filename_component(shader_basename ${shader} NAME)
        FILE(SIZE "${shader}" shader_sz)
        if (shader MATCHES "\\.spv$")
            # Binary contents (SPIR-V): FILE(READ) stops at the first NUL
            # byte, so read as hex and emit every byte as a \xHH escape
            FILE(READ "${shader}" shader_hex HEX)
            STRING(REGEX REPLACE "(..)" "\\\\x\\1" shader_src "${shader_hex}")
        else ()
            FILE(READ "${shader}" shader_src)
            STRING(REPLACE "\"" "\\\"" shader_src "${shader_src}")
            STRING(REPLACE "\n" "\\n\"\n    \"" shader_src "${shader_src}")
        endif ()
        FILE(APPEND "${OUTPUT_FILE}" "    {\n")
        FILE(APPEND "${OUTPUT_FILE}" "        .name     = \"${shader_basename}\",\n")
        FILE(APPEND "${OUTPUT_FILE}" "        .size     = ${shader_sz},\n")
        FILE(APPEND "${OUTPUT_FILE}" "        .contents = \"${shader_src}\"\n")
        FILE(APPEND "${OUTPUT_FILE}" "    },\n")
    ENDFOREACH ()
endif()
