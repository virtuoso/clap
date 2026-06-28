#version 460 core

#include "shader_constants.h"
#include "pass-tex.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;

#include "ubo_projview.glsl"
#include "ubo_render_common.glsl"
#include "ubo_bloom.glsl"

void main()
{
    vec3 blurred = texture(model_tex, pass_tex).rgb;

    /*
     * The raw emission tap is jitter-displaced like the downsample's
     * input, see bloom-downsample.frag; sampled at 1/4-res stride it
     * aliases per frame especially hard on small emitters.
     */
    vec2 em_uv = use_taa ?
        convert_pass_tex(convert_pass_tex(pass_tex) + jitter * 0.5) :
        pass_tex;

    if (!use_hdr) {
        FragColor = vec4(mix(texture(emission_map, em_uv).rgb, blurred, clamp(bloom_intensity, 0.0, 1.0)), 1.0);
        return;
    }

    vec3 hdr_color = (texture(emission_map, em_uv).rgb + blurred * bloom_intensity) * bloom_exposure;
    FragColor = vec4(hdr_color, 1.0);
}
