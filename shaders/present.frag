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

vec3 grain_color(vec3 color, vec3 view_pos)
{
    vec2 uv = vec2(
        fract(sin(film_grain_shift * 12.9898) * 43758.5453),
        fract(cos(film_grain_shift * 78.233)  * 12345.6789)
    );

    float dist = length(view_pos);
    vec3 noise = texture(grain_tex, (pass_tex + uv * dist) * FILM_GRAIN_SIZE).rgb;

    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    float weight = 1.0 - pow(luma, film_grain_power);
    return color + noise.rgb * weight * film_grain_factor;
}

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

    if (film_grain) {
        vec3 view_pos = view_pos_from_depth(depth_tex, inverse_proj, pass_tex);
        FragColor.rgb = grain_color(FragColor.rgb, view_pos);
    }

    if (hdr_output)
        // TODO: This should be a uniform: extended_p3/pq/extended_srgb
        // For now, this should be enough
#ifdef SHADER_RENDERER_WGPU
        FragColor.rgb = scene_linear_to_extended_p3(FragColor.rgb, hdr_white_nits);
#else /* !SHADER_RENDERER_WGPU */
        FragColor.rgb = scene_linear_to_pq(FragColor.rgb, hdr_white_nits);
#endif /* !SHADER_RENDERER_WGPU */
    else
        FragColor.rgb = scene_linear_to_srgb(FragColor.rgb);
}
