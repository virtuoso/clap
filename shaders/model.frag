#version 460 core

#include "shader_constants.h"
#include "texel_fetch.glsl"
#include "color-utils.glsl"

layout (location=0) in vec2 pass_tex;
layout (location=1) in vec3 surface_normal;
layout (location=2) in vec3 orig_normal;
layout (location=3) in vec3 to_camera_vector;
layout (location=4) in vec4 world_pos;
#ifdef SHADER_VARIANT_NORMALS
layout (location=5) in mat3 tbn;
/* the mat3 above occupies locations 5-7 */
#endif /* SHADER_VARIANT_NORMALS */
#ifdef SHADER_VARIANT_TAA
layout (location=8) in vec4 pass_curr_clip;
layout (location=9) in vec4 pass_prev_clip;
#endif /* SHADER_VARIANT_TAA */

#include "ubo_projview.glsl"
#include "ubo_transform.glsl"
#include "ubo_render_common.glsl"
#include "ubo_outline.glsl"
#include "ubo_bloom.glsl"
#include "ubo_frame.glsl"

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
/*
 * Variant rule: a resource is declared iff the variant's code samples it.
 * WebGPU's auto pipeline layout only contains statically-used bindings,
 * so a declared-but-unused resource desyncs the reflection-driven bind
 * groups; unused declarations also waste v3dv descriptors.
 */
#ifdef SHADER_VARIANT_NORMALS
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2D normal_map;
#endif /* SHADER_VARIANT_NORMALS */
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;
layout (binding=SAMPLER_BINDING_light_map) uniform usampler2D light_map;
layout (binding=SAMPLER_BINDING_noise3d) uniform sampler3D noise3d;

#include "shadow.glsl"
#include "lighting.glsl"

layout (location=RT_MODEL_LIGHTING)     out vec4 FragColor;
layout (location=RT_MODEL_EMISSION)     out vec4 EmissiveColor;
layout (location=RT_MODEL_VIEW_NORMALS) out vec4 Normal;
#ifdef SHADER_VARIANT_TAA
layout (location=RT_MODEL_VELOCITY)     out vec2 Velocity;
#endif /* SHADER_VARIANT_TAA */

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
#ifdef SHADER_VARIANT_NORMALS
    if (use_normals) {
        vec3 normal_sample = texture(normal_map, uv).xyz * 2.0 - 1.0;
        unit_normal = normalize(tbn * normal_sample);
    } else
#endif /* SHADER_VARIANT_NORMALS */
    {
        unit_normal = geom_normal;
    }

#ifdef SHADER_VARIANT_NOISE_MATERIALS
    /* the material's own drift; zero keeps the surface static */
    vec3 noise_ofs = noise_drift * uptime;

    if (use_noise_normals == NOISE_NORMALS_GPU)
        unit_normal = noise_normal_gpu(world_pos.xyz + noise_ofs, unit_normal,
                                       noise_normals_amp, noise_normals_scale);
    else if (use_noise_normals == NOISE_NORMALS_3D)
        unit_normal = noise_normal_3d(noise3d, world_pos.xyz, noise_ofs, unit_normal,
                                      noise_normals_amp, noise_normals_scale);
#endif /* SHADER_VARIANT_NOISE_MATERIALS */

    lighting_material mat = noise_material();

    vec3 view_dir = normalize(to_camera_vector);
    vec4 texture_sample = texture(model_tex, uv);
    vec4 view_pos = view * world_pos;

#ifdef SHADER_VARIANT_SHADOWS
    /*
     * The shadow light's direction for the facing test and slope bias:
     * a sun-like stores its propagation direction, but a spotlight in
     * the shadow slot is positional -- its stored dir points at the
     * light -- so derive the per-fragment propagation instead
     */
    vec3 shadow_dir = light_is_spotlight(0) ?
        vec3(world_pos) - light_pos[0] : light_dir[0];
    float shadow_factor = shadow_factor_calc(geom_normal, view_pos, shadow_dir, shadow_vsm, use_msaa);
#else
    float shadow_factor = 1.0;
#endif /* !SHADER_VARIANT_SHADOWS */

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

#ifdef SHADER_VARIANT_LIGHT_HEATMAP
    /*
     * An overlay rather than a replacement: the lit surface stays
     * readable under the tiles, and every sampler the lighting path
     * declares stays statically used (see the variant rule above)
     */
    FragColor.rgb = mix(FragColor.rgb, light_heatmap(), 0.75);
#endif /* SHADER_VARIANT_LIGHT_HEATMAP */

    vec3 emission;
#ifdef SHADER_VARIANT_NOISE_MATERIALS
    if (use_noise_emission) {
        /*
         * Tangential fBm-gradient glow: sample the baked 3D noise at a
         * jittered local-space position, moved by the drift after the
         * jitter, take the component tangential to the surface normal
         * and map its magnitude to a cyan glow.
         */
        vec3 local_pos = (inverse_trs * world_pos).xyz;
        float ln = noise(local_pos);
        vec3 grad = sample_noise3d(noise3d, local_pos * ln + noise_ofs, noise_normals_scale);
        vec3 t = grad - unit_normal * dot(normalize(grad), unit_normal);
        float fac = clamp(length(t), 0.0, 1.0);
        emission = noise_emission_color * min(pow(fac, 0.3), 1.0);
    } else
#endif /* SHADER_VARIANT_NOISE_MATERIALS */
    {
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
            /*
             * The luma of the normal as the Normal RT encodes it, inside
             * (0, 1): the view space normal's own goes negative with the
             * camera's pitch and yaw, where uint() is undefined, and a
             * packed 0 reads as unshadowed in the edge filter
             */
            float luma = luma(view_normal * 0.5 + 0.5);
            final_edge_mode = (uint(luma * EDGE_LUMA_MAX) << (EDGE_SOLID_LUMA_OFFSET));
        }
    }

    Normal.a = float(final_edge_mode & 0xff) / 255.0;

#ifdef SHADER_VARIANT_TAA
    /*
     * Unjittered NDC motion, halved to uv scale: gl_Position carried the
     * jittered projection while the TAA history lives in unjittered space
     */
    vec2 curr_ndc = pass_curr_clip.xy / pass_curr_clip.w - jitter;
    vec2 prev_ndc = pass_prev_clip.xy / pass_prev_clip.w;
    Velocity = use_velocity ? (curr_ndc - prev_ndc) * 0.5 : vec2(0.0);
#endif /* SHADER_VARIANT_TAA */

    /*
     * Fully transparent fragments must not touch any RT or advance the depth
     * buffer: the normals RT now opts out of blending (fbo_attconfig.no_blend),
     * so without discard we'd stamp an unrelated view-normal over what's
     * behind and poison SSAO/sobel. Discard is at the end of main() so all
     * textureSample() calls above stay in uniform control flow (Tint/WGSL).
     */
#ifdef SHADER_VARIANT_ALPHA_DISCARD
    if (texture_sample.a < 1e-3)    discard;
#endif /* SHADER_VARIANT_ALPHA_DISCARD */
}
