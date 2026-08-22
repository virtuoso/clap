#ifndef SHADERS_PRESENT_COMMON_GLSL
#define SHADERS_PRESENT_COMMON_GLSL

#include "oetf.glsl"
#include "shader_constants.h"
#include "view_pos.glsl"

/*
 * Film grain + OETF encode tail shared by present.frag and the merged
 * combine-present.frag; expects the grain_tex/depth_tex samplers and
 * the postproc and frame UBOs in scope.
 */

vec3 grain_color(vec3 color, vec3 view_pos, vec2 uv)
{
    /*
     * Reseed the grain UV at 24Hz for the film cadence, independent of
     * the display frame rate; the seed only needs to be distinct per
     * tick, which float precision sustains for days of uptime
     */
    float grain_seed = floor(uptime * 24.0);
    vec2 shift = vec2(
        fract(sin(grain_seed * 12.9898) * 43758.5453),
        fract(cos(grain_seed * 78.233)  * 12345.6789)
    );

    float dist = length(view_pos);
    vec3 noise = texture(grain_tex, (uv + shift * dist) * FILM_GRAIN_SIZE).rgb;

    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    float weight = 1.0 - pow(luma, film_grain_power);
    /*
     * Zero-mean and achromatic: the texture's three channels are
     * independent noise normalized to [0, 1], and adding them as they
     * come would lift every pixel by half the factor and speckle its hue
     */
    return color + vec3((noise.r - 0.5) * 2.0) * weight * film_grain_factor;
}

vec3 present_finalize(vec3 color, vec2 uv)
{
    if (film_grain) {
        vec3 view_pos = view_pos_from_depth(depth_tex, inverse_proj, uv);
        color = grain_color(color, view_pos, uv);
    }

    /*
     * The encode has to match the surface the backend actually got, which is
     * what hdr_transfer carries: the extended pairs differ in primaries and
     * in whether the compositor takes linear light or the sRGB curve, PQ is
     * the one with a curve of its own.
     */
    if (hdr_output) {
        if (hdr_transfer == HDR_TRANSFER_EXT_SRGB_LINEAR)
            color = scene_linear_to_extended_srgb_linear(color);
        else if (hdr_transfer == HDR_TRANSFER_EXT_P3_LINEAR)
            color = scene_linear_to_extended_p3_linear(color);
        else if (hdr_transfer == HDR_TRANSFER_EXT_SRGB_NONLINEAR)
            color = scene_linear_to_extended_srgb_nonlinear(color);
        else if (hdr_transfer == HDR_TRANSFER_EXT_P3_NONLINEAR)
            color = scene_linear_to_extended_p3_nonlinear(color);
        else if (hdr_transfer == HDR_TRANSFER_SCRGB)
            color = scene_linear_to_scrgb(color, hdr_white_nits);
        else
            color = scene_linear_to_pq(color, hdr_white_nits);
    } else {
        color = scene_linear_to_srgb(color);
    }

    return color;
}

#endif /* SHADERS_PRESENT_COMMON_GLSL */
