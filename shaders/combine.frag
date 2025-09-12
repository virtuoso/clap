#version 460 core

#include "config.h"
#include "shader_constants.h"
#include "tonemap.glsl"
#include "lut.glsl"
#include "oetf.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;
layout (binding=SAMPLER_BINDING_sobel_tex) uniform sampler2D sobel_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2D normal_map;
layout (binding=SAMPLER_BINDING_shadow_map) uniform sampler2D shadow_map;
layout (binding=SAMPLER_BINDING_lut_tex) uniform sampler3D lut_tex;
layout (binding=SAMPLER_BINDING_grain_tex) uniform sampler2D grain_tex;

#include "ubo_render_common.glsl"
#include "ubo_bloom.glsl"
#include "ubo_postproc.glsl"
#include "noise.glsl"

float radial_fog_factor(vec3 view_pos, float near_fog, float far_fog)
{
    float dist = length(view_pos);
    return clamp((dist - near_fog) / (far_fog - near_fog), 0.0, 1.0);
}

vec3 radial_fog_color(vec3 view_pos)
{
    vec3 noise_vec = sample_noise3d(view_pos + noise(view_pos), 0.05) * 0.05; // XXX: parameterize
    float noise = min(length(noise_vec), 1.0);
    return fog_color * (1.0 - noise);
}

float lens_dirt_factor(vec2 uv)
{
    return 1.0;
    // uv = uv * 2.0 - 1.0;
    float rsq = smoothstep(0.0, 1.0, length(uv));//dot(uv, uv);
    vec3 src = vec3(uv * 2.5, rsq);
    vec3 noise_vec = sample_noise3d(src, 0.125);
    // vec3 noise_vec = sample_noise3d(src + noise(src * 2.5), 0.125);
    return clamp(1.0 - smoothstep(0.0, 0.5, length(noise_vec)) * 0.25, 0.0, 1.0);
}

vec3 grain_color(vec3 color)
{
    // vec2 uv = vec2(sin(2.0 * 3.1415 * film_grain_shift), cos(2.0 * 3.1415 * film_grain_shift));
    // vec2 uv = vec2(film_grain_shift * 0.374761393, film_grain_shift * 0.668265263);
    vec2 uv = vec2(
        fract(sin(film_grain_shift * 12.9898) * 43758.5453),
        fract(cos(film_grain_shift * 78.233)  * 12345.6789)
    );

    vec3 view_pos = texture(normal_map, pass_tex).xyz;
    float dist = length(view_pos);//dot(view_pos, view_pos);
    vec3 noise = texture(grain_tex, (pass_tex + uv * dist) * FILM_GRAIN_SIZE).rgb;// * 2.0 - 1.0;

    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    float weight = 1.0 - pow(luma, film_grain_power);//1.0 - max(max(color.r, color.g), color.b);//luma;// * 0.5;
    return color + noise.rgb * weight * film_grain_factor;
}

void main()
{
    vec3 tex_color = texture(model_tex, pass_tex).rgb;
    vec3 highlight_color = texture(emission_map, pass_tex).rgb;
    vec4 sobel = texture(sobel_tex, pass_tex);
    float ao = texture(shadow_map, pass_tex).r;
    vec3 view_pos = texture(normal_map, pass_tex, 0.0).rgb;

    if (use_ssao)
        tex_color = tex_color * mix(1.0, ao, ssao_weight);

    if (use_hdr) {
        vec3 hdr_color = tex_color * lighting_exposure;
        // highlight_color = highlight_color / (highlight_color + vec3(1.0));
        // highlight_color = highlight_color / (highlight_color + vec3(0.3));
        // highlight_color = pow(highlight_color, vec3(0.8));
        highlight_color = mix(reinhard_tonemap(highlight_color), aces_tonemap(highlight_color), bloom_operator);

        // hdr_color += highlight_color;

        vec3 reinhard = reinhard_tonemap(hdr_color);
        vec3 aces = aces_tonemap(hdr_color);
        vec3 mapped = mix(reinhard, aces, lighting_operator);

        // FragColor = vec4(clamp(mapped + highlight_color, vec3(0.0), vec3(1.0)), 1.0);
        // FragColor = vec4(mapped, 1.0);

        hdr_color = mix(mapped, radial_fog_color(view_pos), radial_fog_factor(view_pos, fog_near, fog_far));
        FragColor = vec4(hdr_color + highlight_color * 2.0, 1.0);
    } else {
        tex_color = mix(tex_color, radial_fog_color(view_pos), radial_fog_factor(view_pos, fog_near, fog_far));
        FragColor = vec4(tex_color + highlight_color * 2.0, 1.0);
    }

    float factor = sobel.x;
    FragColor = vec4(mix(FragColor.xyz, vec3(0.0), 1 - factor), 1.0);

    if (film_grain)
        FragColor = vec4(grain_color(FragColor.rgb), 1.0);

    FragColor.rgb *= lens_dirt_factor(pass_tex);

    FragColor.rgb = (FragColor.rgb - 0.5) * (1.0 + contrast) + 0.5;

    FragColor = vec4(applyLUT(lut_tex, FragColor.xyz), 1.0);
#ifdef CONFIG_RENDERER_OPENGL
    FragColor.rgb = scene_linear_to_srgb(FragColor.rgb);
#else
    FragColor.rgb = scene_linear_to_pq(FragColor.rgb, 200.0f);
#endif
}
