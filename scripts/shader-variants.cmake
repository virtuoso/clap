# Shader variant specialization
#
# The model and shadow uber shaders compile into specialized shaders, one
# per combination of the SHADER_VARIANT_* axes their sources respond to.
# The C runtime resolves a variant bitmask to a shader name through
# shader_get_name() (core/shader.c), backed by tables this module
# generates alongside the compile rules, so the two cannot drift apart.
#
# Canonical axis registry: list index == bit position == the C-side
# SHADER_<name>_BIT value and the hex suffix digit in generated shader
# names ("model.0131"). Bit positions are frozen: append new axes at the
# end, never reorder.
# Record format: <bit name>|<GLSL define suffix> (SHADER_VARIANT_<suffix>).
set(SHADER_VARIANT_AXES
    "NORMALS|NORMALS"
    "SKINNING|SKINNING"
    "NOISE_MATERIALS|NOISE_MATERIALS"
    "ALPHA_DISCARD|ALPHA_DISCARD"
    "SHADOWS|SHADOWS"
    "SHADOW_PCF|SHADOW_PCF"
    "RADIAL_FOG|RADIAL_FOG"
    "BLINN_PHONG|BLINN_PHONG"
    "TAA|TAA"
)

# Variant families: uber shaders that specialize, each over the axes its
# stages (and their includes) actually reference. A bit outside a
# family's axis set makes the mask invalid for that family and
# shader_get_name() fails with CERR_INVALID_ARGUMENTS rather than ignoring it.
set(SHADER_VARIANT_FAMILIES model shadow shadow_vsm)

set(SHADER_VARIANT_AXES_model
    NORMALS SKINNING NOISE_MATERIALS ALPHA_DISCARD SHADOWS SHADOW_PCF
    BLINN_PHONG
)
# The velocity RT's presence must agree between the shaders and
# pipeline-builder.c: TAA specializations only exist when the build has
# TAA support at all, and can never vary per material within a pipeline
if (CONFIG_TAA)
    list(APPEND SHADER_VARIANT_AXES_model TAA)
endif ()
set(SHADER_VARIANT_AXES_shadow NORMALS SKINNING)
set(SHADER_VARIANT_AXES_shadow_vsm NORMALS SKINNING)

# Mask validity: <dependent>|<prerequisite> -- a mask with the dependent
# axis but not its prerequisite means nothing to the shader sources and
# gets neither a compile rule nor a lookup table row
set(SHADER_VARIANT_REQUIRES_model
    "SHADOW_PCF|SHADOWS"
)
set(SHADER_VARIANT_REQUIRES_shadow "")
set(SHADER_VARIANT_REQUIRES_shadow_vsm "")

# Canonical bit value of @axis
function(shader_variant_bit_value axis out_var)
    set(idx 0)
    foreach (rec ${SHADER_VARIANT_AXES})
        string(REPLACE "|" ";" f "${rec}")
        list(GET f 0 name)
        if (name STREQUAL "${axis}")
            math(EXPR val "1 << ${idx}")
            set(${out_var} ${val} PARENT_SCOPE)
            return ()
        endif ()
        math(EXPR idx "${idx} + 1")
    endforeach ()
    message(FATAL_ERROR "Unknown shader variant axis '${axis}'")
endfunction()

# GLSL define suffix of @axis (SHADER_VARIANT_<suffix>)
function(shader_variant_define axis out_var)
    foreach (rec ${SHADER_VARIANT_AXES})
        string(REPLACE "|" ";" f "${rec}")
        list(GET f 0 name)
        if (name STREQUAL "${axis}")
            list(GET f 1 def)
            set(${out_var} "${def}" PARENT_SCOPE)
            return ()
        endif ()
    endforeach ()
    message(FATAL_ERROR "Unknown shader variant axis '${axis}'")
endfunction()

# Canonical mask of an axis list
function(shader_variant_mask axes out_var)
    set(mask 0)
    foreach (axis ${axes})
        shader_variant_bit_value(${axis} bit)
        math(EXPR mask "${mask} | ${bit}")
    endforeach ()
    set(${out_var} ${mask} PARENT_SCOPE)
endfunction()

# Fixed-width lowercase hex suffix of @mask, as used in variant shader
# names ("model.0131") and the generated tables
function(shader_variant_suffix mask out_var)
    math(EXPR hex "${mask}" OUTPUT_FORMAT HEXADECIMAL)
    string(SUBSTRING "${hex}" 2 -1 hex)
    string(LENGTH "${hex}" len)
    math(EXPR pad "4 - ${len}")
    if (pad GREATER 0)
        string(REPEAT "0" ${pad} zeros)
        set(hex "${zeros}${hex}")
    endif ()
    set(${out_var} "${hex}" PARENT_SCOPE)
