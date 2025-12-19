#ifndef CLAP_OETF_GLSL
#define CLAP_OETF_GLSL

float linear_to_pq(float l)
{
    /* l is absolute luminance normalized to 10000 nits: l in [0, 1] */
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;

    l = clamp(l, 0.0, 1.0);

    float l_m1 = pow(l, m1);
    float num  = c1 + c2 * l_m1;
    float den  = 1.0 + c3 * l_m1;

    return pow(num / den, m2);
}

vec3 scene_linear_to_pq(vec3 scene_linear_rgb, float paper_white_nits)
{
    vec3 abs_nits = scene_linear_rgb * paper_white_nits;
    vec3 l = abs_nits / 10000.0;
    return vec3(
        linear_to_pq(l.r),
        linear_to_pq(l.g),
        linear_to_pq(l.b)
    );
}

vec3 scene_linear_to_srgb(vec3 scene_linear_rgb)
{
    /*
     * This is not actually sRGB, it lacks the linear part at the bottom,
     * but this is close enough.
     */
    return pow(scene_linear_rgb, vec3(1.0 / 2.2));
}

#endif /* CLAP_OETF_GLSL */
