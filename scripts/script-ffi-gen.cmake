# script-ffi-gen.cmake -- generate the Umka FFI surface from source markers.
#
# Scans the scripting sources for markers (see core/script/ffi.h) and generates
# two headers into ${output_dir}:
#
#   script-priv-gen.h  -- struct script_priv, embedded in struct script; holds
#                         the per-script state each FFI module declares via
#                         CLAP_SCRIPT_PRIV(). Included wherever struct script is.
#   script-ffi-gen.h   -- the "clap" module source, the umkaAddFunc registration
#                         and the script_priv_{init,persistent,done}() helpers.
#                         Included once, by the module assembler (core.c).
#
# Markers:
#   CLAP_SCRIPT_TYPE("type Foo* = ...")            -> a module type declaration
#   CLAP_SCRIPT_FN(cfunc, "fn name*(sig): ret")    -> an exported free function
#   CLAP_SCRIPT_METHOD(cfunc, "fn (r: T) name*(sig): ret")
#                                                  -> a private flat external
#                                                     (cfunc minus ffi_) plus the
#                                                     public forwarding method
#   CLAP_SCRIPT_PRIV(struct list foo)              -> a struct script_priv member
#   CLAP_SCRIPT_DONE(cfunc)                         -> a script teardown hook,
#                                                     void cfunc(script *)
#
# A marker's proto string may start on the line after cfunc, for the markers
# that don't fit on one.

if (NOT DEFINED dir)
    message(FATAL_ERROR "script-ffi-gen.cmake requires -Ddir=<source dir>")
endif ()
if (NOT DEFINED output_dir)
    message(FATAL_ERROR "script-ffi-gen.cmake requires -Doutput_dir=<dir>")
endif ()

file(GLOB sources "${dir}/*.c")
file(MAKE_DIRECTORY "${output_dir}")

set(types "")       # module type declarations
set(externs "")     # extern C decls of the wrappers
set(protos "")      # module prototypes (externals)
set(methods "")     # module method definitions
set(regs "")        # umkaAddFunc() calls
set(priv "")        # struct script_priv members
set(priv_init "")   # list_init() calls for struct list members
set(priv_persist "") # !list_empty() terms for struct list members
set(done_externs "") # extern decls of teardown hooks
set(done_calls "")  # teardown hook calls

# Method-like markers all share the "(cfunc, \"proto\", ...)" shape and generate a
# method the same way. CLAP_SCRIPT_METHOD has exactly those two args; the FFI_*
# field-accessor macros (core/script/ffi.h) add trailing C-body args (struct, field,
# range) that are irrelevant to the binding and ignored here.
set(method_markers "CLAP_SCRIPT_METHOD|FFI_GET_FLOAT|FFI_SET_FLOAT_RANGE|FFI_SET_FLOAT|FFI_GET_BOOL|FFI_SET_BOOL|FFI_GET_INT|FFI_SET_INT|FFI_GET_VEC3|FFI_SET_VEC3")

