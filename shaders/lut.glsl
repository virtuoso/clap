#ifndef LUT_GLSL
#define LUT_GLSL

#include "shader_constants.h"

float linear_to_lut(float x)
{
    x = max(x, 0.0);
    return clamp(log2(1 + HDR_LUT_K * x) / log2(1 + HDR_LUT_K * HDR_LUT_MAX), 0.0, 1.0);
}

float lut_to_linear(float u)
{
    return (pow(2.0, u * log2(1 + HDR_LUT_K * HDR_LUT_MAX)) - 1.0) / HDR_LUT_K;
    // return (HDR_LUT_K * u * hdr_max_norm) / (1.0 - u * hdr_max_norm);
}

vec3 apply_lut(sampler3D lut, vec3 color)
{
    float lut_size = textureSize(lut, 0).x;

    vec3 shaped = vec3(
        linear_to_lut(color.r),
        linear_to_lut(color.g),
        linear_to_lut(color.b)
    );

    /* Scale from [0,1] to [0,1] LUT space with texel offset */
    float scale = (lut_size - 1.0) / lut_size;
    float offset = 0.5 / lut_size;

    vec3 uvw = shaped * scale + offset;
    vec3 graded = texture(lut, uvw).rgb;
    return vec3(
        lut_to_linear(graded.r),
        lut_to_linear(graded.g),
        lut_to_linear(graded.b)
    );
}

#endif /* LUT_GLSL */
