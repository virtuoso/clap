# Shader compilation

set (GLSLC_ARGS -I${CMAKE_BINARY_DIR}/core -I${CMAKE_SOURCE_DIR}/core)
if (DEFINED GLSL_UBO_DIR)
    list(APPEND GLSLC_ARGS -I${GLSL_UBO_DIR})
endif ()
# Set up common variables
if (CONFIG_RENDERER_OPENGL)
    if (CONFIG_GLES)
        set(SHADER_TYPE "glsl-es")
        set(SPIRV_CROSS_ARGS --es --version 300)
    else ()
        set(SHADER_TYPE "glsl")
        if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
            set(SPIRV_CROSS_ARGS --no-es --no-420pack-extension --version 410)
        else ()
            set(SPIRV_CROSS_ARGS --no-es --version 410)
        endif ()
    endif ()
elseif (CONFIG_RENDERER_METAL)
    set(SHADER_TYPE "msl")
    set(SPIRV_CROSS_ARGS --msl --msl-version 20100 --msl-decoration-binding)
endif ()

# Shader output directory
set(SHADER_DIR "${SHADER_BASE}/${SHADER_TYPE}")

# Intermediate SPIR-V directory
set(SPIRV_DIR "${clap_BINARY_DIR}/spirv")

# GLSL compiler
find_program(GLSLC glslc HINTS "${GLSLC_HINT}")
# SPIR-V decompiler
find_program(SPIRV_CROSS spirv-cross HINTS "${SPIRV_CROSS_HINT}")

# Compile a shader
# @shader: shader name ("model.vert")
# @SHADER_SRCS: list of source files for dependencies
# @SHADER_OPTS: list of output files for dependencies
# @PREPROCESS_SHADER_TARGET: directory/target name prefix
function(compile_shader shader SHADER_SRCS SHADER_OUTS PREPROCESS_SHADERS_TARGET)
    # Output directory with the suffix
    set(SHADER_DIR "${SHADER_DIR}${PREPROCESS_SHADERS_TARGET}")
    # Intermediate directory with the suffix
    set(SPIRV_DIR "${SPIRV_DIR}${PREPROCESS_SHADERS_TARGET}")
    set(SPIRV_OUTPUT "${SPIRV_DIR}/${shader}.spv")
    set(SPIRV_DEPFILE "${SPIRV_OUTPUT}.d")

    LIST(APPEND SHADER_SRCS "${SHADER_SOURCE_DIR}/${shader}")
    LIST(APPEND SHADER_OUTS "${SHADER_DIR}/${shader}")
    if (NOT CONFIG_RENDERER_OPENGL)
        LIST(APPEND SHADER_OUTS "${SHADER_DIR}/${shader}.json")
    endif ()

    set(shader_depfile_args "")
    if (CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        list(APPEND shader_depfile_args DEPFILE "${SPIRV_DEPFILE}")
    endif ()

    # Command to build an intermediate representation of ${shader}
    add_custom_command(
        OUTPUT "${SPIRV_OUTPUT}"
        BYPRODUCTS "${SPIRV_DEPFILE}"
        DEPENDS make-shader-ir-dir${PREPROCESS_SHADERS_TARGET} "${SHADER_SOURCE_DIR}/${shader}"
        COMMAND "${GLSLC}"
        ARGS
            -MD
            -MF "${SPIRV_DEPFILE}"
            -MT "${SPIRV_OUTPUT}"
            -fshader-stage=${stage}
            ${GLSLC_ARGS}
            -o "${SPIRV_OUTPUT}"
            -c "${SHADER_SOURCE_DIR}/${shader}"
        ${shader_depfile_args}
    )

    # Command to build the final ${shader}
    add_custom_command(
        OUTPUT "${SHADER_DIR}/${shader}"
        DEPENDS make-shader-output-dir${PREPROCESS_SHADERS_TARGET} "${SPIRV_OUTPUT}"
        COMMAND "${SPIRV_CROSS}"
        ARGS ${SPIRV_CROSS_ARGS} "${SPIRV_OUTPUT}" --output "${SHADER_DIR}/${shader}"
    )

    if (NOT CONFIG_RENDERER_OPENGL)
        add_custom_command(
            OUTPUT "${SHADER_DIR}/${shader}.json"
            DEPENDS make-shader-output-dir${PREPROCESS_SHADERS_TARGET} "${SPIRV_OUTPUT}"
            COMMAND "${SPIRV_CROSS}"
            ARGS "${SPIRV_OUTPUT}" --reflect --output "${SHADER_DIR}/${shader}.json"
        )
    endif ()

    # Propagate sources and outputs to the outer scope
    set(SHADER_SRCS "${SHADER_SRCS}" PARENT_SCOPE)
    set(SHADER_OUTS "${SHADER_OUTS}" PARENT_SCOPE)
endfunction()

# Compile all shaders on the list
# For each shader compile ${shader}.vert, ${shader}.frag and optionally ${shader}.geom
# @SHADERS: list of shader names
# @PREPROCESS_SHADER_TARGET: directory/target name prefix
function(compile_shaders SHADERS PREPROCESS_SHADERS_TARGET)
    # Make a directory for the intermediate SPIR-Vs
    add_custom_target(make-shader-ir-dir${PREPROCESS_SHADERS_TARGET}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SPIRV_DIR}${PREPROCESS_SHADERS_TARGET}"
    )
    # Make a directory for the output shaders
    add_custom_target(make-shader-output-dir${PREPROCESS_SHADERS_TARGET}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_DIR}${PREPROCESS_SHADERS_TARGET}"
    )

    FOREACH(s ${SHADERS})
        FOREACH(stage vert frag geom)
            # Geometry shaders are optional in GL and not supported in GL ES
            if (stage STREQUAL "geom")
                if (NOT EXISTS "${SHADER_SOURCE_DIR}/${s}.${stage}" OR CONFIG_GLES)
                    continue ()
                endif ()
            endif ()
            compile_shader("${s}.${stage}" "${SHADER_SRCS}" "${SHADER_OUTS}" "${PREPROCESS_SHADERS_TARGET}")
        ENDFOREACH ()
    ENDFOREACH()

    set(SHADER_OUTS "${SHADER_OUTS}" PARENT_SCOPE)

    # Create a preprocess_shaders target with a suffix
    add_custom_target(preprocess_shaders${PREPROCESS_SHADERS_TARGET}
        DEPENDS ${SHADER_OUTS}
        COMMENT "Preprocessing shaders"
    )
endfunction()
