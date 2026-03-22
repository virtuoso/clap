#version 460 core

#include "config.h"
#include "shader_constants.h"
#include "tonemap.glsl"
#include "lut.glsl"
#include "oetf.glsl"
#include "smaa-neighborhood-blend.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;
layout (binding=SAMPLER_BINDING_sobel_tex) uniform sampler2D sobel_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2D normal_map;
layout (binding=SAMPLER_BINDING_shadow_map) uniform sampler2D shadow_map;
layout (binding=SAMPLER_BINDING_shadow_map2) uniform sampler2D shadow_map2;
layout (binding=SAMPLER_BINDING_lut_tex) uniform sampler3D lut_tex;

#include "ubo_render_common.glsl"
#include "ubo_bloom.glsl"
#include "ubo_postproc.glsl"

vec3 apply_contrast(vec3 color, float contrast)
{
    return (color.rgb - 0.5) * (1.0 + contrast) + 0.5;
}

float radial_fog_factor(sampler2D tex, vec2 uv, float near_fog, float far_fog)
{
    vec3 view_pos = texture(tex, uv, 0.0).rgb;
    float dist = length(view_pos);
    return clamp((dist - near_fog) / (far_fog - near_fog), 0.0, 1.0);
}

void main()
{
    vec3 tex_color = use_edge_aa ?
        smaa_blend(model_tex, sobel_tex, shadow_map2, pass_tex) :
        apply_edge(model_tex, sobel_tex, 1.0, pass_tex, ivec2(0));
    float fog_factor = radial_fog_factor(normal_map, pass_tex, fog_near, fog_far);
    vec3 highlight_color = texture(emission_map, pass_tex).rgb;
    float ao = texture(shadow_map, pass_tex).r;

    if (use_ssao)
        tex_color = tex_color * mix(1.0, ao, ssao_weight);

    if (use_hdr) {
        /* lighting exposure + bloom exposure */
        vec3 hdr_color = tex_color * lighting_exposure + highlight_color * (1.0 - fog_factor);
        /* fog */
        hdr_color = mix(hdr_color, fog_color, fog_factor);
        /* contrast */
        hdr_color = apply_contrast(hdr_color, contrast);
        /* color grading */
        hdr_color = apply_lut(lut_tex, hdr_color);
        /* tonemapping */
        if (hdr_output)
            hdr_color = hdr_display_map(hdr_color, hdr_white_nits, hdr_peak_nits, hdr_compress_knee, hdr_knee_softness);
        else
            hdr_color = mix(reinhard_tonemap(hdr_color), aces_tonemap(hdr_color), lighting_operator);
        float edge_blend = 1.0 - fog_factor;
        if (use_edge_aa) {
            vec4 blend = texture(shadow_map2, pass_tex);
            edge_blend = max(edge_blend - (blend.x + blend.y + blend.z + blend.w) * 0.125, 0.0);
        }
        hdr_color = apply_edge(hdr_color, sobel_tex, edge_blend, pass_tex, ivec2(0));
        FragColor = vec4(hdr_color, 1.0);
    } else {
        tex_color = mix(tex_color, fog_color, radial_fog_factor(normal_map, pass_tex, fog_near, fog_far));
        FragColor = vec4(tex_color + highlight_color * 2.0, 1.0);
    }

    if (hdr_output)
        FragColor.rgb = scene_linear_to_pq(FragColor.rgb, hdr_white_nits);
    else
        FragColor.rgb = scene_linear_to_srgb(FragColor.rgb);
}
