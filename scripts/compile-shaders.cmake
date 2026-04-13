# Shader compilation
#
# Compiles GLSL shaders to backend-specific formats (WGSL, MSL, GLSL/GLES)
# via an intermediate SPIR-V representation.
#
# Each backend gets its own SPIR-V intermediate directory (because the
# force-included per-backend shader config header produces different
# preprocessor output) and its own final output directory.
#
# Usage from CMakeLists.txt:
#   include(compile-shaders.cmake)
#   compile_shaders_for_backend("wgpu" "${SHADERS}" "_core")
#   compile_shaders_for_backend("gl"   "${SHADERS}" "_core")

set(GLSLC_ARGS -I${CMAKE_BINARY_DIR}/core -I${CMAKE_SOURCE_DIR}/core)
if (DEFINED GLSL_UBO_DIR)
    list(APPEND GLSLC_ARGS -I${GLSL_UBO_DIR})
endif ()

# GLSL compiler
find_program(GLSLC glslc HINTS "${GLSLC_HINT}")
# SPIR-V decompiler
find_program(SPIRV_CROSS spirv-cross HINTS "${SPIRV_CROSS_HINT}")

# Resolve backend properties.
# Sets in PARENT_SCOPE:
#   _SHADER_TYPE        — output directory prefix ("wgsl", "msl", "glsl", "glsl-es")
#   _SPIRV_CROSS_ARGS   — backend-specific spirv-cross flags
#   _GLSLC_BACKEND_ARGS — per-backend glslc flags (-include, -D)
#   _USE_TINT           — TRUE if this backend uses tint instead of spirv-cross
#   _HAS_REFLECTION     — TRUE if this backend emits .json reflection files
#   _SKIP_GEOM          — TRUE if geometry shaders are not supported
function(_shader_backend_props backend)
    set(_USE_TINT FALSE PARENT_SCOPE)
    set(_HAS_REFLECTION TRUE PARENT_SCOPE)
    set(_SKIP_GEOM FALSE PARENT_SCOPE)
    set(_SPIRV_CROSS_ARGS "" PARENT_SCOPE)
    set(_SHADER_EXT "" PARENT_SCOPE)

    # glslc doesn't support -include; pass per-backend macros as -D flags.
    # The corresponding shader-config-*.h files in shaders/ document what each
    # backend defines, but the actual mechanism is command-line -D.
    set(backend_args "")

    if (backend STREQUAL "gl")
        list(APPEND backend_args -DSHADER_RENDERER_OPENGL=1)
        if (CONFIG_GLES)
            set(_SHADER_TYPE "glsl-es" PARENT_SCOPE)
            set(_SHADER_EXT ".glsl" PARENT_SCOPE)
            set(_SPIRV_CROSS_ARGS --es --version 300 PARENT_SCOPE)
            set(_SKIP_GEOM TRUE PARENT_SCOPE)
            list(APPEND backend_args -DSHADER_GLES=1)
            if (CONFIG_BROWSER)
                list(APPEND backend_args -DSHADER_BROWSER=1)
            endif ()
        else ()
            set(_SHADER_TYPE "glsl" PARENT_SCOPE)
            set(_SHADER_EXT ".glsl" PARENT_SCOPE)
            if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
                set(_SPIRV_CROSS_ARGS --no-es --no-420pack-extension --version 410 PARENT_SCOPE)
            else ()
                set(_SPIRV_CROSS_ARGS --no-es --version 410 PARENT_SCOPE)
            endif ()
        endif ()
        set(_HAS_REFLECTION FALSE PARENT_SCOPE)
    elseif (backend STREQUAL "wgpu")
        list(APPEND backend_args
            -DSHADER_RENDERER_WGPU=1
            -DSHADER_ORIGIN_TOP_LEFT=1
            -DSHADER_NDC_ZERO_ONE=1
            -DSHADER_NDC_Y_DOWN=1
        )
        set(_SHADER_TYPE "wgsl" PARENT_SCOPE)
        set(_SHADER_EXT ".wgsl" PARENT_SCOPE)
        set(_USE_TINT TRUE PARENT_SCOPE)
        set(_SKIP_GEOM TRUE PARENT_SCOPE)
        if (CONFIG_BROWSER)
            list(APPEND backend_args -DSHADER_BROWSER=1)
        endif ()
    elseif (backend STREQUAL "metal")
        list(APPEND backend_args
            -DSHADER_RENDERER_METAL=1
            -DSHADER_ORIGIN_TOP_LEFT=1
            -DSHADER_NDC_ZERO_ONE=1
        )
        set(_SHADER_TYPE "msl" PARENT_SCOPE)
        set(_SHADER_EXT ".msl" PARENT_SCOPE)
        set(_SPIRV_CROSS_ARGS --msl --msl-version 20100 --msl-decoration-binding PARENT_SCOPE)
    else ()
        message(FATAL_ERROR "Unknown shader backend: ${backend}")
    endif ()

    set(_GLSLC_BACKEND_ARGS ${backend_args} PARENT_SCOPE)
endfunction()

