#version 460 core

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
layout (binding=SAMPLER_BINDING_volumetric_tex) uniform sampler2D volumetric_tex;

#include "ubo_render_common.glsl"
#include "ubo_postproc.glsl"
#include "ubo_projview.glsl"
#include "ubo_bloom.glsl"

#include "noise.glsl"

#include "combine-common.glsl"

void main()
{
    FragColor = combine_color(pass_tex);
}
