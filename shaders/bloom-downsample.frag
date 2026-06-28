#version 460 core

#include "shader_constants.h"
#include "pass-tex.glsl"
#include "downsample.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

#include "ubo_projview.glsl"
#include "ubo_render_common.glsl"

void main()
{
    /*
     * The emission raster is displaced by the TAA jitter; sample at the
     * displaced position to read temporally stable unjittered content,
     * otherwise bloom pulses whenever a small emitter crosses a
     * quarter-res texel. jitter is in NDC units, the uv delta is half
     * that, orientation handled by the self-inverse convert_pass_tex().
     * use_taa rather than a jitter test: the jitter uniforms go stale,
     * not zero, when TAA is toggled off.
     */
    vec2 uv = use_taa ?
        convert_pass_tex(convert_pass_tex(pass_tex) + jitter * 0.5) :
        pass_tex;

    FragColor = downsample_5tap(model_tex, uv);
}