# Compile a single shader stage for a given backend.
# All _BACKEND_* variables must be set before calling.
function(_compile_shader_for_backend shader stage backend SHADER_SRCS SHADER_OUTS suffix)
    set(SPIRV_DIR "${SHADER_BASE}/spirv-${backend}${suffix}")
    set(SHADER_DIR "${SHADER_BASE}/${_SHADER_TYPE}${suffix}")
    set(SPIRV_OUTPUT "${SPIRV_DIR}/${shader}.spv")
    set(SPIRV_DEPFILE "${SPIRV_OUTPUT}.d")

    list(APPEND SHADER_SRCS "${SHADER_SOURCE_DIR}/${shader}")
    list(APPEND SHADER_OUTS "${SHADER_DIR}/${shader}${_SHADER_EXT}")
    if (_HAS_REFLECTION)
        list(APPEND SHADER_OUTS "${SHADER_DIR}/${shader}.json")
    endif ()

    set(shader_depfile_args "")
    if (CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        list(APPEND shader_depfile_args DEPFILE "${SPIRV_DEPFILE}")
    endif ()

    # SPIR-V intermediate
    add_custom_command(
        OUTPUT "${SPIRV_OUTPUT}"
        BYPRODUCTS "${SPIRV_DEPFILE}"
        DEPENDS render_bindings_gen make-shader-ir-dir-${backend}${suffix} "${SHADER_SOURCE_DIR}/${shader}"
        COMMAND "${GLSLC}"
        ARGS
            -MD
            -MF "${SPIRV_DEPFILE}"
            -MT "${SPIRV_OUTPUT}"
            -fshader-stage=${stage}
            ${GLSLC_ARGS}
            ${_GLSLC_BACKEND_ARGS}
            -o "${SPIRV_OUTPUT}"
            -c "${SHADER_SOURCE_DIR}/${shader}"
        ${shader_depfile_args}
    )

    # Backend-specific output
    if (_USE_TINT)
        add_custom_command(
            OUTPUT "${SHADER_DIR}/${shader}${_SHADER_EXT}"
            DEPENDS make-shader-output-dir-${backend}${suffix} "${SPIRV_OUTPUT}" "${TINT_BIN_PATH}"
            COMMAND "${TINT_BIN_PATH}"
            ARGS --format wgsl -o "${SHADER_DIR}/${shader}${_SHADER_EXT}" "${SPIRV_OUTPUT}"
        )
    else ()
        add_custom_command(
            OUTPUT "${SHADER_DIR}/${shader}${_SHADER_EXT}"
            DEPENDS make-shader-output-dir-${backend}${suffix} "${SPIRV_OUTPUT}"
            COMMAND "${SPIRV_CROSS}"
            ARGS ${_SPIRV_CROSS_ARGS} "${SPIRV_OUTPUT}" --output "${SHADER_DIR}/${shader}${_SHADER_EXT}"
        )
    endif ()

    # JSON reflection (non-GL backends)
    if (_HAS_REFLECTION)
        add_custom_command(
            OUTPUT "${SHADER_DIR}/${shader}.json"
            DEPENDS make-shader-output-dir-${backend}${suffix} "${SPIRV_OUTPUT}"
            COMMAND "${SPIRV_CROSS}"
            ARGS "${SPIRV_OUTPUT}" --reflect --output "${SHADER_DIR}/${shader}.json"
        )
    endif ()

    set(SHADER_SRCS "${SHADER_SRCS}" PARENT_SCOPE)
    set(SHADER_OUTS "${SHADER_OUTS}" PARENT_SCOPE)
endfunction()

# Compile all shaders for a specific backend.
#
# @backend: "gl", "wgpu", or "metal"
# @SHADERS: list of shader base names (e.g. "model;ui;combine")
# @suffix:  target/directory suffix (e.g. "_core")
#
# Creates targets: preprocess_shaders_${backend}${suffix}
# Propagates: SHADER_OUTS_${backend} to parent scope
function(compile_shaders_for_backend backend SHADERS suffix)
    _shader_backend_props(${backend})

    set(SPIRV_DIR "${SHADER_BASE}/spirv-${backend}${suffix}")
    set(SHADER_DIR "${SHADER_BASE}/${_SHADER_TYPE}${suffix}")

    # Create intermediate and output directories
    add_custom_target(make-shader-ir-dir-${backend}${suffix}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SPIRV_DIR}"
    )
    add_custom_target(make-shader-output-dir-${backend}${suffix}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_DIR}"
    )

    foreach(s ${SHADERS})
        foreach(stage vert frag geom)
            if (stage STREQUAL "geom")
                if (NOT EXISTS "${SHADER_SOURCE_DIR}/${s}.${stage}" OR _SKIP_GEOM)
                    continue ()
                endif ()
            endif ()
            _compile_shader_for_backend(
                "${s}.${stage}" "${stage}" "${backend}"
                "${SHADER_SRCS}" "${SHADER_OUTS}" "${suffix}")
        endforeach ()
    endforeach()

    add_custom_target(preprocess_shaders_${backend}${suffix}
        DEPENDS ${SHADER_OUTS}
        COMMENT "Preprocessing ${backend} shaders"
    )

    set(SHADER_OUTS_${backend} "${SHADER_OUTS}" PARENT_SCOPE)
endfunction()

# Backward-compatible wrapper: compile shaders for the currently active backend.
# This preserves the old API so existing CMakeLists.txt can transition gradually.
function(compile_shaders SHADERS PREPROCESS_SHADERS_TARGET)
    if (CONFIG_RENDERER_WGPU)
        set(_backend "wgpu")
    elseif (CONFIG_RENDERER_METAL)
        set(_backend "metal")
    elseif (CONFIG_RENDERER_OPENGL)
        set(_backend "gl")
    else ()
        message(FATAL_ERROR "No renderer configured")
    endif ()

    compile_shaders_for_backend(${_backend} "${SHADERS}" "${PREPROCESS_SHADERS_TARGET}")

    # Propagate outputs under the old variable name for backward compat
    set(SHADER_OUTS "${SHADER_OUTS_${_backend}}" PARENT_SCOPE)

    # Create the old-style target name as an alias
    if (NOT TARGET preprocess_shaders${PREPROCESS_SHADERS_TARGET})
        add_custom_target(preprocess_shaders${PREPROCESS_SHADERS_TARGET}
            DEPENDS preprocess_shaders_${_backend}${PREPROCESS_SHADERS_TARGET}
        )
    endif ()
endfunction()
