# render-bindings-gen.cmake -- Generate shader binding tables from render-bindings.json
#
# Reads core/render-bindings.json and produces:
#   render-bindings.h        -- enums: shader_vars, texture/ubo binding indices,
#                               renderer/binding_class enums, SHADER_BINDING_SLOT_MAX
#   render-bindings.c        -- shader_bindings[][][] lookup table, shader_var_desc[],
#                               shader_var_block_desc[]  (#include'd by shader.c)
#   render-bindings-common.h -- per-backend binding constants for all renderers
#   render-bindings-{gl,metal,wgpu}.h -- per-backend UBO_BINDING_*/SAMPLER_BINDING_* aliases
#
# Binding offset conventions (JSON bindings are renderer-agnostic, starting at 0):
#   OpenGL -- uses JSON values as-is (UBO binding points are a separate namespace)
#   Metal  -- UBOs += 1 (buffer index 0 is reserved for the vertex buffer)
#   WGPU   -- ignores JSON values; auto-assigns: textures get even slots (each
#             texture+sampler pair), UBOs follow sequentially after

if (NOT DEFINED input)
    message(FATAL_ERROR "render-bindings-gen.cmake requires -Dinput=<render-bindings.json>")
endif ()

if (NOT DEFINED output_dir)
    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/bindings")
endif ()

file(READ "${input}" json)
file(MAKE_DIRECTORY "${output_dir}")

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

macro(append_line _var _text)
    string(APPEND ${_var} "${_text}\n")
endmacro()

function(write_if_different path content)
    set(old "")
    if (EXISTS "${path}")
        file(READ "${path}" old)
    endif ()
    if (NOT old STREQUAL content)
        file(WRITE "${path}" "${content}")
    endif ()
endfunction()

# Thin wrappers around string(JSON ...) to reduce line noise.
function(json_get out_var json_text)
    string(JSON _v GET "${json_text}" ${ARGN})
    set(${out_var} "${_v}" PARENT_SCOPE)
endfunction()

function(json_get_optional out_var present_var json_text)
    string(JSON _v ERROR_VARIABLE _err GET "${json_text}" ${ARGN})
    if (_err)
        set(${out_var} "" PARENT_SCOPE)
        set(${present_var} FALSE PARENT_SCOPE)
    else ()
        set(${out_var} "${_v}" PARENT_SCOPE)
        set(${present_var} TRUE PARENT_SCOPE)
    endif ()
endfunction()

function(json_length out_var json_text)
    string(JSON _v LENGTH "${json_text}" ${ARGN})
    set(${out_var} "${_v}" PARENT_SCOPE)
endfunction()

# Build a |-delimited "record" list from a JSON array, for iteration.
# Each record is: original_index|name[|extra fields from extra_keys...].
# The list is sorted by the JSON "binding" field if present, falling back
# to original array order for entries that share a binding value.
function(collect_records out_var json_text array_name name_key)
    set(extra_keys ${ARGN})
    set(records "")

    json_length(count "${json_text}" ${array_name})
    math(EXPR last "${count} - 1")
    if (last LESS 0)
        set(${out_var} "" PARENT_SCOPE)
        return ()
    endif ()

    foreach (i RANGE 0 ${last})
        json_get(name "${json_text}" ${array_name} ${i} ${name_key})
        set(record "${i}|${name}")
        foreach (key ${extra_keys})
            json_get(val "${json_text}" ${array_name} ${i} ${key})
            string(APPEND record "|${val}")
        endforeach ()
        list(APPEND records "${record}")
    endforeach ()

    set(${out_var} "${records}" PARENT_SCOPE)
endfunction()

