#version 460 core

#include "shader_constants.h"

layout(triangles, invocations = CASCADES_MAX) in;
layout(triangle_strip, max_vertices = 3) out;

layout (std140, binding = UBO_BINDING_shadow) uniform shadow {
    mat4 shadow_mvp[CASCADES_MAX];
    float cascade_distances[CASCADES_MAX];
    bool shadow_outline;
};

void main()
{
    for (int i = 0; i < gl_in.length(); i++) {
        gl_Position = shadow_mvp[gl_InvocationID] * gl_in[i].gl_Position;
        gl_Layer = gl_InvocationID;
        EmitVertex();
    }
    EndPrimitive();
}
