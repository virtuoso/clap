# clap-doc API documentation generation

# Append various things from a list of flags to ${cflags}
# ${cflags} is a list variable name
function(cflags_append cflags prefix joined flags)
    foreach(flag ${flags})
        string(STRIP "${flag}" flag)
        if(NOT flag STREQUAL "")
            if (joined)
                list(APPEND ${cflags} ${prefix}${flag})
            else ()
                list(APPEND ${cflags} ${prefix} ${flag})
            endif ()
        endif()
    endforeach()
    set(${cflags} ${${cflags}} PARENT_SCOPE)
endfunction()

# Extract system include directories from the compiler's `-v`
# clang's and gcc's -v outputs seem compatible, this should
# work on both 
function(build_system_cflags cflags)
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -v -E -x c -
        OUTPUT_VARIABLE preproc_out
        ERROR_VARIABLE preproc_out
        INPUT_FILE "/dev/null"
    )

    # Extract only the #include path section
    string(REGEX REPLACE ".*#include <...> search starts here:\n" "" preproc_clean "${preproc_out}")
    string(REGEX REPLACE "\nEnd of search list\..*" "" preproc_clean "${preproc_clean}")

    # Remove chaff
    string(REGEX REPLACE " *\\(framework directory\\)" "" preproc_clean "${preproc_clean}")

    # Make a list
    string(REGEX REPLACE "\n" ";" preproc_list "${preproc_clean}")

    # Add -isystem directories
    cflags_append(${cflags} -isystem FALSE "${preproc_list}")
    set(${cflags} ${${cflags}} PARENT_SCOPE)
endfunction()

