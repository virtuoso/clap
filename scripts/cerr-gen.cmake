# Generate cerr macros from error.h cerr_enum
#
if (NOT CLAP_BUILD_FINAL)
    set(debug_info " .line = __LINE__, .mod = MODNAME,")
endif ()

file(READ "${dir}/error.h" error_src)
string(REGEX REPLACE "^.*typedef enum cerr_enum {(.*)} cerr_enum.*$" "\\1" error_out ${error_src})
string(REGEX REPLACE " *_([A-Z_]*)[- =0-9]+,\n"
                     "#define \\1 ((const cerr){ .err = _\\1,${debug_info} })\n" error_out ${error_out})
file(WRITE  "${output}" "#ifndef __CLAP_CERRS_H__\n")
file(APPEND "${output}" "#define __CLAP_CERRS_H__\n")
file(APPEND "${output}" "${error_out}\n")
file(APPEND "${output}" "#endif /* __CLAP_CERRS_H__ */\n")
