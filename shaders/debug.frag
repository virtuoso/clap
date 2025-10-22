#version 460 core

#include "shader_constants.h"
#include "color_pt.glsl"

layout (location=0) out vec4 FragColor;

void main()
{
    FragColor = color_override(vec4(0.0));
}
