#version 460 core

#include "shader_constants.h"
#include "texel_fetch.inc"

uniform sampler2D model_tex;  /* Pre-edge detection color input */
uniform sampler2D normal_map; /* Blend weights from previous pass */

layout (location=0) in vec2 pass_tex;
layout (location=0) out vec4 FragColor;

void main() {
    vec4 color = texel_fetch_2d(model_tex, pass_tex, ivec2(0));
    vec4 blend = texel_fetch_2d(normal_map, pass_tex, ivec2(0));

    // Blend in 4 directions using weights
    vec3 blendedColor = color.rgb;
    blendedColor += blend.x * texel_fetch_2d(model_tex, pass_tex, ivec2(-1,  0)).rgb;
    blendedColor += blend.y * texel_fetch_2d(model_tex, pass_tex, ivec2( 1,  0)).rgb;
    blendedColor += blend.z * texel_fetch_2d(model_tex, pass_tex, ivec2( 0,  1)).rgb;
    blendedColor += blend.w * texel_fetch_2d(model_tex, pass_tex, ivec2( 0, -1)).rgb;

    FragColor = vec4(blendedColor / (1.0 + blend.x + blend.y + blend.z + blend.w), 1.0);
}
