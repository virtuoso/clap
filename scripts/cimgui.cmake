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
    list(APPEND CIMGUI_SRC "${CIMGUI_DIR}/imgui/backends/imgui_impl_${backend}.cpp")
    list(APPEND CIMGUI_BACKEND_HEADERS "imgui_impl_${backend}.h")
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
FOREACH(src ${CIMGUI_SRC})
    set_source_files_properties(${src} PROPERTIES
        COMPILE_DEFINITIONS "IMGUI_IMPL_API=extern\ \"C\";IMGUI_IMPL_OPENGL_LOADER_GL3W"
        COMPILE_FLAGS "-Wno-nontrivial-memaccess"
    )
ENDFOREACH ()

list(APPEND ENGINE_COMPILE_DEFINITIONS "IMGUI_IMPL_API=\ ")

# Now append our own cimgui bindings/wrappers/backend(s)
list(APPEND ENGINE_SRC ui-imgui.c ui-debug.c)

if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    list(APPEND ENGINE_SRC ui-imgui-www.c)
endif ()
