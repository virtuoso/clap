# Helpers for engine subsystems that live in a core/ subdirectory: a static
# library linked into the engine, and an optional standalone test binary that
# shares the harness in core/test-harness.c. Both inherit the engine library's
# include environment (via get_target_property on ${ENGINE_LIB}) so the flags
# in the tiny per-subsystem CMakeLists never drift from the engine's.

# Give <target> the engine's include dirs, plus the current-dir entries CMake
# adds implicitly (core/ and the generated build/core/) which don't appear in
# INCLUDE_DIRECTORIES.
function(_clap_inherit_engine_includes target)
    get_target_property(src_dir ${ENGINE_LIB} SOURCE_DIR)
    get_target_property(bin_dir ${ENGINE_LIB} BINARY_DIR)
    get_target_property(includes ${ENGINE_LIB} INCLUDE_DIRECTORIES)
    target_include_directories(${target} PRIVATE ${src_dir} ${bin_dir} ${includes})
endfunction ()

# clap_subsystem_library(<target> <source>...)
#   Static library from the given sources, with the engine's include
#   environment, the compat shims, engine definitions and warning flags.
#   The subsystem calls back into the engine just as the engine calls into
#   it, so the two static libraries depend on each other; declaring this
#   direction here (the engine links the subsystem in core/CMakeLists.txt)
#   makes cmake repeat both on the executables' link lines, which a
#   single-pass linker needs to resolve the cross references.
function(clap_subsystem_library target)
    add_library(${target} STATIC ${ARGN})
    add_dependencies(${target} ode refclasses_gen cerrs_gen render_bindings_gen)
    set_target_properties(${target} PROPERTIES C_STANDARD 23)
    win32_library(${target} FALSE)
    _clap_inherit_engine_includes(${target})
    target_compile_definitions(${target} PRIVATE ${ENGINE_COMPILE_DEFINITIONS})
    if (NOT WIN32)
        target_compile_options(${target} PRIVATE ${ENGINE_WARNING_FLAGS})
    endif ()
    target_link_libraries(${target} PRIVATE ${ENGINE_LIB})
endfunction ()

# clap_subsystem_test(<target> <source>...)
#   Standalone test executable: the given sources plus the shared harness and
#   the handful of engine objects it pulls in, registered with ctest. Pass the
#   subsystem sources under test (compiled here, not linked from the library)
#   so the binary links only what it needs.
function(clap_subsystem_test target)
    get_target_property(src_dir ${ENGINE_LIB} SOURCE_DIR)
    add_executable(${target}
        ${ARGN}
        ${src_dir}/test-harness.c
        ${src_dir}/object.c ${src_dir}/util.c ${src_dir}/logger.c
        ${src_dir}/messagebus.c ${src_dir}/error.c ${src_dir}/memory.c)
    add_dependencies(${target} refclasses_gen cerrs_gen render_bindings_gen)
    target_compile_definitions(${target} PRIVATE CLAP_TESTS)
    _clap_inherit_engine_includes(${target})
    win32_executable(${target})
    if ((${CMAKE_SYSTEM_NAME} MATCHES "Emscripten"))
        set_target_properties(${target} PROPERTIES SUFFIX ".js")
        target_link_options(${target} PRIVATE ${DEBUG_LIBRARIES} ${CLAP_TEST_LINK_OPTIONS})
    else ()
        target_link_options(${target} PRIVATE ${CLAP_LINK_OPTIONS} ${DEBUG_LIBRARIES})
    endif ()
    add_test(NAME ${target} COMMAND ${target})
endfunction ()
