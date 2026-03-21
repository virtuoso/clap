#version 460 core

#include "shader_constants.h"
#include "ubo_postproc.glsl"
#include "linearize-depth.glsl"

layout (location=0) out vec2 Moment;

void main()
{
    float d = linearize_depth(gl_FragCoord.z, near_plane, far_plane);
    Moment = vec2(d, d * d);
}
