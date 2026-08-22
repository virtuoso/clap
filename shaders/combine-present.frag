#version 460 core

/*
 * combine + present merged into a single screen pass: valid only when
 * neither TAA nor SMAA runs between the composite and the screen (the
 * pipeline builder picks this shader in that case), saving a full-res
 * RT write + read + quad churn.
 */

#include "shader_constants.h"
#include "half.glsl"
#include "color-utils.glsl"
#include "bloom-level.glsl"
#include "pass-tex.glsl"
#include "tonemap.glsl"
#include "lut.glsl"
#include "smaa-neighborhood-blend.glsl"
#include "view_pos.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;
layout (binding=SAMPLER_BINDING_bloom_tex) uniform sampler2D bloom_tex;
layout (binding=SAMPLER_BINDING_bloom_half_tex) uniform sampler2D bloom_half_tex;
layout (binding=SAMPLER_BINDING_sobel_tex) uniform sampler2D sobel_tex;
layout (binding=SAMPLER_BINDING_depth_tex) uniform sampler2D depth_tex;
layout (binding=SAMPLER_BINDING_shadow_map) uniform sampler2D shadow_map;
layout (binding=SAMPLER_BINDING_lut_tex) uniform sampler3D lut_tex;
layout (binding=SAMPLER_BINDING_noise3d) uniform sampler3D noise3d;
layout (binding=SAMPLER_BINDING_grain_tex) uniform sampler2D grain_tex;

#include "ubo_render_common.glsl"
#include "ubo_postproc.glsl"
#include "ubo_projview.glsl"
#include "ubo_bloom.glsl"
#include "ubo_frame.glsl"

#include "noise.glsl"

#include "combine-common.glsl"
#include "present-common.glsl"

void main()
{
    vec4 color = combine_color(pass_tex);
    FragColor = vec4(present_finalize(color.rgb, pass_tex), color.a);
}
