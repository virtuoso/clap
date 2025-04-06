#version 460 core

#include "tonemap.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

uniform sampler2D model_tex;
uniform sampler2D emission_map;
uniform sampler2D sobel_tex;
uniform sampler2D normal_map;

uniform float bloom_intensity;
uniform float bloom_exposure;
uniform float bloom_operator;

uniform float lighting_exposure;
uniform float lighting_operator;
uniform bool use_hdr;

uniform float fog_near;
uniform float fog_far;
uniform vec3 fog_color;

float radial_fog_factor(sampler2D tex, vec2 uv, float near_fog, float far_fog)
{
    vec3 view_pos = texture(tex, uv, 0.0).rgb;
    float dist = length(view_pos);
    return clamp((dist - near_fog) / (far_fog - near_fog), 0.0, 1.0);
}

void main()
{
    vec3 tex_color = texture(model_tex, pass_tex).rgb;
    vec3 highlight_color = texture(emission_map, pass_tex).rgb;
    vec4 sobel = texture(sobel_tex, pass_tex);

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

        hdr_color = mix(hdr_color, fog_color, radial_fog_factor(normal_map, pass_tex, fog_near, fog_far));
        FragColor = vec4(hdr_color + highlight_color * 2.0, 1.0);
    } else {
        tex_color = mix(tex_color, fog_color, radial_fog_factor(normal_map, pass_tex, fog_near, fog_far));
        FragColor = vec4(tex_color + highlight_color * 2.0, 1.0);
    }

    float factor = sobel.x;
    FragColor = vec4(mix(FragColor.xyz, vec3(0.0), 1 - factor), 1.0);
}
