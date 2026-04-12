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

/*
 * Float16 swapchains with extended color spaces (WebGPU's
 * WGPUPredefinedColorSpace_SRGB / DisplayP3 with toneMappingMode_Extended,
 * macOS's extendedSRGB / extendedDisplayP3) are LINEAR-LIGHT, not
 * gamma/PQ-encoded. The compositor scans linear values out and applies the
 * display's transfer function itself. The conventions:
 *   - 1.0           == SDR diffuse white (~80 nits, sRGB reference)
 *   - values > 1.0  == HDR boost above SDR white
 *   - primaries     == sRGB (Rec.709) for the SRGB color space,
 *                      Display P3 for the DisplayP3 color space
 *
 * So the OETF for these targets is identity for the transfer curve plus a
 * primary conversion (and a uniform scale to map your scene's "paper white"
 * to the compositor's SDR reference).
 */

/*
 * sRGB convention: 1.0 == 80 nits diffuse white. The user-facing
 * paper_white_nits slider then controls how many nits paper white actually
 * lands at on screen by scaling above the compositor's reference.
 */
const float SDR_WHITE_NITS = 80.0;

vec3 scene_linear_to_extended_srgb(vec3 scene_linear_rgb, float paper_white_nits)
{
    /*
     * Float16 swapchain, colorSpace = SRGB, toneMappingMode = Extended.
     * Our scene is rendered in sRGB primaries already, so the only thing
     * left is to scale paper-white-relative values into compositor units
     * (1.0 = SDR white). hdr_display_map() leaves rgb in "relative to
     * paper white" units, so 1.0 in == paper_white_nits out.
     */
    return scene_linear_rgb * (paper_white_nits / SDR_WHITE_NITS);
}

/*
 * sRGB (Rec.709) → Display P3 D65 linear primary conversion. From CSS Color 4
 * (https://www.w3.org/TR/css-color-4/#color-conversion-code), via XYZ.
 * GLSL mat3 is column-major: each line below is one column of the matrix.
 */
const mat3 SRGB_TO_DISPLAY_P3 = mat3(
    0.8224621, 0.0331941, 0.0170827,  /* col 0 */
    0.1775380, 0.9668058, 0.0723974,  /* col 1 */
    0.0000000, 0.0000000, 0.9105199   /* col 2 */
);

vec3 scene_linear_to_extended_p3(vec3 scene_linear_rgb, float paper_white_nits)
{
    /*
     * Float16 swapchain, colorSpace = DisplayP3, toneMappingMode = Extended.
     * Same idea as the sRGB path, but the compositor expects values in P3
     * primaries — so we rotate sRGB primaries into P3 first. The wider gamut
     * is what gives this path its "punch" on capable displays; the SRGB path
     * just clips anything outside Rec.709.
     */
    vec3 p3 = SRGB_TO_DISPLAY_P3 * scene_linear_rgb;
    return p3 * (paper_white_nits / SDR_WHITE_NITS);
}

#endif /* CLAP_OETF_GLSL */