if (CLAP_BUILD_DOCS AND
     NOT (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten") AND
     (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))

    build_system_cflags(SYSTEM_CFLAGS)

    add_subdirectory(tools/clap-doc-plugin)

    get_target_property(prefix clapdoc PREFIX)
    get_target_property(suffix clapdoc SUFFIX)
    get_target_property(name clapdoc NAME)
    get_target_property(bindir clapdoc BINARY_DIR)
    set(CLAP_DOC_PLUGIN "${bindir}/${prefix}${name}${suffix}")

    set(CLAP_DOC_DEP "clapdoc")
    set_property(TARGET clapdoc PROPERTY IMPORTED_LOCATION "${CLAP_DOC_PLUGIN}")

    set(CLAP_DOC_API_DIR "${CMAKE_BINARY_DIR}/docs/api/")
    set(CLAP_DOC_OUTPUT "output.base=${CLAP_DOC_API_DIR}")
    set(CLAP_DOC_SRCROOT "src_root=${CMAKE_SOURCE_DIR}/")

    set(ode_exclude_dir "${CMAKE_BINARY_DIR}/ode")
    cmake_path(RELATIVE_PATH ode_exclude_dir BASE_DIRECTORY ${CMAKE_SOURCE_DIR})
    set(CLAP_DOC_EXCLUDE "exclude=deps/,${ode_exclude_dir}")
    set(CLAP_DOC_BACKENDS "backends=json,markdown,c")

    set(CLAP_DOC_OPT
        "-Xclang -load"
        "-Xclang ${CLAP_DOC_PLUGIN}"
        "-Xclang -add-plugin -Xclang clapdoc"
        "-Xclang -plugin-arg-clapdoc -Xclang ${CLAP_DOC_BACKENDS}"
        "-Xclang -plugin-arg-clapdoc -Xclang ${CLAP_DOC_OUTPUT}"
        "-Xclang -plugin-arg-clapdoc -Xclang ${CLAP_DOC_SRCROOT}"
        "-Xclang -plugin-arg-clapdoc -Xclang ${CLAP_DOC_EXCLUDE}")
    list(JOIN CLAP_DOC_OPT " " CLAP_DOC_OPT)
    set(CLAP_DOCGEN_OPT
        "-cc1"
        "-fsyntax-only"
        "-load" "${CLAP_DOC_PLUGIN}"
        "-plugin" "clapdoc"
        "-plugin-arg-clapdoc" "${CLAP_DOC_OUTPUT}"
        "-plugin-arg-clapdoc" "${CLAP_DOC_SRCROOT}"
        "-plugin-arg-clapdoc" "${CLAP_DOC_BACKENDS}"
        "-plugin-arg-clapdoc" "${CLAP_DOC_EXCLUDE}"
    )
endif ()

function(add_clapdoc_target target dir)
    if (NOT CLAP_BUILD_DOCS)
        return ()
    endif ()

    get_target_property(srcs ${target} SOURCES)
    cmake_path(RELATIVE_PATH CLAP_DOC_API_DIR
        BASE_DIRECTORY ${CMAKE_BINARY_DIR}
        OUTPUT_VARIABLE rel_dir
    )

    foreach(src ${srcs})
        if (${src} MATCHES ".*\.cpp$" OR ${src} MATCHES ".*\.mm$" OR
            ${src} MATCHES "builddate.c$" OR ${src} MATCHES "builtin-shaders.c$")
            continue ()
        endif ()
        list(APPEND DOC_DEPS "${rel_dir}${dir}/${src}.json")
    endforeach()

    set(output "${CLAP_DOC_API_DIR}/clap-api.md")
    cmake_path(RELATIVE_PATH output
        BASE_DIRECTORY ${CMAKE_BINARY_DIR}
        OUTPUT_VARIABLE output
    )

    add_custom_command(
        COMMENT "Generating API doc markdown to ${output}"
        OUTPUT ${output}
        DEPENDS ${DOC_DEPS} clapdoc
        COMMAND
            ${CMAKE_COMMAND}
                -Ddir=${dir}
                -Doutput=${output}
                -DCLAP_DOC_API_DIR=${CLAP_DOC_API_DIR}
                -P ${CMAKE_SOURCE_DIR}/scripts/clap-doc-md-gen.cmake
        VERBATIM
    )
    add_custom_target(docgen-${dir} DEPENDS ${output} ${target})
endfunction()

function(make_clapdoc_targets target dir)
    if (NOT CLAP_BUILD_DOCS)
        return ()
    endif ()

    set(LOCAL_CFLAGS "")

    get_target_property(compat_definitions clap_compat INTERFACE_COMPILE_DEFINITIONS)
    get_target_property(compat_options clap_compat INTERFACE_COMPILE_OPTIONS)
    get_target_property(compat_includes clap_compat INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(local_includes ${target} INCLUDE_DIRECTORIES)

    # Add in compat's interface includes/definitions/options
    cflags_append(LOCAL_CFLAGS -D TRUE "${compat_definitions}")
    cflags_append(LOCAL_CFLAGS -I TRUE "${compat_includes}")
    cflags_append(LOCAL_CFLAGS "" TRUE "${compat_options}")

    # Add in target's includes
    cflags_append(LOCAL_CFLAGS -I TRUE "${local_includes}")

    list(APPEND LOCAL_CFLAGS "-I${CMAKE_BINARY_DIR}/${dir}")

    # Pretend to be a real compiler
    list(PREPEND LOCAL_CFLAGS
        "-D__GNUC__=4"
        "-D__GNUC_MINOR__=2"
        "-D__GNUC_PATCHLEVEL__=1"
        "-D__GNUC_STDC_INLINE__=1"
        "-std=c2x"
    )

    file(GLOB headers
        LIST_DIRECTORIES false
        # RELATIVE ${CMAKE_BINARY_DIR}
        "${CMAKE_SOURCE_DIR}/${dir}/*.h"
    )
    # list(APPEND headers "${CMAKE_SOURCE_DIR}/compat/compat.h")
    # if (WIN32)
    #     file(GLOB platform_headers LIST_DIRECTORIES false "${CMAKE_SOURCE_DIR}/compat/windows/*.h")
    # else ()
    #     file(GLOB platform_headers LIST_DIRECTORIES false "${CMAKE_SOURCE_DIR}/compat/shared/*.h")
    # endif ()
    # list(APPEND headers "${platform_headers}")

    foreach(header ${headers})
        cmake_path(
            RELATIVE_PATH header
            BASE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            OUTPUT_VARIABLE rel_header
        )
        cmake_path(GET header FILENAME header_name)

        set(dep ${CLAP_DOC_API_DIR}/${dir}/${header_name})
        cmake_path(
            RELATIVE_PATH dep
            BASE_DIRECTORY ${CMAKE_BINARY_DIR}
        )
        set(dep_json    "${dep}.json")
        set(dep_toc     "${dep}.toc.md")
        set(dep_body    "${dep}.body.md")
        set(dep_missing "${dep}.missing.c")

        add_custom_command(
            COMMAND ${CMAKE_C_COMPILER}
            ARGS
                ${CLAP_DOCGEN_OPT} ${SYSTEM_CFLAGS} ${LOCAL_CFLAGS} ${header}
            COMMENT "Generating API docs for ${dir}/${header_name}"
            OUTPUT
                ${dep_json}
                ${dep_toc}
                ${dep_body}
                ${dep_missing}
            DEPENDS clapdoc refclasses_gen cerrs_gen ${rel_header}
            VERBATIM
        )
        add_custom_target(
            docgen-${header_name}
            DEPENDS ${dep_json}
            VERBATIM
        )
        list(APPEND DOC_DEPS ${dep_json})
    endforeach()

    set(output "${CLAP_DOC_API_DIR}/clap-api.md")
        cmake_path(RELATIVE_PATH output
        BASE_DIRECTORY ${CMAKE_BINARY_DIR}
        OUTPUT_VARIABLE output
    )

    add_custom_command(
        COMMENT "Generating API doc markdown to ${output}"
        OUTPUT ${output}
        DEPENDS ${DOC_DEPS}
        COMMAND
            ${CMAKE_COMMAND}
                -Ddir=${dir}
                -Doutput=${output}
                -P ${CMAKE_SOURCE_DIR}/scripts/clap-doc-md-gen.cmake
        VERBATIM
    )
    add_custom_target(docgen-${dir} DEPENDS ${output} ${target})
endfunction()