# Convert a UBO's "stages" array (e.g. ["vertex","fragment"]) to a C bitmask expression.
function(stages_to_bitmask out_var ubo_idx)
    json_length(count "${json}" ubos ${ubo_idx} stages)
    math(EXPR last "${count} - 1")
    set(bits "")
    foreach (i RANGE 0 ${last})
        json_get(stage "${json}" ubos ${ubo_idx} stages ${i})
        if (stage STREQUAL "vertex")
            list(APPEND bits "SHADER_STAGE_VERTEX_BIT")
        elseif (stage STREQUAL "fragment")
            list(APPEND bits "SHADER_STAGE_FRAGMENT_BIT")
        elseif (stage STREQUAL "geometry")
            list(APPEND bits "SHADER_STAGE_GEOMETRY_BIT")
        else ()
            message(FATAL_ERROR "Unknown shader stage '${stage}' in ${input}")
        endif ()
    endforeach ()
    if (NOT bits)
        message(FATAL_ERROR "UBO at index ${ubo_idx} has no stages in ${input}")
    endif ()
    string(JOIN " | " expr ${bits})
    set(${out_var} "${expr}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Collect records from JSON
# ---------------------------------------------------------------------------

# attribute_records: idx|name
# Attributes follow JSON array order -- must match ATTR_LOC_* in shader_constants.h.
collect_records(attribute_records "${json}" vertex_attributes name)

# texture_records: idx|name|symbol
collect_records(texture_records "${json}" textures name symbol)

# ubo_records: idx|name
collect_records(ubo_records "${json}" ubos name)

list(LENGTH texture_records texture_count)
list(LENGTH ubo_records ubo_count)
if (texture_count GREATER ubo_count)
    set(binding_slot_count ${texture_count})
else ()
    set(binding_slot_count ${ubo_count})
endif ()
if (binding_slot_count EQUAL 0)
    set(binding_slot_count 1)
endif ()

set(backends OPENGL METAL WGPU)

# ---------------------------------------------------------------------------
# render-bindings-common.h -- flat #define constants for every backend
# ---------------------------------------------------------------------------
# Each backend gets its own set of BACKEND_BINDING_{TEXTURE,UBO}_name defines.

function(emit_common_header out_var)
    set(c "")

    foreach (backend ${backends})
        set(label_OPENGL "OpenGL")
        set(label_METAL  "Metal")
        set(label_WGPU   "WGPU")
        append_line(c "/* ${label_${backend}} bindings */")

        # Texture bindings: all backends use the JSON binding value directly,
        # except WGPU which auto-assigns even-numbered slots (texture+sampler pairs).
        if (backend STREQUAL "WGPU")
            set(slot 0)
        endif ()
        foreach (record ${texture_records})
            string(REPLACE "|" ";" f "${record}")
            list(GET f 0 idx)
            list(GET f 1 name)
            if (backend STREQUAL "WGPU")
                set(binding ${slot})
                math(EXPR slot "${slot} + 2")
            else ()
                json_get(binding "${json}" textures ${idx} binding)
            endif ()
            append_line(c "#define ${backend}_BINDING_TEXTURE_${name} ${binding}")
        endforeach ()

        # UBO bindings:
        #   OpenGL -- straight from JSON (separate binding-point namespace)
        #   Metal  -- JSON + 1 (buffer index 0 = vertex data)
        #   WGPU   -- sequential, continuing after the last texture slot
        foreach (record ${ubo_records})
            string(REPLACE "|" ";" f "${record}")
            list(GET f 0 idx)
            list(GET f 1 name)
            if (backend STREQUAL "WGPU")
                set(binding ${slot})
                math(EXPR slot "${slot} + 1")
            else ()
                json_get(binding "${json}" ubos ${idx} binding)
                if (backend STREQUAL "METAL")
                    math(EXPR binding "${binding} + 1")
                endif ()
            endif ()
            append_line(c "#define ${backend}_BINDING_UBO_${name} ${binding}")
        endforeach ()

        append_line(c "")
    endforeach ()

    set(${out_var} "${c}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# render-bindings-{gl,metal,wgpu}.h -- backend-specific alias headers
# ---------------------------------------------------------------------------
# These are included from shader_constants.h via #if CONFIG_RENDERER_*.
# They alias UBO_BINDING_foo / SAMPLER_BINDING_foo to the backend-specific
# constants from render-bindings-common.h, so that shaders and C code can
# use backend-agnostic names.

function(emit_backend_header out_var guard backend)
    set(c "/* Generated by scripts/render-bindings-gen.cmake. */\n")
    append_line(c "#ifndef ${guard}")
    append_line(c "#define ${guard}")
    append_line(c "")
    append_line(c "#include \"render-bindings-common.h\"")
    append_line(c "")
    append_line(c "/* UBO binding locations */")
    foreach (record ${ubo_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 1 name)
        append_line(c "#define UBO_BINDING_${name} ${backend}_BINDING_UBO_${name}")
    endforeach ()
    append_line(c "")
    append_line(c "/* Texture binding locations */")
    foreach (record ${texture_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 1 name)
        append_line(c "#define SAMPLER_BINDING_${name} ${backend}_BINDING_TEXTURE_${name}")
    endforeach ()
    append_line(c "")
    append_line(c "#endif /* ${guard} */")
    set(${out_var} "${c}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# render-bindings.h -- enums used by shader.c and the rest of the engine
# ---------------------------------------------------------------------------

function(emit_header out_var)
    set(c "/* Generated by scripts/render-bindings-gen.cmake. */\n")
    append_line(c "#ifndef __CLAP_RENDER_BINDINGS_GENERATED_H__")
    append_line(c "#define __CLAP_RENDER_BINDINGS_GENERATED_H__")
    append_line(c "")

    # enum shader_vars: contiguous enum covering attributes, texture uniforms,
    # and all per-UBO uniforms -- used as indices into shader_var_desc[].
    append_line(c "enum shader_vars {")
    append_line(c "    /* vertex attributes */")
    set(first TRUE)
    foreach (record ${attribute_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 1 name)
        string(TOUPPER "${name}" upper)
        if (first)
            append_line(c "    ATTR_${upper} = 0,")
            set(first FALSE)
        else ()
            append_line(c "    ATTR_${upper},")
        endif ()
    endforeach ()
    append_line(c "    ATTR_MAX,")

    append_line(c "    /* texture uniforms */")
    set(first TRUE)
    foreach (record ${texture_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 2 symbol)
        if (first)
            append_line(c "    ${symbol} = ATTR_MAX,")
            set(first FALSE)
        else ()
            append_line(c "    ${symbol},")
        endif ()
    endforeach ()
    append_line(c "    UNIFORM_TEX_MAX,")
    append_line(c "    UNIFORM_NR_TEX = UNIFORM_TEX_MAX - ATTR_MAX,")

    set(first TRUE)
    foreach (record ${ubo_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 0 ubo_idx)
        list(GET f 1 ubo_name)
        append_line(c "    /* \"${ubo_name}\" uniform buffer */")
        json_length(u_count "${json}" ubos ${ubo_idx} uniforms)
        math(EXPR u_last "${u_count} - 1")
        foreach (u RANGE 0 ${u_last})
            json_get(sym "${json}" ubos ${ubo_idx} uniforms ${u} symbol)
            if (first)
                append_line(c "    ${sym} = UNIFORM_TEX_MAX,")
                set(first FALSE)
            else ()
                append_line(c "    ${sym},")
            endif ()
        endforeach ()
    endforeach ()
    append_line(c "    SHADER_VAR_MAX")
    append_line(c "};")
    append_line(c "")

    # enum shader_texture_bindings: abstract texture slot indices (0..N-1),
    # mapped to actual backend binding points via shader_bindings[][][].
    append_line(c "enum shader_texture_bindings {")
    set(first TRUE)
    foreach (record ${texture_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 1 name)
        if (first)
            append_line(c "    BINDING_TEXTURE_${name} = 0,")
            set(first FALSE)
        else ()
            append_line(c "    BINDING_TEXTURE_${name},")
        endif ()
    endforeach ()
    append_line(c "    BINDING_TEXTURE_MAX")
    append_line(c "};")
    append_line(c "")

    # enum shader_ubo_bindings: abstract UBO slot indices (0..N-1).
    append_line(c "enum shader_ubo_bindings {")
    set(first TRUE)
    foreach (record ${ubo_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 1 name)
        if (first)
            append_line(c "    BINDING_UBO_${name} = 0,")
            set(first FALSE)
        else ()
            append_line(c "    BINDING_UBO_${name},")
        endif ()
    endforeach ()
    append_line(c "    BINDING_UBO_MAX")
    append_line(c "};")
    append_line(c "")

    append_line(c "enum shader_binding_renderers {")
    append_line(c "#ifdef CONFIG_RENDERER_OPENGL")
    append_line(c "    RENDERER_OPENGL,")
    append_line(c "#endif")
    append_line(c "#ifdef CONFIG_RENDERER_METAL")
    append_line(c "    RENDERER_METAL,")
    append_line(c "#endif")
    append_line(c "#ifdef CONFIG_RENDERER_WGPU")
    append_line(c "    RENDERER_WGPU,")
    append_line(c "#endif")
    append_line(c "    RENDERER_MAX")
    append_line(c "};")
    append_line(c "")
    append_line(c "enum shader_binding_classes {")
    append_line(c "    BC_UBO = 0,")
    append_line(c "    BC_TEXTURE,")
    append_line(c "    BC_MAX")
    append_line(c "};")
    append_line(c "")
    append_line(c "static constexpr int SHADER_BINDING_SLOT_MAX = ${binding_slot_count};")
    append_line(c "")
    append_line(c "#endif /* __CLAP_RENDER_BINDINGS_GENERATED_H__ */")

    set(${out_var} "${c}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# render-bindings.c -- data tables (#include'd by shader.c)
# ---------------------------------------------------------------------------
# Emits:
#   shader_bindings[renderer][binding_class][slot] -- maps abstract slot indices
#       to actual backend binding points at runtime
#   shader_var_desc[] -- per-variable metadata (name, type, texture_slot, elem_count)
#   shader_var_block_desc[] -- per-UBO metadata (name, binding, stages, member list)

function(emit_source out_var)
    set(c "/* Generated by scripts/render-bindings-gen.cmake. */\n")
    append_line(c "/*")
    append_line(c " * Requires the following in scope where included:")
    append_line(c " * - enum shader_vars")
    append_line(c " * - enum shader_texture_bindings / enum shader_ubo_bindings")
    append_line(c " * - struct shader_var_desc")
    append_line(c " * - struct shader_var_block_desc")
    append_line(c " */")
    append_line(c "")
    append_line(c "#include \"render-bindings-common.h\"")
    append_line(c "")

    # shader_bindings[][][] -- abstract-to-backend binding lookup table
    append_line(c "static const int shader_bindings[RENDERER_MAX][BC_MAX][SHADER_BINDING_SLOT_MAX] = {")
    foreach (backend OPENGL METAL WGPU)
        append_line(c "#ifdef CONFIG_RENDERER_${backend}")
        append_line(c "    [RENDERER_${backend}] = {")
        append_line(c "        [BC_TEXTURE] = {")
        foreach (record ${texture_records})
            string(REPLACE "|" ";" f "${record}")
            list(GET f 1 name)
            append_line(c "            [BINDING_TEXTURE_${name}] = ${backend}_BINDING_TEXTURE_${name},")
        endforeach ()
        append_line(c "        },")
        append_line(c "        [BC_UBO] = {")
        foreach (record ${ubo_records})
            string(REPLACE "|" ";" f "${record}")
            list(GET f 1 name)
            append_line(c "            [BINDING_UBO_${name}] = ${backend}_BINDING_UBO_${name},")
        endforeach ()
        append_line(c "        },")
        append_line(c "    },")
        append_line(c "#endif")
    endforeach ()
    append_line(c "};")
    append_line(c "")

    # shader_var_desc[] -- flat array indexed by enum shader_vars
    append_line(c "static const struct shader_var_desc shader_var_desc[] = {")
    append_line(c "    /* vertex attributes */")
    foreach (record ${attribute_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 0 idx)
        list(GET f 1 name)
        string(TOUPPER "${name}" upper)
        json_get(type "${json}" vertex_attributes ${idx} type)
        append_line(c "    [ATTR_${upper}] = { .name = \"${name}\", .type = ${type}, .texture_slot = -1, .elem_count = 1 },")
    endforeach ()

    append_line(c "    /* texture bindings */")
    foreach (record ${texture_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 1 name)
        list(GET f 2 symbol)
        append_line(c "    [${symbol}] = { .name = \"${name}\", .type = DT_INT, .texture_slot = BINDING_TEXTURE_${name}, .elem_count = 1 },")
    endforeach ()

    foreach (record ${ubo_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 0 ubo_idx)
        list(GET f 1 ubo_name)
        append_line(c "    /* \"${ubo_name}\" uniform buffer */")
        json_length(u_count "${json}" ubos ${ubo_idx} uniforms)
        math(EXPR u_last "${u_count} - 1")
        foreach (u RANGE 0 ${u_last})
            json_get(u_name "${json}" ubos ${ubo_idx} uniforms ${u} name)
            json_get(u_type "${json}" ubos ${ubo_idx} uniforms ${u} type)
            json_get(u_sym  "${json}" ubos ${ubo_idx} uniforms ${u} symbol)
            json_get_optional(u_arr has_arr "${json}" ubos ${ubo_idx} uniforms ${u} array_size)
            if (has_arr)
                set(elem "${u_arr}")
            else ()
                set(elem 1)
            endif ()
            append_line(c "    [${u_sym}] = { .name = \"${u_name}\", .type = ${u_type}, .texture_slot = -1, .elem_count = ${elem} },")
        endforeach ()
    endforeach ()
    append_line(c "};")
    append_line(c "")

    # shader_var_block_desc[] -- UBO descriptors indexed by BINDING_UBO_*
    append_line(c "static const struct shader_var_block_desc shader_var_block_desc[] = {")
    foreach (record ${ubo_records})
        string(REPLACE "|" ";" f "${record}")
        list(GET f 0 ubo_idx)
        list(GET f 1 ubo_name)
        stages_to_bitmask(stages "${ubo_idx}")
        append_line(c "    [BINDING_UBO_${ubo_name}] = {")
        append_line(c "        .name = \"${ubo_name}\",")
        append_line(c "        .binding = BINDING_UBO_${ubo_name},")
        append_line(c "        .stages = ${stages},")
        append_line(c "        .vars = (enum shader_vars[]){")
        json_length(u_count "${json}" ubos ${ubo_idx} uniforms)
        math(EXPR u_last "${u_count} - 1")
        foreach (u RANGE 0 ${u_last})
            json_get(u_sym "${json}" ubos ${ubo_idx} uniforms ${u} symbol)
            append_line(c "            ${u_sym},")
        endforeach ()
        append_line(c "            SHADER_VAR_MAX")
        append_line(c "        },")
        append_line(c "    },")
    endforeach ()
    append_line(c "};")

    set(${out_var} "${c}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Generate and write output files
# ---------------------------------------------------------------------------

emit_header(header_content)
emit_source(source_content)
emit_common_header(common_content)

set(common_header "/* Generated by scripts/render-bindings-gen.cmake. */\n")
append_line(common_header "#ifndef CLAP_RENDER_BINDINGS_COMMON_GENERATED_H")
append_line(common_header "#define CLAP_RENDER_BINDINGS_COMMON_GENERATED_H")
append_line(common_header "")
string(APPEND common_header "${common_content}")
append_line(common_header "#endif /* CLAP_RENDER_BINDINGS_COMMON_GENERATED_H */")

emit_backend_header(gl_header    "CLAP_RENDER_BINDINGS_GL_GENERATED_H"    "OPENGL")
emit_backend_header(metal_header "CLAP_RENDER_BINDINGS_METAL_GENERATED_H" "METAL")
emit_backend_header(wgpu_header  "CLAP_RENDER_BINDINGS_WGPU_GENERATED_H"  "WGPU")

write_if_different("${output_dir}/render-bindings.h"        "${header_content}")
write_if_different("${output_dir}/render-bindings.c"        "${source_content}")
write_if_different("${output_dir}/render-bindings-common.h" "${common_header}")
write_if_different("${output_dir}/render-bindings-gl.h"     "${gl_header}")
write_if_different("${output_dir}/render-bindings-metal.h"  "${metal_header}")
write_if_different("${output_dir}/render-bindings-wgpu.h"   "${wgpu_header}")
