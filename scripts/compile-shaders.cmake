# Shader compilation
#
# Compiles GLSL shaders to backend-specific formats (WGSL, MSL, GLSL/GLES,
# SPIR-V) via an intermediate SPIR-V representation.
#
# Each backend gets its own SPIR-V intermediate directory (because the
# force-included per-backend shader config header produces different
# preprocessor output) and its own final output directory.
#
# Usage from CMakeLists.txt:
#   include(compile-shaders.cmake)
#   compile_shaders_for_backend("wgpu" "${SHADERS}" "_core")
#   compile_shaders_for_backend("gl"   "${SHADERS}" "_core")

include("${CMAKE_SOURCE_DIR}/scripts/shader-variants.cmake")

set(GLSLC_ARGS -I${CMAKE_BINARY_DIR}/core -I${CMAKE_SOURCE_DIR}/core)
if (DEFINED GLSL_UBO_DIR)
    list(APPEND GLSLC_ARGS -I${GLSL_UBO_DIR})
endif ()

# Default variant axis set: what the plain (uber) "model"/"shadow"/
# "shadow_vsm" shaders and every non-variant shader compile with, and
# the mask the base names resolve to in the generated lookup tables.
# The variant families additionally compile their debug companions
# and, with CLAP_SHADER_VARIANTS, one specialization per valid axis
# combination; see shader-variants.cmake (SHADER_VARIANT_BLINN_PHONG
# selects the cheaper BRDF and stays off by default). See
# model.vert/model.frag/lighting.glsl.
set(SHADER_VARIANT_DEFAULTS
    NORMALS
    SKINNING
    NOISE_MATERIALS
    ALPHA_DISCARD
    SHADOWS
    SHADOW_PCF
    RADIAL_FOG
)

# Unlike the axes above, TAA follows the build config: the model pass
# RT layout (velocity RT present or not) must agree between the shaders
# and pipeline-builder.c, so this axis can never vary per material
if (CONFIG_TAA)
    list(APPEND SHADER_VARIANT_DEFAULTS TAA)
endif ()

# The default set rides on every plain compile as a per-compile argument
# (not in GLSLC_ARGS: defines are additive, so variant specializations
# must start from none and pass exactly their mask's subset instead)
set(GLSLC_VARIANT_DEFAULT_ARGS "")
foreach (axis ${SHADER_VARIANT_DEFAULTS})
    shader_variant_define(${axis} def)
    list(APPEND GLSLC_VARIANT_DEFAULT_ARGS -DSHADER_VARIANT_${def}=1)
endforeach ()

# GLSL compiler
find_program(GLSLC glslc HINTS "${GLSLC_HINT}")
# SPIR-V decompiler
find_program(SPIRV_CROSS spirv-cross HINTS "${SPIRV_CROSS_HINT}")
# SPIR-V optimizer, for the backends whose bind group layouts come from
# the shader's statically used resources
find_program(SPIRV_OPT spirv-opt HINTS "${SPIRV_OPT_HINT}")

