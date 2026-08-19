#version 460 core

#include "shader_constants.h"
#include "half.glsl"
#include "color-utils.glsl"
#include "pass-tex.glsl"
#include "view_pos.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_depth_tex) uniform sampler2D depth_tex;
layout (binding=SAMPLER_BINDING_history_tex) uniform sampler2D history_tex;
layout (binding=SAMPLER_BINDING_velocity_tex) uniform sampler2D velocity_tex;

#include "ubo_projview.glsl"
#include "ubo_postproc.glsl"

/*
 * One pass over the 3x3 neighborhood around uv: first two moments of
 * the color in YCoCg space (for the variance clip) and the offset of
 * the sample closest to the camera (for depth-dilated reprojection).
 * Explicit-LOD sampling throughout: this runs under a non-uniform
 * branch, where tint (WGSL) disallows implicit-derivative texture().
 */
void neighborhood(vec2 uv, out f16vec3 mu, out f16vec3 sigma, out ivec2 near_off)
{
    ivec2 size = textureSize(depth_tex, 0);
    ivec2 tc = ivec2(uv * vec2(size));
    f16vec3 m1 = HVEC3(0.0);
    f16vec3 m2 = HVEC3(0.0);
    float near_depth = 1.0;

    near_off = ivec2(0);

#define TAP(dx, dy) { \
        f16vec3 c = HVEC3(rgb_to_ycocg(textureLodOffset(model_tex, uv, 0.0, ivec2(dx, dy)).rgb)); \
        m1 += c; \
        m2 += c * c; \
        float d = texelFetch(depth_tex, clamp(tc + ivec2(dx, dy), ivec2(0), size - 1), 0).r; \
        if (d < near_depth) { near_depth = d; near_off = ivec2(dx, dy); } \
    }
    TAP(-1, -1) TAP( 0, -1) TAP( 1, -1)
    TAP(-1,  0) TAP( 0,  0) TAP( 1,  0)
    TAP(-1,  1) TAP( 0,  1) TAP( 1,  1)
#undef TAP

    mu = m1 / H(9.0);
    sigma = sqrt(max(m2 / H(9.0) - mu * mu, HVEC3(0.0)));
}

/*
 * Clip color to the [mn, mx] box by pulling it toward the box center
 * instead of clamping per component: clamping projects onto box faces
 * and skews the hue, clipping preserves the direction of the change.
 * The epsilon keeps the division finite on a degenerate (flat) box.
 * t tells the caller how far outside the box the color was: ~1 for
 * legitimate sub-pixel edge information, much larger for stale
 * (disoccluded) history.
 */
f16vec3 clip_to_aabb(f16vec3 mn, f16vec3 mx, f16vec3 color, out float16_t t)
{
    f16vec3 center = (mx + mn) * H(0.5);
    f16vec3 extent = (mx - mn) * H(0.5) + H(1e-3);
    f16vec3 dist = color - center;
    f16vec3 ts = abs(dist) / extent;

    t = max(ts.x, max(ts.y, ts.z));

    return t > H(1.0) ? center + dist / t : color;
}

/*
 * Catmull-Rom history filter, 5 bilinear taps (the corner taps of the
 * full 9 carry ~4% of the energy and are dropped, cross renormalized):
 * the bicubic negative lobes undo the softening that plain bilinear
 * history resampling accumulates under camera motion. At a texel
 * center (static camera) the weights collapse to a single exact fetch,
 * preserving bit-stable convergence. Explicit LOD throughout: this is
 * called from a non-uniform branch (tint).
 */
f16vec3 sample_history(vec2 uv)
{
    vec2 size = vec2(textureSize(history_tex, 0));
    vec2 texel = 1.0 / size;

    vec2 pos = uv * size;
    vec2 center = floor(pos - 0.5) + 0.5;
    vec2 f = pos - center;
    vec2 f2 = f * f;
    vec2 f3 = f2 * f;

    /* Catmull-Rom weights (B = 0, C = 0.5) */
    vec2 w0 = f2 - 0.5 * (f3 + f);
    vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    vec2 w3 = 0.5 * (f3 - f2);
    vec2 w2 = 1.0 - w0 - w1 - w3;

    /* fold the two middle rows/columns into one bilinear tap each */
    vec2 w12 = w1 + w2;
    vec2 tc0 = (center - 1.0) * texel;
    vec2 tc12 = (center + w2 / w12) * texel;
    vec2 tc3 = (center + 2.0) * texel;

    vec3 history =
        textureLod(history_tex, vec2(tc0.x,  tc12.y), 0.0).rgb * (w0.x  * w12.y) +
        textureLod(history_tex, vec2(tc12.x, tc0.y),  0.0).rgb * (w12.x * w0.y)  +
        textureLod(history_tex, vec2(tc12.x, tc12.y), 0.0).rgb * (w12.x * w12.y) +
        textureLod(history_tex, vec2(tc12.x, tc3.y),  0.0).rgb * (w12.x * w3.y)  +
        textureLod(history_tex, vec2(tc3.x,  tc12.y), 0.0).rgb * (w3.x  * w12.y);
    float wsum = w0.x * w12.y + w12.x * w0.y + w12.x * w12.y +
                 w12.x * w3.y + w3.x * w12.y;

    /* the negative lobes can undershoot; clamp before the color transform */
    return HVEC3(rgb_to_ycocg(max(history / wsum, vec3(0.0))));
}

