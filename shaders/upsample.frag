#version 460 core

#include "shader_constants.h"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;

#include "ubo_render_common.glsl"
#include "ubo_bloom.glsl"

void main()
{
    vec3 blurred = texture(model_tex, pass_tex).rgb;

    if (!use_hdr) {
        FragColor = vec4(mix(texture(emission_map, pass_tex).rgb, blurred, clamp(bloom_intensity, 0.0, 1.0)), 1.0);
        return;
    }

    vec3 hdr_color = (texture(emission_map, pass_tex).rgb + blurred * bloom_intensity) * bloom_exposure;
    FragColor = vec4(hdr_color, 1.0);
}
