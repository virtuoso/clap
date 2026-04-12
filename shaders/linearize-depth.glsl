#ifndef SHADERS_LINEARIZE_DEPTH_GLSL
#define SHADERS_LINEARIZE_DEPTH_GLSL

#include "ndc-z.glsl"

float linearize_depth_ndc(float ndc_z, float near_plane, float far_plane)
{
#ifdef SHADER_NDC_ZERO_ONE
    return near_plane * far_plane / (far_plane - ndc_z * (far_plane - near_plane));
#else /* !SHADER_NDC_ZERO_ONE */
    return (2.0 * near_plane * far_plane) / (far_plane + near_plane - ndc_z * (far_plane - near_plane));
#endif /* !SHADER_NDC_ZERO_ONE */
}

float linearize_depth(float depth, float near_plane, float far_plane)
{
    return linearize_depth_ndc(convert_to_ndc_z(depth), near_plane, far_plane);
}

#endif /* SHADERS_LINEARIZE_DEPTH_GLSL */