foreach (src ${sources})
    file(READ "${src}" text)

    # --- CLAP_SCRIPT_TYPE("type Foo* = ...") ---
    string(REGEX MATCHALL "CLAP_SCRIPT_TYPE[ \t]*\\([ \t]*\"[^\"]*\"[ \t]*\\)" tmatches "${text}")
    foreach (m ${tmatches})
        string(REGEX REPLACE "CLAP_SCRIPT_TYPE[ \t]*\\([ \t]*\"([^\"]*)\".*" "\\1" decl "${m}")
        string(APPEND types "    \"${decl}\\n\"\n")
    endforeach ()

    # --- CLAP_SCRIPT_FN(cfunc, "fn name*(sig): ret") ---
    string(REGEX MATCHALL "CLAP_SCRIPT_FN[ \t]*\\([ \t]*[A-Za-z0-9_]+[ \t]*,[ \t\r\n]*\"[^\"]*\"[ \t]*\\)" fmatches "${text}")
    foreach (m ${fmatches})
        string(REGEX REPLACE "CLAP_SCRIPT_FN[ \t]*\\([ \t]*([A-Za-z0-9_]+).*" "\\1" cfunc "${m}")
        string(REGEX REPLACE "CLAP_SCRIPT_FN[ \t]*\\([^,]*,[ \t\r\n]*\"([^\"]*)\".*" "\\1" proto "${m}")
        string(REGEX REPLACE "^fn[ \t]+([A-Za-z0-9_]+).*" "\\1" name "${proto}")

        string(APPEND externs "extern void ${cfunc}(UmkaStackSlot *, UmkaStackSlot *);\n")
        string(APPEND protos "    \"${proto}\\n\"\n")
        string(APPEND regs "    umkaAddFunc(umka, \"${name}\", ${cfunc});\n")
    endforeach ()

    # --- method-like markers: CLAP_SCRIPT_METHOD + FFI_* accessors ---
    # (cfunc, "fn (r: T) name*(sig): ret"[, C-body args...]); trailing args ignored.
    string(REGEX MATCHALL "(${method_markers})[ \t]*\\([ \t]*[A-Za-z0-9_]+[ \t]*,[ \t\r\n]*\"[^\"]*\"[^)]*\\)" mmatches "${text}")
    foreach (m ${mmatches})
        string(REGEX REPLACE "^[A-Za-z0-9_]+[ \t]*\\([ \t]*([A-Za-z0-9_]+).*" "\\1" cfunc "${m}")
        string(REGEX REPLACE "^[A-Za-z0-9_]+[ \t]*\\([^,]*,[ \t\r\n]*\"([^\"]*)\".*" "\\1" proto "${m}")

        string(REGEX REPLACE "^ffi_" "" flat "${cfunc}")

        string(REGEX REPLACE "^fn[ \t]*\\(([^)]*)\\).*" "\\1" recv "${proto}")
        string(REGEX REPLACE "^[ \t]*([A-Za-z0-9_]+)[ \t]*:[ \t]*([A-Za-z0-9_]+).*" "\\1" rvar "${recv}")
        string(REGEX REPLACE "^[ \t]*([A-Za-z0-9_]+)[ \t]*:[ \t]*([A-Za-z0-9_]+).*" "\\2" rtype "${recv}")

        string(REGEX REPLACE "^fn[ \t]*\\([^)]*\\)[ \t]*([A-Za-z0-9_]+).*" "\\1" name "${proto}")
        string(REGEX REPLACE "^fn[ \t]*\\([^)]*\\)[ \t]*[A-Za-z0-9_]+\\*?[ \t]*\\(([^)]*)\\).*" "\\1" sig "${proto}")

        set(ret "")
        if (proto MATCHES "\\)[ \t]*:[ \t]*([A-Za-z0-9_]+)[ \t]*$")
            set(ret "${CMAKE_MATCH_1}")
        endif ()

        string(REGEX REPLACE ":[ \t]*[A-Za-z0-9_]+" "" fwd "${sig}")
        string(STRIP "${fwd}" fwd)

        set(flat_params "${rvar}: ${rtype}")
        if (NOT sig STREQUAL "")
            set(flat_params "${flat_params}, ${sig}")
        endif ()
        set(flat_ret "")
        if (NOT ret STREQUAL "")
            set(flat_ret ": ${ret}")
        endif ()

        set(call_args "${rvar}^")
        if (NOT fwd STREQUAL "")
            set(call_args "${call_args}, ${fwd}")
        endif ()
        set(body "${flat}(${call_args})")
        if (NOT ret STREQUAL "")
            set(body "return ${body}")
        endif ()

        string(APPEND externs "extern void ${cfunc}(UmkaStackSlot *, UmkaStackSlot *);\n")
        string(APPEND protos "    \"fn ${flat}(${flat_params})${flat_ret}\\n\"\n")
        string(APPEND methods "    \"fn (${rvar}: ^${rtype}) ${name}*(${sig})${flat_ret} { ${body} }\\n\"\n")
        string(APPEND regs "    umkaAddFunc(umka, \"${flat}\", ${cfunc});\n")
    endforeach ()

    # --- CLAP_SCRIPT_PRIV(<member decl>) ---
    string(REGEX MATCHALL "CLAP_SCRIPT_PRIV[ \t]*\\([^)]*\\)" pmatches "${text}")
    foreach (m ${pmatches})
        string(REGEX REPLACE "CLAP_SCRIPT_PRIV[ \t]*\\(([^)]*)\\).*" "\\1" member "${m}")
        string(STRIP "${member}" member)
        string(APPEND priv "    ${member};\n")
        # struct list members get an init and count towards persistence
        if (member MATCHES "^struct[ \t]+list[ \t]+([A-Za-z0-9_]+)$")
            set(lname "${CMAKE_MATCH_1}")
            string(APPEND priv_init "    list_init(&s->priv.${lname});\n")
            string(APPEND priv_persist "        !list_empty(&s->priv.${lname}) ||\n")
        endif ()
    endforeach ()

    # --- CLAP_SCRIPT_DONE(cfunc) ---
    string(REGEX MATCHALL "CLAP_SCRIPT_DONE[ \t]*\\([ \t]*[A-Za-z0-9_]+[ \t]*\\)" dmatches "${text}")
    foreach (m ${dmatches})
        string(REGEX REPLACE "CLAP_SCRIPT_DONE[ \t]*\\([ \t]*([A-Za-z0-9_]+).*" "\\1" cfunc "${m}")
        string(APPEND done_externs "extern void ${cfunc}(script *);\n")
        string(APPEND done_calls "    ${cfunc}(s);\n")
    endforeach ()