# Resolve backend properties.
# Sets in PARENT_SCOPE:
#   _SHADER_TYPE        -- output directory prefix ("wgsl", "msl", "glsl", "glsl-es", "spv")
#   _SPIRV_CROSS_ARGS   — backend-specific spirv-cross flags
#   _GLSLC_BACKEND_ARGS — per-backend glslc flags (-include, -D)
#   _USE_TINT           — TRUE if this backend uses tint instead of spirv-cross
#   _USE_COPY           -- TRUE if the SPIR-V intermediate is the final artifact
#   _HAS_REFLECTION     — TRUE if this backend emits .json reflection files
#   _SKIP_GEOM          — TRUE if geometry shaders are not supported
function(_shader_backend_props backend)
    set(_USE_TINT FALSE PARENT_SCOPE)
    set(_USE_COPY FALSE PARENT_SCOPE)
    set(_HAS_REFLECTION TRUE PARENT_SCOPE)
    set(_SKIP_GEOM FALSE PARENT_SCOPE)
    set(_SPIRV_CROSS_ARGS "" PARENT_SCOPE)
    set(_SHADER_EXT "" PARENT_SCOPE)

    # glslc doesn't support -include; pass per-backend macros as -D flags.
    # The corresponding shader-config-*.h files in shaders/ document what each
    # backend defines, but the actual mechanism is command-line -D.
    set(backend_args "")

    if (backend STREQUAL "gl")
        # no explicit fp16 arithmetic in GLSL, half.glsl falls back to mediump
        list(APPEND backend_args -DSHADER_RENDERER_OPENGL=1 -DSHADER_NO_FP16=1)
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
            # f16 in WGSL needs the optional shader-f16 feature; until the
            # renderer requests it, half.glsl falls back to mediump
            -DSHADER_NO_FP16=1
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
    elseif (backend STREQUAL "vulkan")
        list(APPEND backend_args
            -DSHADER_RENDERER_VULKAN=1
            -DSHADER_ORIGIN_TOP_LEFT=1
            -DSHADER_NDC_ZERO_ONE=1
            -DSHADER_NDC_Y_DOWN=1
            --target-env=vulkan1.3
        )
        # Devices without VkPhysicalDeviceVulkan12Features::shaderFloat16
        # (RPi5/v3dv): fall back to mediump/RelaxedPrecision in half.glsl
        if (CONFIG_SHADER_NO_FP16)
            list(APPEND backend_args -DSHADER_NO_FP16=1)
        endif ()
        set(_SHADER_TYPE "spv" PARENT_SCOPE)
        set(_SHADER_EXT ".spv" PARENT_SCOPE)
        set(_USE_COPY TRUE PARENT_SCOPE)
    else ()
        message(FATAL_ERROR "Unknown shader backend: ${backend}")
    endif ()

    set(_GLSLC_BACKEND_ARGS ${backend_args} PARENT_SCOPE)
endfunction()

