#version 460 core

#include "shader_constants.h"
#include "pass-tex.glsl"

layout (location=ATTR_LOC_POSITION) in vec3 position;
layout (location=ATTR_LOC_TEX) in vec2 tex;

#include "ubo_transform.glsl"

layout (location=0) out vec2 pass_tex;

void main()
{
    gl_Position = trans * vec4(position, 1.0);
    pass_tex = convert_pass_tex(tex);
}

