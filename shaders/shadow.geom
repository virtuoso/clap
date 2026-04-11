#version 460 core

#include "shader_constants.h"

layout(triangles, invocations = CASCADES_MAX) in;
layout(triangle_strip, max_vertices = 3) out;

#include "ubo_shadow.glsl"

void main()
{
    for (int i = 0; i < gl_in.length(); i++) {
#ifdef SHADER_SHADOW_MAP_ARRAY
        gl_Position = shadow_mvp[gl_InvocationID] * gl_in[i].gl_Position;
        gl_Layer = gl_InvocationID;
#else /* !SHADER_SHADOW_MAP_ARRAY */
        gl_Position = gl_in[i].gl_Position;
        gl_Layer = 0;
#endif /* !SHADER_SHADOW_MAP_ARRAY */
        EmitVertex();
    }

    EndPrimitive();
}
