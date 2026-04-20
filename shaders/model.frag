#version 460 core

#include "shader_constants.h"
#include "texel_fetch.glsl"
#include "color-utils.glsl"

layout (location=0) in vec2 pass_tex;
layout (location=1) in vec3 surface_normal;
layout (location=2) in vec3 orig_normal;
layout (location=3) in vec3 to_camera_vector;
layout (location=4) in vec4 world_pos;
layout (location=5) in mat3 tbn;

#include "ubo_projview.glsl"
#include "ubo_render_common.glsl"
#include "ubo_outline.glsl"
#include "ubo_bloom.glsl"
#include "ubo_postproc.glsl"

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2D normal_map;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;
layout (binding=SAMPLER_BINDING_light_map) uniform usampler2D light_map;
layout (binding=SAMPLER_BINDING_noise3d) uniform sampler3D noise3d;

#include "shadow.glsl"
#include "lighting.glsl"

layout (location=RT_MODEL_LIGHTING)     out vec4 FragColor;
layout (location=RT_MODEL_EMISSION)     out vec4 EmissiveColor;
layout (location=RT_MODEL_VIEW_NORMALS) out vec4 Normal;

void main()
{
    /*
     * Keep the geometric (unperturbed) normal around for shadow sampling:
     * tangent-space normal maps and noise-based perturbations both wiggle
     * the shading normal, which would make shadow_factor_calc()'s
     * light_dot-based early-out and slope bias wiggle with it and produce
     * flicker on near-terminator fragments when the camera moves.
     */
    vec3 geom_normal = normalize(surface_normal);
    vec3 unit_normal;

    vec2 uv = pass_tex * uv_factor;
    if (use_normals) {
        vec3 normal_sample = texture(normal_map, uv).xyz * 2.0 - 1.0;
        unit_normal = normalize(tbn * normal_sample);
    } else {
        unit_normal = geom_normal;
    }

    if (use_noise_normals == NOISE_NORMALS_GPU)
        unit_normal = noise_normal_gpu(world_pos.xyz + vec3(noise_shift), unit_normal,
                                       noise_normals_amp, noise_normals_scale);
    else if (use_noise_normals == NOISE_NORMALS_3D)
        unit_normal = noise_normal_3d(noise3d, world_pos.xyz + vec3(noise_shift), unit_normal,
                                      noise_normals_amp, noise_normals_scale);

    lighting_material mat = noise_material();

    vec3 view_dir = normalize(to_camera_vector);
    vec4 texture_sample = texture(model_tex, uv);
    vec4 view_pos = view * world_pos;

    float shadow_factor = shadow_factor_calc(geom_normal, view_pos, light_dir[0], shadow_vsm, use_msaa);

    /*
     * XXX: Both diffuse and spcular components are affected by the shadow_factor;
     * the problem is that shadow_factor is only derived from the light source 0,
     * whereas lighting_result r is sum total of diffuse and specular influences
     * from all light sources. IOW, shadow_factor_calc() will have to move inside
     * compute_total_lighting() and shadow UBO will likely become an extension of
     * lighting UBO.
     *
     * *However*, as it stands today, there is only one shadow casting light source
     * and changing that would involve VRAM footprint and GPU performance hit that
     * we just don't need yet. Once this becomes less critical, the above change
     * will have to be made.
     */
    lighting_result r = compute_total_lighting(unit_normal, view_dir, texture_sample.rgb, shadow_factor, mat, noise3d);

    FragColor = vec4(r.diffuse, 1.0) * texture_sample + vec4(r.specular, 1.0);
    /* Carry the source alpha into the blend factor for semi-transparent fragments */
    FragColor.a = texture_sample.a;

    vec3 emission;
    if (use_noise_emission) {
        /*
         * Tangential fBm-gradient glow: sample the baked 3D noise at a
         * jittered local-space position, take the component tangential
         * to the surface normal and map its magnitude to a cyan glow.
         */
        vec3 pos = world_pos.xyz + vec3(noise_shift);
        vec3 grad = sample_noise3d(noise3d, pos.xyz * noise(pos.xyz), noise_normals_scale);
        vec3 t = grad - unit_normal * dot(normalize(grad), unit_normal);
        float fac = clamp(length(t), 0.0, 1.0);
        emission = noise_emission_color * min(pow(fac, 0.3), 1.0);
    } else {
        emission = bloom_intensity > 0.0 ? texture(emission_map, uv).rgb : texture_sample.rgb;
    }
    emission = max(emission - bloom_threshold, vec3(0.0)) * abs(bloom_intensity);

    /*
     * Emission RT alpha drives SRC_ALPHA blending so the glow trail behind a
     * transparent model comes through; fully-opaque fragments still overwrite.
     */
    EmissiveColor = vec4(use_hdr ? emission : min(emission, vec3(1.0)), texture_sample.a);

    /* surface_normal is in world space */
    vec3 view_normal = mat3(view) * surface_normal;
    Normal = vec4(view_normal * 0.5 + 0.5, 0.0);

    uint final_edge_mode = edge_mode;
    if ((edge_mode & EDGE_SOLID_MASK) != 0) {
        float luma = luma(texture_sample.rgb);
        final_edge_mode |= (uint(luma * EDGE_LUMA_MAX) << (EDGE_SOLID_LUMA_OFFSET));
    }

    if (shadow_outline && shadow_factor > shadow_outline_threshold) {
        if ((edge_mode & EDGE_SOLID_MASK) != 0) {
            final_edge_mode ^= EDGE_LUMA_MAX << EDGE_SOLID_LUMA_OFFSET;
        } else {
            float luma = luma(view_normal.xyz);
            final_edge_mode = (uint(luma * EDGE_LUMA_MAX) << (EDGE_SOLID_LUMA_OFFSET));
        }
    }

    Normal.a = float(final_edge_mode & 0xff) / 255.0;

    /*
     * Fully transparent fragments must not touch any RT or advance the depth
     * buffer: the normals RT now opts out of blending (fbo_attconfig.no_blend),
     * so without discard we'd stamp an unrelated view-normal over what's
     * behind and poison SSAO/sobel. Discard is at the end of main() so all
     * textureSample() calls above stay in uniform control flow (Tint/WGSL).
     */
    if (texture_sample.a < 1e-3)    discard;
}
