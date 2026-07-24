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
 * Float16 swapchains with extended color spaces. The conventions they share:
 *   - 1.0           == the compositor's SDR white, whatever it currently has
 *                      that set to -- commonly 200 nits on a KWin desktop,
 *                      not the nominal 80 the sRGB spec names
 *   - values > 1.0  == HDR boost above SDR white
 *   - primaries     == BT.709 for the sRGB color space, Display P3 for the
 *                      P3 one
 * and deliberately no paper-white scale on top: hdr_display_map() already
 * leaves 1.0 meaning diffuse white, which is exactly what the compositor's
 * 1.0 means; the old paper_white_nits/80 factor made these paths ~2.5x
 * brighter than PQ instead. hdr_white_nits is inert here: only PQ, which
 * encodes absolute nits, can honour it.
 *
 * Where they differ is the transfer curve. Vulkan's EXTENDED_SRGB_LINEAR /
 * DISPLAY_P3_LINEAR and Metal's extended linear sRGB are LINEAR-LIGHT: the
 * compositor scans linear values out and applies the display's transfer
 * function itself, so the encode is the identity plus a primary conversion
 * where the target isn't BT.709. A browser canvas is not: WebGPU's
 * colorSpace "srgb" / "display-p3" are the CSS predefined color spaces, which
 * carry their transfer function, and toneMappingMode Extended only lets the
 * curve continue past 1.0 (there is no linear predefined color space). Linear
 * light written there is read as encoded values: 0.3 shows as ~0.07 linear,
 * darks crushed, brights nearly right. Vulkan names the same thing
 * EXTENDED_SRGB_NONLINEAR. So there are two encoders per gamut, linear and
 * nonlinear, and HDR_TRANSFER_* says which one a surface is; the backend
 * that configured it reports that.
 */

/*
 * The sRGB transfer function, extended: the piecewise curve as CSS Color 4
 * defines it, continued past 1.0 and mirrored for negatives, no clamp. It is
 * what a browser canvas expects for both "srgb" and "display-p3", which
 * share the curve and differ in primaries only.
 */
vec3 extended_srgb_oetf(vec3 linear_rgb)
{
    vec3 a = abs(linear_rgb);
    vec3 encoded = mix(a * 12.92, 1.055 * pow(a, vec3(1.0 / 2.4)) - 0.055,
                       step(vec3(0.0031308), a));
    return sign(linear_rgb) * encoded;
}

vec3 scene_linear_to_extended_srgb_linear(vec3 scene_linear_rgb)
{
    /* EXTENDED_SRGB_LINEAR: BT.709 in, BT.709 out, linear light, nothing to do */
    return scene_linear_rgb;
}

vec3 scene_linear_to_extended_srgb_nonlinear(vec3 scene_linear_rgb)
{
    /* EXTENDED_SRGB_NONLINEAR, the canvas' "srgb": BT.709 in and out, the curve applied */
    return extended_srgb_oetf(scene_linear_rgb);
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

/*
 * Same idea as the sRGB pair, but the compositor expects values in P3
 * primaries -- so BT.709 primaries are converted to P3 first. The wider gamut
 * is what gives these paths their "punch" on capable displays; the sRGB pair
 * just clips anything outside BT.709.
 */
vec3 scene_linear_to_extended_p3_linear(vec3 scene_linear_rgb)
{
    /* DISPLAY_P3_LINEAR: linear light */
    return LINEAR_TO_DISPLAY_P3 * scene_linear_rgb;
}

vec3 scene_linear_to_extended_p3_nonlinear(vec3 scene_linear_rgb)
{
    /* DISPLAY_P3_NONLINEAR, the canvas' "display-p3": the curve is sRGB's */
    return extended_srgb_oetf(LINEAR_TO_DISPLAY_P3 * scene_linear_rgb);
}

#endif /* CLAP_OETF_GLSL */
