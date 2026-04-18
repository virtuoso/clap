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
layout (binding=SAMPLER_BINDING_grain_tex) uniform sampler2D grain_tex;
layout (binding=SAMPLER_BINDING_lut_tex) uniform sampler3D lut_tex;
layout (binding=SAMPLER_BINDING_noise3d) uniform sampler3D noise3d;

#include "ubo_render_common.glsl"
#include "ubo_postproc.glsl"
#include "ubo_projview.glsl"

#include "noise.glsl"

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

vec3 radial_fog_color(vec3 view_pos)
{
    vec3 noise_vec = sample_noise3d(noise3d, view_pos + vec3(noise(view_pos)), 0.05) * 0.05;
    float n = min(dot(noise_vec, noise_vec), 3.0) / 3.0;
    return fog_color * (1.0 - n);
}

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
    f16vec3 tex_color = use_edge_aa ?
        smaa_blend(model_tex, sobel_tex, shadow_map2, pass_tex) :
        apply_edge(model_tex, sobel_tex, H(1.0), pass_tex, ivec2(0));
    float16_t fog_factor = radial_fog_factor(depth_tex, pass_tex, fog_near, fog_far);
    f16vec3 highlight_color = HVEC3(texture(emission_map, pass_tex).rgb);
    float16_t ao = H(texture(shadow_map, pass_tex).r);

    if (use_ssao)
        tex_color = tex_color * mix(H(1.0), ao, H(ssao_weight));

    vec3 view_pos = view_pos_from_depth(depth_tex, inverse_proj, pass_tex);

    if (use_hdr) {
        /* lighting exposure + bloom exposure */
        f16vec3 hdr_color = tex_color * H(lighting_exposure) + highlight_color * (H(1.0) - fog_factor);
        /* fog */
        hdr_color = mix(hdr_color, HVEC3(radial_fog_color(view_pos)), fog_factor);
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
        tex_color = mix(tex_color, HVEC3(radial_fog_color(view_pos)), fog_factor);
        FragColor = vec4(tex_color + highlight_color * 2.0, 1.0);
    }

    if (film_grain)
        FragColor = vec4(grain_color(FragColor.rgb, view_pos), 1.0);

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
