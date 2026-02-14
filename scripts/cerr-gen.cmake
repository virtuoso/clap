# Generate cerr macros from error.h cerr_enum

# Read the existing file (if it exists)
set(existing_content "")
if (EXISTS "${output}")
    file(READ "${output}" existing_content)
endif()

if (NOT CLAP_BUILD_FINAL)
    set(debug_info " .line = __LINE__, .mod = MODNAME,")
endif ()

file(READ "${dir}/error.h" error_src)

# Convert error constants into tricky struct types
string(REGEX REPLACE "^.*typedef enum cerr_enum {(.*)} cerr_enum.*$" "\\1" error_out ${error_src})
string(REGEX REPLACE " *_([A-Z_]*)[- =0-9]+,\n"
                     "#define \\1 ((const cerr){ .err = _\\1,${debug_info} })\n" error_out_plain ${error_out})
string(REGEX REPLACE " *_([A-Z_]*)[- =0-9]+,\n"
                     "#define \\1_REASON(...) ((const cerr){ .err = _\\1, .reason = { __VA_ARGS__ },${debug_info} })\n" error_out_reason ${error_out})
set(new_content "#ifndef __CLAP_CERRS_H__\n")
set(new_content "${new_content}#define __CLAP_CERRS_H__\n")
set(new_content "${new_content}${error_out_plain}\n")
set(new_content "${new_content}${error_out_reason}\n")
set(new_content "${new_content}#endif /* __CLAP_CERRS_H__ */\n")

# Only write to the file if the content has changed
if (NOT existing_content STREQUAL new_content)
    file(WRITE "${output}" "${new_content}")
endif ()
