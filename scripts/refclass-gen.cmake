# Generate __obj_ref() macro that returns struct ref * for an
# object of a type that's been declared with DECLARE_REFCLASS(),
# because at least some of such objects are private to their
# respective compilation units and are opaque to everybody else,
# but we still want ref_get() and ref_put() to work

list(APPEND decls "")
file(GLOB sources "${dir}/*.h" "${dir}/*.c")
file(WRITE  "${output}" "#ifndef __CLAP_REFCLASS_H_GENERATED__\n")
file(APPEND "${output}" "#define __CLAP_REFCLASS_H_GENERATED__\n\n")
file(APPEND "${output}" "struct ref;\n\n")
file(APPEND "${output}" "static inline struct ref *__bad_object(void *x) { return NULL; };\n\n")
foreach (src ${sources})
    file(READ "${src}" text)
    string(REGEX MATCHALL "DECLARE_REFCLASS\\(([A-Za-z_0-9]*)\\);" decl "${text}")
    if (NOT ("${decl}" STREQUAL ""))
        foreach(str ${decl})
            string(REPLACE "DECLARE_REFCLASS(" "" str ${str})
            string(REPLACE ")" "" str ${str})
            list(APPEND decls "${str}")
        endforeach()
    endif ()
endforeach ()

foreach (decl ${decls})
    file(APPEND "${output}" "struct ref *${decl}_ref(void *x);\n")
endforeach ()

file(APPEND "${output}" "\n#define __obj_ref(_obj) (_Generic((_obj), \\\n")
foreach (decl ${decls})
    file(APPEND "${output}" "    struct ${decl} *: ${decl}_ref, \\\n")
endforeach ()
file(APPEND "${output}" "    default: __bad_object)((_obj)))\n\n")
file(APPEND "${output}" "#endif /* __CLAP_REFCLASS_H_GENERATED__ */\n")
