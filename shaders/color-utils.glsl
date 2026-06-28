#ifndef SHADERS_COLOR_UTILS_GLSL
#define SHADERS_COLOR_UTILS_GLSL

float luma(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

/*
 * YCoCg: cheap linear transform that decorrelates luma from chroma;
 * an axis-aligned bounding box around a pixel neighborhood is much
 * tighter there than in RGB, which is what the TAA history clip wants
 */
vec3 rgb_to_ycocg(vec3 c)
{
    return vec3(
        dot(c, vec3( 0.25, 0.5,  0.25)),
        dot(c, vec3( 0.5,  0.0, -0.5)),
        dot(c, vec3(-0.25, 0.5, -0.25))
    );
}

vec3 ycocg_to_rgb(vec3 c)
{
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

#endif /* SHADERS_COLOR_UTILS_GLSL */
