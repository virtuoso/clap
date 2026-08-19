#version 460 core

#include "shader_constants.h"
#include "pass-tex.glsl"
#include "view_pos.glsl"

layout (location=0) out float FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_depth_tex) uniform sampler2D depth_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2D normal_map;
layout (binding=SAMPLER_BINDING_sobel_tex) uniform sampler2D sobel_tex;

#include "ubo_projview.glsl"
#include "ubo_postproc.glsl"

/*
 * Horizon-based GTAO (Jimenez et al. 2016, "Practical Realtime Strategies
 * for Accurate Indirect Occlusion"), an alternative to ssao.frag with the
 * same inputs: depth, view space normals, rotation noise in sobel_tex and
 * the ssao_radius/ssao_noise_scale uniforms.
 *
 * For every fragment, march GTAO_SLICES screen space directions, find the
 * horizon angles on both sides of the view vector within the slice plane
 * and integrate the unoccluded, cosine-weighted arc around the projected
 * normal.
 */

#define GTAO_SLICES 3
#define GTAO_STEPS 3
#define GTAO_ANGLE_BIAS 0.1
/* the nearest sample's distance from the center, in depth texels */
#define GTAO_MIN_TEXELS 1.5
#define PI 3.1415926538

/* texture space direction for an NDC space direction, see convert_pass_tex() */
vec2 convert_pass_dir(vec2 dir)
{
    return convert_pass_tex(dir * 0.5 + 0.5) * 2.0 - 1.0;
}

void main()
{
    vec2 noise_uv = pass_tex * ssao_noise_scale;
    vec2 rot = normalize(texture(sobel_tex, noise_uv).xy);

    vec3 normal_sample = texture(normal_map, pass_tex).xyz;
    vec3 normal = normalize(normal_sample * 2.0 - 1.0);
    vec3 pos = view_pos_from_depth(depth_tex, inverse_proj, pass_tex);

    FragColor = 1.0;

    vec3 view_dir = normalize(-pos);

    float scale = clamp(1.0 + pos.z / far_plane, 0.1, 1.0);
    float actual_radius = ssao_radius * scale;

    /* view space radius projected to NDC, per axis */
    vec2 ndc_radius = vec2(proj[0][0], proj[1][1]) * (actual_radius / -pos.z);

    /*
     * The radius in depth texels, the same along every slice: proj[0][0]
     * is proj[1][1] scaled by the aspect ratio. Sub-texel samples read
     * the center texel's depth, which makes them false occluders on
     * sloped surfaces, so the march starts GTAO_MIN_TEXELS out; once the
     * radius shrinks under that, every sample lands past the kernel and
     * the horizons it finds are quantization noise: fade the kernel out
     * over its last texel and skip it once it is gone. Far geometry
     * shrinks below this well before the far plane, where the background
     * sits.
     */
    float radius_px = ndc_radius.y * 0.5 * float(textureSize(depth_tex, 0).y);
    float fade = clamp(radius_px - GTAO_MIN_TEXELS, 0.0, 1.0);
    if (fade == 0.0)    return;

    float min_t = GTAO_MIN_TEXELS / radius_px;

    /* interleaved gradient noise: step jitter decorrelated from rot */
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    float visibility = 0.0;

    for (int slice = 0; slice < GTAO_SLICES; ++slice) {
        float phi = PI * float(slice) / float(GTAO_SLICES);
        vec2 dir = vec2(cos(phi), sin(phi));
        dir = vec2(dir.x * rot.x - dir.y * rot.y, dir.x * rot.y + dir.y * rot.x);

        /*
         * Slice plane basis: view_dir and the marching direction, which
         * is dir in view space; ndc_radius takes it to the screen per
         * axis, so the march and the plane agree at any aspect ratio
         */
        vec3 slice_dir = normalize(vec3(dir, 0.0));
        vec3 axis = normalize(cross(slice_dir, view_dir));
        vec3 tangent = cross(view_dir, axis);

        /* normal projected onto the slice plane; n is its angle from view_dir */
        vec3 proj_normal = normal - axis * dot(normal, axis);
        float proj_len = length(proj_normal);
        float cos_n = clamp(dot(proj_normal, view_dir) / max(proj_len, 1e-6), 0.0, 1.0);
        float n = sign(dot(proj_normal, tangent)) * acos(cos_n);

        /* texture space offset of the outermost sample */
        vec2 duv = convert_pass_dir(dir * ndc_radius) * 0.5;

        for (int side = 0; side < 2; ++side) {
            float side_sign = side == 0 ? 1.0 : -1.0;
            float cos_h = -1.0;

            for (int i = 0; i < GTAO_STEPS; ++i) {
                float t = (float(i) + jitter) / float(GTAO_STEPS);
                t = max(t * t, min_t);
                vec2 suv = pass_tex + duv * (side_sign * t);
                if (suv.x < 0.0 || suv.y < 0.0 || suv.x >= 1.0 || suv.y >= 1.0) break;

                vec3 sample_view = view_pos_from_depth(depth_tex, inverse_proj, suv);
                vec3 delta = sample_view - pos;
                float dist = length(delta);
                if (dist < 1e-4)    continue;

                float cos_s = dot(delta, view_dir) / dist;
                /* horizons fade out between actual_radius and 2 * actual_radius */
                float falloff = clamp((dist - actual_radius) / actual_radius, 0.0, 1.0);
                cos_h = max(cos_h, mix(cos_s, -1.0, falloff));
            }

            /*
             * Open the horizon up by the bias angle to absorb quantization
             * noise from the depth and normal buffers, then clamp it to
             * the hemisphere around the normal
             */
            float h = side_sign * (acos(clamp(cos_h, -1.0, 1.0)) + GTAO_ANGLE_BIAS);
            h = n + clamp(h - n, -0.5 * PI, 0.5 * PI);

            /* analytic cosine-weighted arc integral */
            visibility += proj_len * 0.25 * (cos_n + 2.0 * h * sin(n) - cos(2.0 * h - n));
        }
    }

    FragColor = 1.0 - fade * (1.0 - clamp(visibility / float(GTAO_SLICES), 0.0, 1.0));
}
