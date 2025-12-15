#version 460 core

#include "shader_constants.h"
#include "ubo_postproc.glsl"
#include "ndc-z.glsl"

layout (location=0) out vec2 Moment;

// XXX
float linearize_depth(float depth)
{
    float z = convert_to_ndc_z(depth);
#ifdef CONFIG_NDC_ZERO_ONE
    return near_plane * far_plane / (far_plane - z * (far_plane - near_plane));
#else /* !CONFIG_NDC_ZERO_ONE */
    return (2.0 * near_plane * far_plane) / (far_plane + near_plane - z * (far_plane - near_plane));
#endif /* !CONFIG_NDC_ZERO_ONE */
}

void main()
{
    float d = linearize_depth(gl_FragCoord.z);
    Moment = vec2(d, d * d);
}
