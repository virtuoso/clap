# Generate cerr macros from error.h cerr_enum
#
if (NOT CLAP_BUILD_FINAL)
    set(debug_info " .line = __LINE__, .mod = MODNAME,")
endif ()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/error.h" error_src)
string(REGEX REPLACE "^.*typedef enum cerr_enum {(.*)} cerr_enum.*$" "\\1" error_out ${error_src})
string(REGEX REPLACE " *_([A-Z_]*)[\- =0-9]+,\n"
                     "#define \\1 ((const cerr){ .err = _\\1,${debug_info} })\n" error_out ${error_out})
file(WRITE  ${CMAKE_CURRENT_BINARY_DIR}/cerrs.h "#ifndef __CLAP_CERRS_H__\n")
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/cerrs.h "#define __CLAP_CERRS_H__\n")
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/cerrs.h "${error_out}\n")
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/cerrs.h "#endif /* __CLAP_CERRS_H__ */\n")
