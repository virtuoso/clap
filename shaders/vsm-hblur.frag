#version 460 core

#include "shader_constants.h"

layout (location=0) out vec2 Moment;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

/*
 * 3-tap depth-aware bilateral blur for VSM moments (horizontal pass).
 */
void main()
{
    float pixsz = 1.0 / float(textureSize(model_tex, 0).x);
    vec2 c  = texture(model_tex, pass_tex).rg;
    vec2 t0 = texture(model_tex, pass_tex + vec2(-pixsz, 0.0)).rg;
    vec2 t1 = texture(model_tex, pass_tex + vec2( pixsz, 0.0)).rg;

    float depth_thresh = 0.1;
    float w0 = abs(t0.x - c.x) < depth_thresh ? 0.25 : 0.0;
    float w1 = abs(t1.x - c.x) < depth_thresh ? 0.25 : 0.0;
    float wc = 1.0 - w0 - w1;

    Moment = c * wc + t0 * w0 + t1 * w1;
}
