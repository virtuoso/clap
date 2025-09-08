#version 460 core

layout (location=0) out vec2 Moment;

#include "shader_constants.h"
#include "ubo_postproc.glsl"

float linearize_depth(float depth, float near_plane, float far_plane)
{
    float linear_depth = near_plane * far_plane / (far_plane + depth * (near_plane - far_plane));
    return (linear_depth - near_plane) / (far_plane - near_plane);
}

void main()
{
    float d = gl_FragCoord.z;
    // float d = linearize_depth(gl_FragCoord.z, near_plane, far_plane);
    Moment = vec2(d, d * d);
}
