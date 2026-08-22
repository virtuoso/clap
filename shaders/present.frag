#version 460 core

#include "shader_constants.h"
#include "half.glsl"
#include "smaa-neighborhood-blend.glsl"
#include "oetf.glsl"
#include "view_pos.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_depth_tex) uniform sampler2D depth_tex;
layout (binding=SAMPLER_BINDING_grain_tex) uniform sampler2D grain_tex;
layout (binding=SAMPLER_BINDING_sobel_tex) uniform sampler2D sobel_tex;
layout (binding=SAMPLER_BINDING_shadow_map2) uniform sampler2D shadow_map2;

#include "ubo_render_common.glsl"
#include "ubo_postproc.glsl"
#include "ubo_projview.glsl"
#include "ubo_frame.glsl"

#include "present-common.glsl"

void main()
{
    /*
     * SMAA neighborhood blend belongs on the final composite, where
     * the neighbors it blends from have actually been computed --
     * combine can't sample its own in-flight output. Mutually
     * exclusive with TAA, which wants no extra blending on top.
     */
    if (use_edge_aa)
        FragColor = vec4(smaa_blend(model_tex, sobel_tex, shadow_map2, pass_tex), 1.0);
    else
        FragColor = vec4(texture(model_tex, pass_tex).rgb, 1.0);

    FragColor.rgb = present_finalize(FragColor.rgb, pass_tex);
}
