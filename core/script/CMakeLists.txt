# The Umka scripting subsystem, built as a static library and linked into the
# engine. The FFI surface is generated from the CLAP_SCRIPT_* markers in the
# sources by scripts/script-ffi-gen.cmake.

set(SCRIPT_SRC core.c umka.c frame.c ffi-scene.c ffi-ui.c)

set(SCRIPT_FFI_H "${CMAKE_CURRENT_BINARY_DIR}/script-ffi-gen.h")
set(SCRIPT_PRIV_H "${CMAKE_CURRENT_BINARY_DIR}/script-priv-gen.h")
add_custom_command(
    COMMENT "Generating Umka FFI bindings"
    OUTPUT "${SCRIPT_FFI_H}" "${SCRIPT_PRIV_H}"
    DEPENDS ${SCRIPT_SRC} "${CMAKE_SOURCE_DIR}/scripts/script-ffi-gen.cmake"
    COMMAND
        ${CMAKE_COMMAND}
            -Ddir="${CMAKE_CURRENT_SOURCE_DIR}"
            -Doutput_dir="${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_SOURCE_DIR}/scripts/script-ffi-gen.cmake"
)
add_custom_target(script_ffi_gen DEPENDS "${SCRIPT_FFI_H}" "${SCRIPT_PRIV_H}")

clap_subsystem_library(clap_script ${SCRIPT_SRC})
add_dependencies(clap_script script_ffi_gen)

# The interpreter is this subsystem's dependency: its headers come with the
# target, and the executables pick up the archive through this library.
target_link_libraries(clap_script PRIVATE umka)