# Compile a single shader stage for a given backend.
# All _BACKEND_* variables must be set before calling.
# @shader is the output basename (e.g. "model.0131.vert"), @src the GLSL
# source path: they differ for variant specializations of an uber shader.
# @variant_args is the compile's SHADER_VARIANT_* define set: the global
# default set for plain shaders, the mask's exact subset for variants
function(_compile_shader_for_backend shader src stage backend SHADER_SRCS SHADER_OUTS suffix variant_args)
    set(SPIRV_DIR "${SHADER_BASE}/spirv-${backend}${suffix}")
    set(SHADER_DIR "${SHADER_BASE}/${_SHADER_TYPE}${suffix}")
    set(SPIRV_OUTPUT "${SPIRV_DIR}/${shader}.spv")
    set(SPIRV_DEPFILE "${SPIRV_OUTPUT}.d")

    list(APPEND SHADER_SRCS "${src}")
    list(APPEND SHADER_OUTS "${SHADER_DIR}/${shader}${_SHADER_EXT}")
    if (_HAS_REFLECTION)
        list(APPEND SHADER_OUTS "${SHADER_DIR}/${shader}${_SHADER_EXT}.json")
    endif ()

    set(shader_depfile_args "")
    if (CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        list(APPEND shader_depfile_args DEPFILE "${SPIRV_DEPFILE}")
    endif ()

    # SPIR-V intermediate
    add_custom_command(
        OUTPUT "${SPIRV_OUTPUT}"
        BYPRODUCTS "${SPIRV_DEPFILE}"
        DEPENDS render_bindings_gen make-shader-ir-dir-${backend}${suffix} "${src}"
        COMMAND "${GLSLC}"
        ARGS
            -MD
            -MF "${SPIRV_DEPFILE}"
            -MT "${SPIRV_OUTPUT}"
            -fshader-stage=${stage}
            ${GLSLC_ARGS}
            ${_GLSLC_BACKEND_ARGS}
            ${variant_args}
            -o "${SPIRV_OUTPUT}"
            -c "${src}"
        ${shader_depfile_args}
    )

    # WebGPU derives a pipeline's bind group layout from the resources its
    # shaders statically use, and rejects a bind group with an entry for
    # anything else. A specialization of an uber shader keeps the uniform
    # blocks its includes declare for the axes it compiles out, and the
    # reflection would list them: strip the dead declarations first, so
    # the reflection and the WGSL carry exactly what the pipeline binds.
    # Keep the stage interface, though: the dead code elimination drops
    # the inputs a shader doesn't read, and a prog's vertex layout comes
    # from its reflected inputs; a model's shadow prog draws the vertex
    # buffer laid out for the model's own prog, so it needs all of them
    if (_USE_TINT)
        set(SPIRV_USED "${SPIRV_DIR}/${shader}.used.spv")
        add_custom_command(
            OUTPUT "${SPIRV_USED}"
            DEPENDS "${SPIRV_OUTPUT}"
            COMMAND "${SPIRV_OPT}"
            ARGS --preserve-interface --eliminate-dead-code-aggressive -o "${SPIRV_USED}" "${SPIRV_OUTPUT}"
        )
    else ()
        set(SPIRV_USED "${SPIRV_OUTPUT}")
    endif ()

    # Backend-specific output
    if (_USE_TINT)
        add_custom_command(
            OUTPUT "${SHADER_DIR}/${shader}${_SHADER_EXT}"
            DEPENDS make-shader-output-dir-${backend}${suffix} "${SPIRV_USED}" "${TINT_BIN_PATH}"
            COMMAND "${TINT_BIN_PATH}"
            ARGS --format wgsl -o "${SHADER_DIR}/${shader}${_SHADER_EXT}" "${SPIRV_USED}"
        )
    elseif (_USE_COPY)
        # copy_if_different leaves the destination mtime behind the source's
        # when the content matches, which makes this rule fire on every build
        # forever; the explicit touch settles it after one run
        add_custom_command(
            OUTPUT "${SHADER_DIR}/${shader}${_SHADER_EXT}"
            DEPENDS make-shader-output-dir-${backend}${suffix} "${SPIRV_OUTPUT}"
            COMMAND ${CMAKE_COMMAND}
            ARGS -E copy_if_different "${SPIRV_OUTPUT}" "${SHADER_DIR}/${shader}${_SHADER_EXT}"
            COMMAND ${CMAKE_COMMAND} -E touch "${SHADER_DIR}/${shader}${_SHADER_EXT}"
        )
    else ()
        add_custom_command(
            OUTPUT "${SHADER_DIR}/${shader}${_SHADER_EXT}"
            DEPENDS make-shader-output-dir-${backend}${suffix} "${SPIRV_OUTPUT}"
            COMMAND "${SPIRV_CROSS}"
            ARGS ${_SPIRV_CROSS_ARGS} "${SPIRV_OUTPUT}" --output "${SHADER_DIR}/${shader}${_SHADER_EXT}"
        )
    endif ()

    # JSON reflection (non-GL backends): the bindings in it are this
    # backend's, so it carries the shading language suffix like the shader
    # it describes does; the runtime asks for it under that name, which
    # keeps a GL build from picking up another backend's reflection when
    # several are packed together
    if (_HAS_REFLECTION)
        add_custom_command(
            OUTPUT "${SHADER_DIR}/${shader}${_SHADER_EXT}.json"
            DEPENDS make-shader-output-dir-${backend}${suffix} "${SPIRV_USED}"
            COMMAND "${SPIRV_CROSS}"
            ARGS "${SPIRV_USED}" --reflect --output "${SHADER_DIR}/${shader}${_SHADER_EXT}.json"
        )
    endif ()

    set(SHADER_SRCS "${SHADER_SRCS}" PARENT_SCOPE)
    set(SHADER_OUTS "${SHADER_OUTS}" PARENT_SCOPE)
endfunction()

# Compile all shaders for a specific backend.
#
# @backend: "gl", "wgpu", "metal" or "vulkan"
# @SHADERS: list of shader base names (e.g. "model;ui;combine")
# @suffix:  target/directory suffix (e.g. "_core")
#
# Creates targets: preprocess_shaders_${backend}${suffix}
# Propagates: SHADER_OUTS_${backend} to parent scope
function(compile_shaders_for_backend backend SHADERS suffix)
    _shader_backend_props(${backend})
    if (_USE_TINT AND NOT SPIRV_OPT)
        message(FATAL_ERROR "spirv-opt is required for the ${backend} shaders (SPIRV_OPT_HINT)")
    endif ()

    set(SPIRV_DIR "${SHADER_BASE}/spirv-${backend}${suffix}")
    set(SHADER_DIR "${SHADER_BASE}/${_SHADER_TYPE}${suffix}")

    # Create intermediate and output directories
    add_custom_target(make-shader-ir-dir-${backend}${suffix}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SPIRV_DIR}"
    )
    add_custom_target(make-shader-output-dir-${backend}${suffix}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_DIR}"
    )

    # A name's stages are the ones it has sources for: a prog can lack a
    # geometry stage, and a vertex shader built as an entry of its own has
    # no fragment stage. Geometry also needs the backend's support.
    foreach(s ${SHADERS})
        foreach(stage vert frag geom)
            if (NOT EXISTS "${SHADER_SOURCE_DIR}/${s}.${stage}")
                continue ()
            endif ()
            if (stage STREQUAL "geom" AND _SKIP_GEOM)
                continue ()
            endif ()
            _compile_shader_for_backend(
                "${s}.${stage}" "${SHADER_SOURCE_DIR}/${s}.${stage}" "${stage}" "${backend}"
                "${SHADER_SRCS}" "${SHADER_OUTS}" "${suffix}" "${GLSLC_VARIANT_DEFAULT_ARGS}")
        endforeach ()
    endforeach()

    # Variant specializations, named "<family>.<mask hex>": the debug
    # companions always, plus one compile of each family's stages per
    # valid axis combination with CLAP_SHADER_VARIANTS; the default
    # combination is the plain "<family>" compile above
    foreach (family ${SHADER_VARIANT_FAMILIES})
        if (NOT family IN_LIST SHADERS)
            continue ()
        endif ()

        shader_variant_default_mask(${family} default_mask)
        shader_variant_debug_masks(${family} masks)
        if (CLAP_SHADER_VARIANTS)
            shader_variant_enumerate(${family} full_masks)
            list(APPEND masks ${full_masks})
        endif ()
        foreach (mask ${masks})
            if (mask EQUAL default_mask)
                continue ()
            endif ()

            shader_variant_suffix(${mask} sfx)
            shader_variant_glslc_args(${family} ${mask} variant_args)
            foreach (stage vert frag geom)
                if (NOT EXISTS "${SHADER_SOURCE_DIR}/${family}.${stage}")
                    continue ()
                endif ()
                if (stage STREQUAL "geom" AND _SKIP_GEOM)
                    continue ()
                endif ()
                _compile_shader_for_backend(
                    "${family}.${sfx}.${stage}" "${SHADER_SOURCE_DIR}/${family}.${stage}"
                    "${stage}" "${backend}"
                    "${SHADER_SRCS}" "${SHADER_OUTS}" "${suffix}" "${variant_args}")
            endforeach ()
        endforeach ()
    endforeach ()

    add_custom_target(preprocess_shaders_${backend}${suffix}
        DEPENDS ${SHADER_OUTS}
        COMMENT "Preprocessing ${backend} shaders"
    )

    set(SHADER_OUTS_${backend} "${SHADER_OUTS}" PARENT_SCOPE)

    # Create the old-style target name as an alias
    if (NOT TARGET preprocess_shaders${PREPROCESS_SHADERS_TARGET})
        add_custom_target(preprocess_shaders${PREPROCESS_SHADERS_TARGET}
            DEPENDS preprocess_shaders_${_backend}${PREPROCESS_SHADERS_TARGET}
        )
    endif ()
