#version 460 core

#include "shader_constants.h"

layout (location=0) in vec3 position;
layout (location=1) in vec2 tex;

layout (std140, binding = UBO_BINDING_transform) uniform transform {
    mat4 trans;
};

uniform float width;

layout (location=0) out vec2 pass_tex;
layout (location=1) out vec2 blur_coords[11];

void main()
{
    gl_Position = trans * vec4(position, 1.0);
    pass_tex = tex;
    float pixsz = 1.0 / width;
    for (int i = -5; i <= 5; i++)
        blur_coords[i + 5] = vec2(pixsz * float(i), 0.0);
}
