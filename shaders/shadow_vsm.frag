#version 460 core

#include "shader_constants.h"

layout (location=0) out vec2 Moment;

void main()
{
    float d = gl_FragCoord.z;
    Moment = vec2(d, d * d);
}
