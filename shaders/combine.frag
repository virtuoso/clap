#version 460 core

#include "shader_constants.h"
#include "half.glsl"
#include "tonemap.glsl"
#include "lut.glsl"
#include "smaa-neighborhood-blend.glsl"
#include "view_pos.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;
layout (binding=SAMPLER_BINDING_sobel_tex) uniform sampler2D sobel_tex;
layout (binding=SAMPLER_BINDING_depth_tex) uniform sampler2D depth_tex;
layout (binding=SAMPLER_BINDING_shadow_map) uniform sampler2D shadow_map;
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

void main()
{
    f16vec3 tex_color = HVEC3(texture(model_tex, pass_tex).rgb);
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
        /*
         * The one edge application, at the composite, fading with fog;
         * SMAA (use_edge_aa) applies edges in present.frag instead,
         * where the neighbors it blends from have been computed
         */
        if (!use_edge_aa)
            hdr_color = apply_edge(hdr_color, sobel_tex, 1.0 - fog_factor, pass_tex, ivec2(0));
        FragColor = vec4(hdr_color, 1.0);
    } else {
        tex_color = mix(tex_color, HVEC3(radial_fog_color(view_pos)), fog_factor);
        if (!use_edge_aa)
            tex_color = apply_edge(tex_color, sobel_tex, 1.0 - fog_factor, pass_tex, ivec2(0));
        FragColor = vec4(tex_color + highlight_color * 2.0, 1.0);
    }
}
