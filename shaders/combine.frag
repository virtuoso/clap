#version 460 core

#include "tonemap.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

uniform sampler2D model_tex;
uniform sampler2D emission_map;
uniform sampler2D sobel_tex;

uniform float bloom_intensity;
uniform float bloom_exposure;
uniform float bloom_operator;

uniform float lighting_exposure;
uniform float lighting_operator;
uniform bool use_hdr;

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

        FragColor = vec4(hdr_color + highlight_color * 2.0, 1.0);
    } else {
        FragColor = vec4(tex_color + highlight_color * 2.0, 1.0);
    }

    float factor = sobel.x;
    FragColor = vec4(mix(FragColor.xyz, vec3(0.0), 1 - factor), 1.0);
}
