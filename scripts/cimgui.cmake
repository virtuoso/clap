# Since only a handful of files is needed from cimgui/imgui, it's easier to
# cook up a small custom build system, plus opengl3 backend header needs a
# bit of patching before a C compiler can compile it, and a few other minor
# tweaks.

file(GLOB
    CIMGUI_SRC
    "${CIMGUI_DIR}/*.cpp"
    "${CIMGUI_DIR}/imgui/*.cpp"
)

FOREACH(backend ${CIMGUI_BACKENDS})
    if (${backend} STREQUAL "metal")
        set(backend_sfx "mm")
    else ()
        set(backend_sfx "cpp")
    endif ()
    set(backend_file "${CIMGUI_DIR}/imgui/backends/imgui_impl_${backend}.${backend_sfx}")
    list(APPEND CIMGUI_SRC "${backend_file}")
    list(APPEND CIMGUI_BACKEND_HEADERS "imgui_impl_${backend}.h")

    # Per-backend compile flags
    set(per_backend_cflags "")
    if (${backend} STREQUAL "metal")
        set(per_backend_cflags "-fobjc-arc")
    endif ()
    if (${backend} STREQUAL "wgpu")
        set(per_backend_cflags "--use-port=emdawnwebgpu")
    endif ()
    set_source_files_properties("${backend_file}" PROPERTIES
        COMPILE_DEFINITIONS "IMGUI_IMPL_API=extern\ \"C\""
        COMPILE_FLAGS "-Wno-nontrivial-memaccess -Wno-deprecated-declarations ${per_backend_cflags}"
    )
ENDFOREACH()

# ImGui's backend headers require a bit of hackery to work with C
FOREACH(backend ${CIMGUI_BACKEND_HEADERS})
    FILE(READ "${CIMGUI_BACKENDS_DIR}/${backend}" backend_src)
    STRING(REPLACE "#include \"imgui.h\"" "" backend_src "${backend_src}")
    STRING(REPLACE "glsl_version = nullptr" "glsl_version" backend_src "${backend_src}")
    FILE(WRITE "${CMAKE_BINARY_DIR}/${backend}" "${backend_src}")
ENDFOREACH ()

# cimgui specific compilation defines to disable C++ mangling: this only needs
# to happen for the cimgui's compilation units, since it's C++
# Also, clang 20.0.0 detects naughtiness all over the imgui code related to
# memcpy()ing into non-trivial objects; the upstream solution seems to be to
# silence the warning:
# https://github.com/ocornut/imgui/commit/419a9ada16eb9b6d84ead4911b9c2a32820cfffb
# so, do it here, but in a less intrusive way
#
# Backend source files already have their flags set above; this loop only
# touches the core cimgui/imgui sources.
FOREACH(src ${CIMGUI_SRC})
    get_source_file_property(_already ${src} COMPILE_FLAGS)
    if (_already STREQUAL "NOTFOUND" OR _already STREQUAL "")
        set_source_files_properties(${src} PROPERTIES
            COMPILE_DEFINITIONS "IMGUI_IMPL_API=extern\ \"C\""
            COMPILE_FLAGS "-Wno-nontrivial-memaccess -Wno-deprecated-declarations"
        )
    endif ()
ENDFOREACH ()

list(APPEND ENGINE_COMPILE_DEFINITIONS "IMGUI_IMPL_API=\ ")

# Now append our own cimgui bindings/wrappers/backend(s)
list(APPEND ENGINE_SRC ui-imgui.c ui-debug.c)

if (CONFIG_RENDERER_METAL)
    list(APPEND CIMGUI_SRC "ui-imgui-metal.m")
    set_source_files_properties("ui-imgui-metal.m" PROPERTIES
        COMPILE_DEFINITIONS "IMGUI_IMPL_API=\ "
        COMPILE_FLAGS "-Wno-nontrivial-memaccess -Wno-deprecated-declarations"
    )
endif ()

if (CONFIG_RENDERER_WGPU)
    list(APPEND ENGINE_SRC ui-imgui-wgpu.cpp)
endif ()

if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    list(APPEND ENGINE_SRC ui-imgui-www.c)
endif ()