endfunction()

# The mask the plain "<family>" shader is compiled with: the global
# default axis set (SHADER_VARIANT_DEFAULTS, see compile-shaders.cmake)
# narrowed down to the axes @family responds to
function(shader_variant_default_mask family out_var)
    set(axes "")
    foreach (axis ${SHADER_VARIANT_DEFAULTS})
        if (axis IN_LIST SHADER_VARIANT_AXES_${family})
            list(APPEND axes ${axis})
        endif ()
    endforeach ()
    shader_variant_mask("${axes}" mask)
    set(${out_var} ${mask} PARENT_SCOPE)
endfunction()

# Enumerate every valid variant mask of @family, ascending
function(shader_variant_enumerate family out_var)
    set(axes ${SHADER_VARIANT_AXES_${family}})
    list(LENGTH axes nr_axes)
    math(EXPR last "(1 << ${nr_axes}) - 1")

    # cache per-axis canonical bit values and requires-rules as masks
    set(bits "")
    foreach (axis ${axes})
        shader_variant_bit_value(${axis} bit)
        list(APPEND bits ${bit})
    endforeach ()
    set(rules "")
    foreach (rule ${SHADER_VARIANT_REQUIRES_${family}})
        string(REPLACE "|" ";" f "${rule}")
        list(GET f 0 dep)
        list(GET f 1 pre)
        shader_variant_bit_value(${dep} dep_bit)
        shader_variant_bit_value(${pre} pre_bit)
        list(APPEND rules "${dep_bit}|${pre_bit}")
    endforeach ()

    set(masks "")
    foreach (fmask RANGE 0 ${last})
        set(mask 0)
        set(idx 0)
        foreach (bit ${bits})
            math(EXPR axis_set "(${fmask} >> ${idx}) & 1")
            if (axis_set)
                math(EXPR mask "${mask} | ${bit}")
            endif ()
            math(EXPR idx "${idx} + 1")
        endforeach ()

        set(valid TRUE)
        foreach (rule ${rules})
            string(REPLACE "|" ";" f "${rule}")
            list(GET f 0 dep_bit)
            list(GET f 1 pre_bit)
            math(EXPR has_dep "${mask} & ${dep_bit}")
            math(EXPR has_pre "${mask} & ${pre_bit}")
            if (has_dep AND NOT has_pre)
                set(valid FALSE)
                break ()
            endif ()
        endforeach ()

        if (valid)
            list(APPEND masks ${mask})
        endif ()
    endforeach ()

    set(${out_var} "${masks}" PARENT_SCOPE)
endfunction()

# glslc -D arguments for @family's axes present in @mask
function(shader_variant_glslc_args family mask out_var)
    set(args "")
    foreach (axis ${SHADER_VARIANT_AXES_${family}})
        shader_variant_bit_value(${axis} bit)
        math(EXPR axis_set "${mask} & ${bit}")
        if (axis_set)
            shader_variant_define(${axis} def)
            list(APPEND args -DSHADER_VARIANT_${def}=1)
        endif ()
    endforeach ()
    set(${out_var} "${args}" PARENT_SCOPE)
endfunction()

function(_shader_variants_write_if_different path content)
    set(old "")
    if (EXISTS "${path}")
        file(READ "${path}" old)
    endif ()
    if (NOT old STREQUAL content)
        file(WRITE "${path}" "${content}")
    endif ()
endfunction()

