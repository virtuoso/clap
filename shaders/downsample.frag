#version 460 core

#include "shader_constants.h"
#include "downsample.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

void main()
{
    FragColor = downsample_5tap(model_tex, pass_tex);
}
