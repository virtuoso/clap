#ifndef SHADERS_PRESENT_COMMON_GLSL
#define SHADERS_PRESENT_COMMON_GLSL

#include "oetf.glsl"
#include "view_pos.glsl"

/*
 * Film grain + OETF encode tail shared by present.frag and the merged
 * combine-present.frag; expects the grain_tex/depth_tex samplers and
 * the postproc UBO in scope.
 */

vec3 grain_color(vec3 color, vec3 view_pos, vec2 uv)
{
    vec2 shift = vec2(
        fract(sin(film_grain_shift * 12.9898) * 43758.5453),
        fract(cos(film_grain_shift * 78.233)  * 12345.6789)
    );

    float dist = length(view_pos);
    vec3 noise = texture(grain_tex, (uv + shift * dist) * FILM_GRAIN_SIZE).rgb;

    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    float weight = 1.0 - pow(luma, film_grain_power);
    return color + noise.rgb * weight * film_grain_factor;
}

vec3 present_finalize(vec3 color, vec2 uv)
{
    if (film_grain) {
        vec3 view_pos = view_pos_from_depth(depth_tex, inverse_proj, uv);
        color = grain_color(color, view_pos, uv);
    }

    if (hdr_output)
        // TODO: This should be a uniform: extended_p3/pq/extended_srgb
        // For now, this should be enough
#ifdef SHADER_RENDERER_WGPU
        color = scene_linear_to_extended_p3(color);
#else /* !SHADER_RENDERER_WGPU */
        color = scene_linear_to_pq(color, hdr_white_nits);
#endif /* !SHADER_RENDERER_WGPU */
    else
        color = scene_linear_to_srgb(color);

    return color;
}

#endif /* SHADERS_PRESENT_COMMON_GLSL */
