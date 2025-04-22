#version 460 core

#include "shader_constants.h"

layout (location=0) in vec3 position;
layout (location=1) in vec2 tex;

layout (std140, binding = UBO_BINDING_transform) uniform transform {
    mat4 trans;
};

layout (location=0) out vec2 pass_tex;

void main()
{
    gl_Position = trans * vec4(position, 1.0);
    pass_tex = tex;
}

