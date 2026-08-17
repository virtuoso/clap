#version 460 core

#include "shader_constants.h"
#include "color-utils.glsl"
#include "pass-tex.glsl"
#include "view_pos.glsl"
#include "downsample.glsl"
#include "bloom-level.glsl"

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
     * Carry the emitters' distance along the chain, not the receiving
     * pixel's: the halo lands on the background, whose depth is not the
     * emitter's. It goes as the pyramid level the emitter wants, see
     * bloom-level.glsl: full size (the coarsest level) up to
     * bloom_distance, one level down per doubling of distance beyond
     * it, i.e. a halo radius falling as 1/distance until the finest
     * level. 0 disables the scaling; where nothing was rendered the
     * depth reconstructs to the far plane, as far as it gets.
     */
    vec3 view_pos = view_pos_from_depth(depth_tex, inverse_proj, uv);
    float dist = max(-view_pos.z, 1e-3);
    float nearness = bloom_distance > 0.0 ? min(bloom_distance / dist, 1.0) : 1.0;
    float rel = clamp(BLOOM_LEVEL_RANGE + log2(max(nearness, 1e-6)), 0.0, BLOOM_LEVEL_RANGE);

    FragColor = vec4(color.rgb, bloom_level_encode(luma(color.rgb), rel));
}
