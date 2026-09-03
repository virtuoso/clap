#version 460 core

/*
 * Pixel-for-pixel copy of model_tex: puts a pass that had to render
 * off-screen onto the screen (see the screen capture setup in
 * pipeline-builder.c); source and target have the same size
 */

#include "shader_constants.h"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

void main()
{
    /* same size: the samples land on texel centers, filtering is moot */
    FragColor = texture(model_tex, pass_tex);
}
