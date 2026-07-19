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

/*
 * BT.709 primaries -> BT.2020 primaries, D65 white in both, derived from the
 * CIE xy coordinates of each gamut. This is a change of basis on linear light
 * and nothing else: it says which physical red/green/blue the channels point
 * at, and leaves the transfer function alone. It is a plain matrix because
 * both gamuts are linear combinations of the same XYZ tristimulus values,
 * which is also exactly why it is only valid before an OETF is applied.
 *
 * BT.709 is the smaller gamut, so everything lands inside BT.2020 and no
 * component leaves [0, 1]; the inverse direction would produce negatives.
 *
 * GLSL mat3 is column-major: each line below is one column of the matrix.
 */
const mat3 LINEAR_TO_BT2020 = mat3(
    0.6274039, 0.0690973, 0.0163914,  /* col 0 */
    0.3292830, 0.9195404, 0.0880133,  /* col 1 */
    0.0433131, 0.0113623, 0.8955953   /* col 2 */
);

vec3 scene_linear_to_pq(vec3 scene_linear_rgb, float paper_white_nits)
{
    /*
     * PQ output is always paired with a BT.2020 color space (Vulkan's
     * HDR10_ST2084, CoreGraphics' ITUR_2100_PQ), so the scene's BT.709
     * primaries have to be converted to BT.2020 first; skipping it leaves
     * the compositor reading BT.709 coordinates as the wider gamut's and
     * oversaturating everything.
     */
    vec3 abs_nits = (LINEAR_TO_BT2020 * scene_linear_rgb) * paper_white_nits;
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
 * BT.709 primaries -> Display P3 primaries, D65 white in both. From CSS Color 4
 * (https://www.w3.org/TR/css-color-4/#color-conversion-code), via XYZ. Linear
 * light only, on the same terms as LINEAR_TO_BT2020 above.
 * GLSL mat3 is column-major: each line below is one column of the matrix.
 */
const mat3 LINEAR_TO_DISPLAY_P3 = mat3(
    0.8224621, 0.0331941, 0.0170827,  /* col 0 */
    0.1775380, 0.9668058, 0.0723974,  /* col 1 */
    0.0000000, 0.0000000, 0.9105199   /* col 2 */
);

vec3 scene_linear_to_extended_p3(vec3 scene_linear_rgb, float paper_white_nits)
{
    /*
     * Float16 swapchain, colorSpace = DisplayP3, toneMappingMode = Extended.
     * Same idea as the sRGB path, but the compositor expects values in P3
     * primaries — so we convert BT.709 primaries to P3 first. The wider gamut
     * is what gives this path its "punch" on capable displays; the SRGB path
     * just clips anything outside BT.709.
     */
    vec3 p3 = LINEAR_TO_DISPLAY_P3 * scene_linear_rgb;
    return p3 * (paper_white_nits / SDR_WHITE_NITS);
}

#endif /* CLAP_OETF_GLSL */
