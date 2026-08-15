#version 460 core

#include "shader_constants.h"
#include "color-utils.glsl"
#include "pass-tex.glsl"
#include "view_pos.glsl"
#include "downsample.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_depth_tex) uniform sampler2D depth_tex;

#include "ubo_projview.glsl"
#include "ubo_render_common.glsl"
#include "ubo_bloom.glsl"

void main()
{
    /*
     * The emission raster is displaced by the TAA jitter; sample at the
     * displaced position to read temporally stable unjittered content,
     * otherwise bloom pulses whenever a small emitter crosses a
     * quarter-res texel. jitter is in NDC units, the uv delta is half
     * that, orientation handled by the self-inverse convert_pass_tex().
     * use_taa rather than a jitter test: the jitter uniforms go stale,
     * not zero, when TAA is toggled off.
     */
    vec2 uv = use_taa ?
        convert_pass_tex(convert_pass_tex(pass_tex) + jitter * 0.5) :
        pass_tex;

    vec4 color = downsample_5tap(model_tex, uv);

    /*
     * Carry the emitter's distance to combine_color(), premultiplied by
     * luma, in place of the emission RT's blend alpha, which nothing
     * downstream of here reads. The blur chain is linear, so that alpha
     * divided by the luma of the color beside it recovers the
     * luma-weighted mean distance of the emitters that reached a halo
     * texel, which is what the halo's radius depends on. Sampling depth
     * at the receiving pixel instead would read whatever the halo lands
     * on -- for an emitter against a distant background, the background.
     * Normalized, so alpha stays inside the range the color channels
     * already hold; the sky sentinel (view_pos.z > 0) reads as
     * maximally distant.
     */
    vec3 view_pos = view_pos_from_depth(depth_tex, inverse_proj, uv);
    float dist = view_pos.z > 0.0 ? bloom_distance : -view_pos.z;
    float depth_norm = bloom_distance > 0.0 ? clamp(dist / bloom_distance, 0.0, 1.0) : 0.0;

    FragColor = vec4(color.rgb, luma(color.rgb) * depth_norm);
}
