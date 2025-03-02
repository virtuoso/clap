#version 460 core

#include "shader_constants.h"

layout (location=0) in vec3 position;

uniform mat4 proj;
uniform mat4 view;

layout (std140, binding = UBO_BINDING_transform) uniform transform {
    mat4 trans;
};

void main()
{
    gl_Position = proj * view * trans * vec4(position, 1.0);
}