endfunction()

# Backward-compatible wrapper: compile shaders for the currently active backend.
# This preserves the old API so existing CMakeLists.txt can transition gradually.
function(compile_shaders SHADERS PREPROCESS_SHADERS_TARGET)
    if (CONFIG_RENDERER_WGPU)
        set(_backend "wgpu")
        compile_shaders_for_backend(${_backend} "${SHADERS}" "${PREPROCESS_SHADERS_TARGET}")
        list(APPEND shader_outs ${SHADER_OUTS_${_backend}})
    endif ()
    if (CONFIG_RENDERER_METAL)
        set(_backend "metal")
        compile_shaders_for_backend(${_backend} "${SHADERS}" "${PREPROCESS_SHADERS_TARGET}")
        list(APPEND shader_outs ${SHADER_OUTS_${_backend}})
    endif ()
    if (CONFIG_RENDERER_OPENGL)
        set(_backend "gl")
        compile_shaders_for_backend(${_backend} "${SHADERS}" "${PREPROCESS_SHADERS_TARGET}")
        list(APPEND shader_outs ${SHADER_OUTS_${_backend}})
    endif ()
    if (CONFIG_RENDERER_VULKAN)
        set(_backend "vulkan")
        compile_shaders_for_backend(${_backend} "${SHADERS}" "${PREPROCESS_SHADERS_TARGET}")
        list(APPEND shader_outs ${SHADER_OUTS_${_backend}})
    endif ()

    # Propagate outputs under the old variable name for backward compat
    set(SHADER_OUTS "${shader_outs}" PARENT_SCOPE)
endfunction()
