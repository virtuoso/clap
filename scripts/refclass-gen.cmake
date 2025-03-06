# Generate __obj_ref() macro that returns struct ref * for an
# object of a type that's been declared with DECLARE_REFCLASS(),
# because at least some of such objects are private to their
# respective compilation units and are opaque to everybody else,
# but we still want ref_get() and ref_put() to work

list(APPEND decls "")
file(GLOB sources "${dir}/*.h" "${dir}/*.c")

# Read the existing file (if it exists)
set(existing_content "")
if (EXISTS "${output}")
    file(READ "${output}" existing_content)
endif()

# Write the new contents into a variable first, so it can be compared
# against the existing contents and only update the file if they differ
set(new_content "#ifndef __CLAP_REFCLASS_H_GENERATED__\n")
set(new_content "${new_content}#define __CLAP_REFCLASS_H_GENERATED__\n\n")
set(new_content "${new_content}struct ref;\n\n")
set(new_content "${new_content}static inline struct ref *__bad_object(void *x) { return NULL; };\n\n")

# Trawl through the source files for the declarations of refclasses,
# store the caught ones in ${decl} list
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

# Declare the ref* obtaining functions defined in DEFINE_REFCLASS*()
foreach (decl ${decls})
    set(new_content "${new_content}struct ref *${decl}_ref(void *x);\n")
endforeach ()

# Define a macro _Generic over the give type to select the ref* obtaining
# function appropriate to the type and call it
set(new_content "${new_content}\n#define __obj_ref(_obj) (_Generic((_obj), \\\n")
foreach (decl ${decls})
    set(new_content "${new_content}    struct ${decl} *: ${decl}_ref, \\\n")
endforeach ()

# Fallback for unknown types, returns NULL: a guaranteed immediate SEGFAULT
# is always better than obscure memory corruptions with delayed effect
set(new_content "${new_content}    default: __bad_object)((_obj)))\n\n")
set(new_content "${new_content}#endif /* __CLAP_REFCLASS_H_GENERATED__ */\n")

# Only write to the file if the content has changed
if (NOT existing_content STREQUAL new_content)
    file(WRITE "${output}" "${new_content}")
endif ()
