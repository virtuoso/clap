// SPDX-License-Identifier: Apache-2.0
//
// FFI over the context's render options: the post-processing and debug-draw
// knobs an agent tweaks between screenshots (fog, bloom, contrast, SSAO, edge
// and shadow settings), plus selecting a colour-grading LUT by name. The single
// render_options lives on the context, so it is handed to scripts as one
// RenderOptions handle obtained from clap::render(); the field accessors are
// generated from the markers in ffi.h. Changes are picked up on the next frame
// by clap_update_render_options().

#include "clap.h"
#include "pipeline.h"
#include "script/ffi.h"

CLAP_SCRIPT_TYPE("type RenderOptions* = struct { _h: uint }")

/* The context's render options, as a handle to hang the knobs below off. */
CLAP_SCRIPT_FN(ffi_render, "fn render*(): RenderOptions")
void ffi_render(UmkaStackSlot *params, UmkaStackSlot *result)
{
    ffi_ret_ptr(params, result, clap_get_render_options(ffi_ctx(result)));
}

/* Select the colour-grading LUT by name; false if no LUT by that name exists. */
CLAP_SCRIPT_FN(ffi_set_lut, "fn set_lut*(name: str): bool")
void ffi_set_lut(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const char *name = ffi_str(params, 0);
    cerr err = clap_set_lighting_lut(ffi_ctx(result), name ? name : "");
    ffi_ret_bool(params, result, !IS_CERR(err));
}

/* Fog */
FFI_GET_FLOAT(ffi_render_fog_near, "fn (ro: RenderOptions) fog_near*(): real", render_options, fog_near)
FFI_SET_FLOAT(ffi_render_set_fog_near, "fn (ro: RenderOptions) set_fog_near*(v: real)", render_options, fog_near)
FFI_GET_FLOAT(ffi_render_fog_far, "fn (ro: RenderOptions) fog_far*(): real", render_options, fog_far)
FFI_SET_FLOAT(ffi_render_set_fog_far, "fn (ro: RenderOptions) set_fog_far*(v: real)", render_options, fog_far)
FFI_GET_VEC3(ffi_render_fog_color, "fn (ro: RenderOptions) fog_color*(): Vec3", render_options, fog_color)
FFI_SET_VEC3(ffi_render_set_fog_color, "fn (ro: RenderOptions) set_fog_color*(r, g, b: real)",
             render_options, fog_color)

/* Bloom */
FFI_GET_FLOAT(ffi_render_bloom_intensity, "fn (ro: RenderOptions) bloom_intensity*(): real",
              render_options, bloom_intensity)
FFI_SET_FLOAT(ffi_render_set_bloom_intensity, "fn (ro: RenderOptions) set_bloom_intensity*(v: real)",
              render_options, bloom_intensity)
FFI_GET_FLOAT(ffi_render_bloom_exposure, "fn (ro: RenderOptions) bloom_exposure*(): real",
              render_options, bloom_exposure)
FFI_SET_FLOAT(ffi_render_set_bloom_exposure, "fn (ro: RenderOptions) set_bloom_exposure*(v: real)",
              render_options, bloom_exposure)
FFI_GET_FLOAT(ffi_render_bloom_threshold, "fn (ro: RenderOptions) bloom_threshold*(): real",
              render_options, bloom_threshold)
FFI_SET_FLOAT(ffi_render_set_bloom_threshold, "fn (ro: RenderOptions) set_bloom_threshold*(v: real)",
              render_options, bloom_threshold)

/* Tone / colour */
FFI_GET_FLOAT(ffi_render_contrast, "fn (ro: RenderOptions) contrast*(): real", render_options, contrast)
FFI_SET_FLOAT(ffi_render_set_contrast, "fn (ro: RenderOptions) set_contrast*(v: real)", render_options, contrast)
FFI_GET_FLOAT(ffi_render_lighting_exposure, "fn (ro: RenderOptions) lighting_exposure*(): real",
              render_options, lighting_exposure)
FFI_SET_FLOAT(ffi_render_set_lighting_exposure, "fn (ro: RenderOptions) set_lighting_exposure*(v: real)",
              render_options, lighting_exposure)