# Generate bindings/shader-variants.{h,c}: the SHADER_*_BIT enum and the
# per-family mask -> shader name tables behind shader_get_name().
# @output_dir:    directory for the generated files
# @with_variants: whether variant compiles are enabled; otherwise each
#                 family's table holds only its default (uber) row
function(shader_variants_emit output_dir with_variants)
    file(MAKE_DIRECTORY "${output_dir}")

    set(h "/* Generated by scripts/shader-variants.cmake. */\n")
    string(APPEND h "#ifndef __CLAP_SHADER_VARIANTS_GENERATED_H__\n")
    string(APPEND h "#define __CLAP_SHADER_VARIANTS_GENERATED_H__\n")
    string(APPEND h "\n")
    string(APPEND h "#include <stdint.h>\n")
    string(APPEND h "\n")
    string(APPEND h "/*\n")
    string(APPEND h " * Variant axes of the model/shadow uber shaders: bit i corresponds to\n")
    string(APPEND h " * compiling with -DSHADER_VARIANT_<axis>=1; a mask names the shader\n")
    string(APPEND h " * specialized for exactly that axis set (see shader_get_name())\n")
    string(APPEND h " */\n")
    string(APPEND h "typedef uint64_t shader_variants_t;\n")
    string(APPEND h "\n")
    set(idx 0)
    foreach (rec ${SHADER_VARIANT_AXES})
        string(REPLACE "|" ";" f "${rec}")
        list(GET f 0 name)
        list(GET f 1 def)
        set(line "static constexpr shader_variants_t SHADER_${name}_BIT = (shader_variants_t)1 << ${idx};")
        if (NOT name STREQUAL def)
            string(LENGTH "${line}" len)
            math(EXPR pad "80 - ${len}")
            if (pad LESS 1)
                set(pad 1)
            endif ()
            string(REPEAT " " ${pad} spaces)
            string(APPEND line "${spaces}/* SHADER_VARIANT_${def} */")
        endif ()
        string(APPEND h "${line}\n")
        math(EXPR idx "${idx} + 1")
    endforeach ()
    string(APPEND h "\n")
    string(APPEND h "#endif /* __CLAP_SHADER_VARIANTS_GENERATED_H__ */\n")

    set(c "/* Generated by scripts/shader-variants.cmake. */\n")
    string(APPEND c "/*\n")
    string(APPEND c " * Included by shader.c. One row per compiled specialization of each\n")
    string(APPEND c " * uber shader family; the default (uber) axis set maps to the plain\n")
    string(APPEND c " * base name. Rows are sorted by mask: shader_get_name() relies on\n")
    string(APPEND c " * that for binary search.\n")
    string(APPEND c " */\n")
    string(APPEND c "\n")
    string(APPEND c "struct shader_variant_name {\n")
    string(APPEND c "    shader_variants_t   mask;\n")
    string(APPEND c "    const char          *name;\n")
    string(APPEND c "};\n")
    string(APPEND c "\n")
    string(APPEND c "struct shader_variant_family {\n")
    string(APPEND c "    const char                          *base;\n")
    string(APPEND c "    const struct shader_variant_name    *names;\n")
    string(APPEND c "    unsigned int                        nr_names;\n")
    string(APPEND c "};\n")
    string(APPEND c "\n")
    string(APPEND c "/* axis names by bit position (shader_variants_str()) */\n")
    string(APPEND c "static const char *const shader_variant_bit_names[] = {\n")
    foreach (rec ${SHADER_VARIANT_AXES})
        string(REPLACE "|" ";" f "${rec}")
        list(GET f 0 name)
        string(APPEND c "    \"${name}\",\n")
    endforeach ()
    string(APPEND c "};\n")
    string(APPEND c "\n")

    foreach (family ${SHADER_VARIANT_FAMILIES})
        shader_variant_default_mask(${family} default_mask)

        if (with_variants)
            shader_variant_enumerate(${family} masks)
            # the default set is compiled whether or not it enumerates
            # (a barebones default could sit outside the requires-rules)
            list(APPEND masks ${default_mask})
            list(REMOVE_DUPLICATES masks)
            list(SORT masks COMPARE NATURAL)
        else ()
            set(masks ${default_mask})
        endif ()

        string(APPEND c "static const struct shader_variant_name shader_variant_names_${family}[] = {\n")
        foreach (mask ${masks})
            shader_variant_suffix(${mask} sfx)
            if (mask EQUAL default_mask)
                string(APPEND c "    { 0x${sfx}, \"${family}\" },\n")
            else ()
                string(APPEND c "    { 0x${sfx}, \"${family}.${sfx}\" },\n")
            endif ()
        endforeach ()
        string(APPEND c "};\n")
        string(APPEND c "\n")
    endforeach ()

    string(APPEND c "static const struct shader_variant_family shader_variant_families[] = {\n")
    foreach (family ${SHADER_VARIANT_FAMILIES})
        string(APPEND c "    {\n")
        string(APPEND c "        .base     = \"${family}\",\n")
        string(APPEND c "        .names    = shader_variant_names_${family},\n")
        string(APPEND c "        .nr_names = array_size(shader_variant_names_${family}),\n")
        string(APPEND c "    },\n")
    endforeach ()
    string(APPEND c "};\n")

    _shader_variants_write_if_different("${output_dir}/shader-variants.h" "${h}")
    _shader_variants_write_if_different("${output_dir}/shader-variants.c" "${c}")
endfunction()
