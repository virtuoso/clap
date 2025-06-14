#ifndef LUT_GLSL
#define LUT_GLSL

/* XXX: make this a uniform */
const float lut_size = 32.0;

vec3 applyLUT(sampler3D lut, vec3 color)
{
    /* Clamp input to [0,1] just in case */
    color = clamp(color, 0.0, 1.0);

    /* Scale from [0,1] to [0,1] LUT space with texel offset */
    float scale = (lut_size - 1.0) / lut_size;
    float offset = 0.5 / lut_size;

    vec3 uvw = color * scale + offset;
    return texture(lut, uvw).rgb;
}

#endif /* LUT_GLSL */
