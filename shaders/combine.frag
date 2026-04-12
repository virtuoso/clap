#version 460 core

#include "shader_constants.h"
#include "half.glsl"
#include "tonemap.glsl"
#include "lut.glsl"
#include "oetf.glsl"
#include "smaa-neighborhood-blend.glsl"
#include "view_pos.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;
layout (binding=SAMPLER_BINDING_sobel_tex) uniform sampler2D sobel_tex;
layout (binding=SAMPLER_BINDING_depth_tex) uniform sampler2D depth_tex;
layout (binding=SAMPLER_BINDING_shadow_map) uniform sampler2D shadow_map;
layout (binding=SAMPLER_BINDING_shadow_map2) uniform sampler2D shadow_map2;
layout (binding=SAMPLER_BINDING_lut_tex) uniform sampler3D lut_tex;

#include "ubo_render_common.glsl"
#include "ubo_postproc.glsl"
#include "ubo_projview.glsl"

f16vec3 apply_contrast(f16vec3 color, float16_t contrast)
{
    return (color.rgb - H(0.5)) * (H(1.0) + contrast) + H(0.5);
}

float16_t radial_fog_factor(sampler2D tex, vec2 uv, float near_fog, float far_fog)
{
    f16vec3 view_pos = HVEC3(view_pos_from_depth(tex, inverse_proj, uv));
    if (view_pos.z > 0.0)   return H(0.0);
    float16_t dist = length(view_pos);
    return clamp((dist - H(near_fog)) / H(far_fog - near_fog), H(0.0), H(1.0));
}

void main()
{
    f16vec3 tex_color = use_edge_aa ?
        smaa_blend(model_tex, sobel_tex, shadow_map2, pass_tex) :
        apply_edge(model_tex, sobel_tex, H(1.0), pass_tex, ivec2(0));
    float16_t fog_factor = radial_fog_factor(depth_tex, pass_tex, fog_near, fog_far);
    f16vec3 highlight_color = HVEC3(texture(emission_map, pass_tex).rgb);
    float16_t ao = H(texture(shadow_map, pass_tex).r);

    if (use_ssao)
        tex_color = tex_color * mix(H(1.0), ao, H(ssao_weight));

    if (use_hdr) {
        /* lighting exposure + bloom exposure */
        f16vec3 hdr_color = tex_color * H(lighting_exposure) + highlight_color * (H(1.0) - fog_factor);
        /* fog */
        hdr_color = mix(hdr_color, HVEC3(fog_color), fog_factor);
        /* contrast */
        hdr_color = apply_contrast(hdr_color, H(contrast));
        /* color grading */
        hdr_color = HVEC3(apply_lut(lut_tex, hdr_color));
        /* tonemapping */
        if (hdr_output)
            hdr_color = HVEC3(hdr_display_map(hdr_color, hdr_white_nits, hdr_peak_nits, hdr_compress_knee, hdr_knee_softness));
        else
            hdr_color = mix(reinhard_tonemap(hdr_color), aces_tonemap(hdr_color), H(lighting_operator));
        float edge_blend = 1.0 - fog_factor;
        if (use_edge_aa) {
            vec4 blend = texture(shadow_map2, pass_tex);
            edge_blend = max(edge_blend - (blend.x + blend.y + blend.z + blend.w) * 0.125, 0.0);
        }
        hdr_color = apply_edge(hdr_color, sobel_tex, edge_blend, pass_tex, ivec2(0));
        FragColor = vec4(hdr_color, 1.0);
    } else {
        tex_color = mix(tex_color, HVEC3(fog_color), fog_factor);
        FragColor = vec4(tex_color + highlight_color * 2.0, 1.0);
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
