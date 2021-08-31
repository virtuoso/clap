# Compile a Rust crate and copy the *.so into the current binary dir. This 
# also sets the LOCATION of the provided target to be the generated binary.
#
# A test target named ${target_name}_test is also generated.
function(cargo_library target_name project_dir)
    file(GLOB sources ${project_dir}/src/**/*.rs)
    # Building a 'staticlib' requires manually adding -pthread -ldl -lm to the
    # final linkage. An option to avoid this is to build 'cdynlib' instead,
    # but that makes emcc fail the rust linking stage. We do need the
    # wasm32-unknown-emscripten target, because wasm32-unknown-unknown
    # produces broken wasm at the moment. So, 'staticlib'.
    set(output_library ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_DIR}/lib${target_name}.a)
    set(CARGO_ENV "")

    set(compile_message "Compiling ${target_name}")

    if(CARGO_RELEASE_FLAG STREQUAL "--release")
        set(compile_message "${compile_message} in release mode")
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
        set(output_library ${CMAKE_CURRENT_BINARY_DIR}/wasm32-unknown-emscripten/${TARGET_DIR}/lib${target_name}.a)
        set(CARGO_OPTS --target wasm32-unknown-emscripten)
        set(CARGO_ENV "")
    else ()
        set(CARGO_OPTS "")
    endif ()

    add_custom_target(${target_name} ALL 
        COMMENT ${compile_message}
        COMMAND env CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR} ${CARGO_ENV} cargo +nightly build ${CARGO_OPTS} ${CARGO_RELEASE_FLAG}
        COMMAND cp ${output_library} ${CMAKE_CURRENT_BINARY_DIR}
        WORKING_DIRECTORY ${project_dir})
    set_target_properties(${target_name} PROPERTIES LOCATION ${output_library})

    add_test(NAME ${target_name}_test 
        COMMAND env CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR} cargo test ${CARGO_RELEASE_FLAG}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()