void main()
{
    /*
     * The current sample stays at its jittered position on purpose:
     * blending differently-jittered frames is what integrates edges.
     */
    f16vec3 current = HVEC3(rgb_to_ycocg(texture(model_tex, pass_tex).rgb));

    f16vec3 mu, sigma;
    ivec2 near_off;
    neighborhood(pass_tex, mu, sigma, near_off);

    /*
     * Motion source, dilated to the closest depth in the neighborhood
     * so that silhouette-crossing pixels follow the foreground object:
     * models write true object + camera motion into the velocity RT;
     * zero (the clear value, also what particles write) means "static
     * world", for which the camera reprojection below is exact. The
     * two paths agree bit-for-bit on static geometry, so the hybrid
     * seam is invisible.
     */
    ivec2 size = textureSize(depth_tex, 0);
    ivec2 tc = ivec2(pass_tex * vec2(size));
    vec2 vel = texelFetch(velocity_tex, clamp(tc + near_off, ivec2(0), size - 1), 0).rg;

    vec2 prev_uv;
    bool valid = !taa_reset;

    if (vel != vec2(0.0)) {
        /* velocity is a GL-orientation uv delta, unjittered both sides */
        prev_uv = convert_pass_tex(convert_pass_tex(pass_tex) - vel);
    } else {
        /*
         * Reproject: reconstruct the world position at the dilated
         * offset and find its pixel in the previous frame; the
         * neighbor's motion is transferred back to this pixel by
         * undoing the offset afterwards. The accumulated history
         * converges to the *unjittered* image, so jitter must cancel
         * out of the reprojection: prev_proj_view is unjittered, and
         * adding our own jitter re-aligns the result with this frame's
         * pixel grid. For a static camera prev_uv then equals pass_tex
         * exactly, texel-center aligned -- no bilinear resampling, no
         * progressive history blur.
         *
         * Where nothing was rendered there is no point to follow, only
         * a view direction: the background is at infinity and moves
         * with the camera's rotation alone, so reproject it as a
         * direction (w = 0) and let the translation drop out.
         *
         * History is unusable for points behind the previous camera.
         */
        vec2 texel = 1.0 / vec2(width, height);
        vec2 uv_near = pass_tex + vec2(near_off) * texel;
        float depth;
        vec3 view_pos = view_pos_from_depth(depth_tex, inverse_proj, uv_near, depth);
        vec4 world_pos = inverse_view * vec4(view_pos, depth < 1.0 ? 1.0 : 0.0);
        vec4 prev_clip = prev_proj_view * world_pos;
        vec2 prev_ndc = prev_clip.xy / prev_clip.w + jitter;

        prev_uv = convert_pass_tex(prev_ndc * 0.5 + 0.5) - vec2(near_off) * texel;
        valid = valid && prev_clip.w > 0.0;
    }

    /* off-screen reprojections have no history on either path */
    valid = valid &&
            all(greaterThanEqual(prev_uv, vec2(0.0))) &&
            all(lessThanEqual(prev_uv, vec2(1.0)));

    if (valid) {
        /*
         * Variance clip (Salvi): mu +/- gamma * sigma bounds much
         * tighter than the min/max box where the neighborhood
         * straddles a silhouette, so disoccluded history (e.g. a face
         * ghost on a freshly revealed wall) dies in a frame or two
         */
        const float16_t var_gamma = H(1.0);

        f16vec3 history = sample_history(prev_uv);
        float16_t t;
        history = clip_to_aabb(mu - sigma * var_gamma, mu + sigma * var_gamma, history, t);

        /*
         * Disocclusion response: history far outside the box (wide
         * bands of freshly revealed background during camera motion)
         * would take ~1/feedback frames to fade as a smudge; snap the
         * blend toward the current sample instead. t <= 1 (converged
         * or normal edge detail) keeps the configured feedback.
         */
        float16_t w = mix(H(taa_feedback), H(1.0),
                          clamp((t - H(1.0)) * H(0.5), H(0.0), H(1.0)));

        current = mix(history, current, w);
    }

    FragColor = vec4(ycocg_to_rgb(current), 1.0);
}