FFI_GET_FLOAT(ffi_render_lighting_operator, "fn (ro: RenderOptions) lighting_operator*(): real",
              render_options, lighting_operator)
FFI_SET_FLOAT(ffi_render_set_lighting_operator, "fn (ro: RenderOptions) set_lighting_operator*(v: real)",
              render_options, lighting_operator)

/* SSAO */
FFI_GET_BOOL(ffi_render_ssao, "fn (ro: RenderOptions) ssao*(): bool", render_options, ssao)
FFI_SET_BOOL(ffi_render_set_ssao, "fn (ro: RenderOptions) set_ssao*(v: bool)", render_options, ssao)
FFI_GET_FLOAT(ffi_render_ssao_radius, "fn (ro: RenderOptions) ssao_radius*(): real", render_options, ssao_radius)
FFI_SET_FLOAT(ffi_render_set_ssao_radius, "fn (ro: RenderOptions) set_ssao_radius*(v: real)",
              render_options, ssao_radius)
FFI_GET_FLOAT(ffi_render_ssao_weight, "fn (ro: RenderOptions) ssao_weight*(): real", render_options, ssao_weight)
FFI_SET_FLOAT(ffi_render_set_ssao_weight, "fn (ro: RenderOptions) set_ssao_weight*(v: real)",
              render_options, ssao_weight)

/* Edge / outline */
FFI_GET_BOOL(ffi_render_edge_sobel, "fn (ro: RenderOptions) edge_sobel*(): bool", render_options, edge_sobel)
FFI_SET_BOOL(ffi_render_set_edge_sobel, "fn (ro: RenderOptions) set_edge_sobel*(v: bool)", render_options, edge_sobel)
FFI_GET_BOOL(ffi_render_edge_antialiasing, "fn (ro: RenderOptions) edge_antialiasing*(): bool",
             render_options, edge_antialiasing)
FFI_SET_BOOL(ffi_render_set_edge_antialiasing, "fn (ro: RenderOptions) set_edge_antialiasing*(v: bool)",
             render_options, edge_antialiasing)

/* Shadows */
FFI_GET_BOOL(ffi_render_shadow_vsm, "fn (ro: RenderOptions) shadow_vsm*(): bool", render_options, shadow_vsm)
FFI_SET_BOOL(ffi_render_set_shadow_vsm, "fn (ro: RenderOptions) set_shadow_vsm*(v: bool)", render_options, shadow_vsm)
FFI_GET_BOOL(ffi_render_shadow_msaa, "fn (ro: RenderOptions) shadow_msaa*(): bool", render_options, shadow_msaa)
FFI_SET_BOOL(ffi_render_set_shadow_msaa, "fn (ro: RenderOptions) set_shadow_msaa*(v: bool)",
             render_options, shadow_msaa)
FFI_GET_BOOL(ffi_render_shadow_outline, "fn (ro: RenderOptions) shadow_outline*(): bool",
             render_options, shadow_outline)
FFI_SET_BOOL(ffi_render_set_shadow_outline, "fn (ro: RenderOptions) set_shadow_outline*(v: bool)",
             render_options, shadow_outline)
FFI_GET_FLOAT(ffi_render_shadow_outline_threshold, "fn (ro: RenderOptions) shadow_outline_threshold*(): real",
              render_options, shadow_outline_threshold)
FFI_SET_FLOAT(ffi_render_set_shadow_outline_threshold, "fn (ro: RenderOptions) set_shadow_outline_threshold*(v: real)",
              render_options, shadow_outline_threshold)

/* Debug overlays */
FFI_GET_BOOL(ffi_render_aabb_draws, "fn (ro: RenderOptions) aabb_draws*(): bool", render_options, aabb_draws_enabled)
FFI_SET_BOOL(ffi_render_set_aabb_draws, "fn (ro: RenderOptions) set_aabb_draws*(v: bool)",
             render_options, aabb_draws_enabled)
FFI_GET_BOOL(ffi_render_light_draws, "fn (ro: RenderOptions) light_draws*(): bool", render_options, light_draws_enabled)
FFI_SET_BOOL(ffi_render_set_light_draws, "fn (ro: RenderOptions) set_light_draws*(v: bool)",
             render_options, light_draws_enabled)
