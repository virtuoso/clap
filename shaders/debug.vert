#version 460 core

#include "shader_constants.h"

layout (location=0) in vec3 position;

#include "ubo_projview.glsl"
#include "ubo_transform.glsl"

void main()
{
    gl_Position = proj * view * trans * vec4(position, 1.0);
}
