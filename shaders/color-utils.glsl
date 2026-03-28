#ifndef SHADERS_COLOR_UTILS_GLSL
#define SHADERS_COLOR_UTILS_GLSL

float luma(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

#endif /* SHADERS_COLOR_UTILS_GLSL */
