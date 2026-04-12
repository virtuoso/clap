#version 460 core

#include "shader_constants.h"

layout (location=0) out vec2 Moment;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

/*
 * 5-tap depth-aware bilateral blur for VSM moments (horizontal).
 */
void main()
{
    float pixsz = 1.0 / float(textureSize(model_tex, 0).x);
    vec2 c  = texture(model_tex, pass_tex).rg;
    vec2 t0 = texture(model_tex, pass_tex + vec2(-2.0 * pixsz, 0.0)).rg;
    vec2 t1 = texture(model_tex, pass_tex + vec2(-1.0 * pixsz, 0.0)).rg;
    vec2 t2 = texture(model_tex, pass_tex + vec2( 1.0 * pixsz, 0.0)).rg;
    vec2 t3 = texture(model_tex, pass_tex + vec2( 2.0 * pixsz, 0.0)).rg;

    float depth_thresh = 0.1;
    float w0 = abs(t0.x - c.x) < depth_thresh ? 0.06 : 0.0;
    float w1 = abs(t1.x - c.x) < depth_thresh ? 0.24 : 0.0;
    float w2 = abs(t2.x - c.x) < depth_thresh ? 0.24 : 0.0;
    float w3 = abs(t3.x - c.x) < depth_thresh ? 0.06 : 0.0;
    float wc = 1.0 - w0 - w1 - w2 - w3;

    Moment = c * wc + t0 * w0 + t1 * w1 + t2 * w2 + t3 * w3;
}
