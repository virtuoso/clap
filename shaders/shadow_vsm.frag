#version 460 core

#include "shader_constants.h"
#include "ubo_postproc.glsl"
#include "ndc-z.glsl"

layout (location=0) out vec2 Moment;

// XXX
float linearize_depth(float depth)
{
    float z = convert_to_ndc_z(depth);
    return (2.0 * near_plane * far_plane) / (far_plane + near_plane - z * (far_plane - near_plane));
}

void main()
{
    float d = linearize_depth(gl_FragCoord.z);
    Moment = vec2(d, d * d);
}