endforeach ()

if (regs STREQUAL "")
    set(regs "    (void)umka;\n")
endif ()

# ---------------------------------------------------------------------------
# script-priv-gen.h
# ---------------------------------------------------------------------------
set(pout "/* Generated by scripts/script-ffi-gen.cmake. Do not edit. */\n")
string(APPEND pout "#ifndef __CLAP_SCRIPT_PRIV_GENERATED_H__\n")
string(APPEND pout "#define __CLAP_SCRIPT_PRIV_GENERATED_H__\n\n")
string(APPEND pout "/* Per-script state owned by the FFI modules (CLAP_SCRIPT_PRIV). */\n")
string(APPEND pout "struct script_priv {\n")
if (priv STREQUAL "")
    string(APPEND pout "    char _unused;\n")
else ()
    string(APPEND pout "${priv}")
endif ()
string(APPEND pout "};\n\n")
string(APPEND pout "#endif /* __CLAP_SCRIPT_PRIV_GENERATED_H__ */\n")

# ---------------------------------------------------------------------------
# script-ffi-gen.h
# ---------------------------------------------------------------------------
set(out "/* Generated by scripts/script-ffi-gen.cmake. Do not edit. */\n")
string(APPEND out "/* #include'd once by the module assembler; needs struct script in scope. */\n")
string(APPEND out "#ifndef __CLAP_SCRIPT_FFI_GENERATED_H__\n")
string(APPEND out "#define __CLAP_SCRIPT_FFI_GENERATED_H__\n\n")
string(APPEND out "${externs}${done_externs}\n")
string(APPEND out "static const char clap_script_um[] =\n")
string(APPEND out "    /* types */\n${types}")
string(APPEND out "    /* externals */\n${protos}")
string(APPEND out "    /* methods */\n${methods}")
string(APPEND out "    \"\";\n\n")
string(APPEND out "static void clap_script_ffi_register(Umka *umka)\n{\n${regs}}\n\n")
string(APPEND out "static void script_priv_init(script *s)\n{\n")
if (priv_init STREQUAL "")
    string(APPEND out "    (void)s;\n")
else ()
    string(APPEND out "${priv_init}")
endif ()
string(APPEND out "}\n\n")
string(APPEND out "static bool script_priv_persistent(script *s)\n{\n")
if (priv_persist STREQUAL "")
    string(APPEND out "    (void)s;\n    return false;\n")
else ()
    string(APPEND out "    return\n${priv_persist}        false;\n")
endif ()
string(APPEND out "}\n\n")
string(APPEND out "static void script_priv_done(script *s)\n{\n")
if (done_calls STREQUAL "")
    string(APPEND out "    (void)s;\n")
else ()
    string(APPEND out "${done_calls}")
endif ()
string(APPEND out "}\n\n")
string(APPEND out "#endif /* __CLAP_SCRIPT_FFI_GENERATED_H__ */\n")

# Only rewrite when content changes, to avoid spurious rebuilds.
function(write_if_changed path content)
    set(old "")
    if (EXISTS "${path}")
        file(READ "${path}" old)
    endif ()
    if (NOT old STREQUAL content)
        file(WRITE "${path}" "${content}")
    endif ()
endfunction ()

write_if_changed("${output_dir}/script-priv-gen.h" "${pout}")
write_if_changed("${output_dir}/script-ffi-gen.h" "${out}")
