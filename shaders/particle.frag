#version 460 core

#include "shader_constants.h"

#include "ubo_projview.glsl"
#include "ubo_render_common.glsl"
#include "ubo_outline.glsl"
#include "ubo_bloom.glsl"
#include "ubo_postproc.glsl"
#include "ubo_shadow.glsl"

layout (location=0) in vec2 pass_tex;
layout (location=1) in vec3 surface_normal;
layout (location=2) in vec3 to_camera_vector;
layout (location=3) in vec4 world_pos;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;
layout (binding=SAMPLER_BINDING_light_map) uniform usampler2D light_map;
layout (binding=SAMPLER_BINDING_noise3d) uniform sampler3D noise3d;

/*
 * lighting.glsl references tbn inside a use_normals branch. Particle
 * materials never have a normal map, so use_normals is always 0 at
 * runtime and that branch is dead — but the compiler still needs the
 * symbol to resolve.
 */
mat3 tbn = mat3(1.0);

#include "lighting.glsl"

layout (location=RT_MODEL_LIGHTING)     out vec4 FragColor;
layout (location=RT_MODEL_EMISSION)     out vec4 EmissiveColor;
layout (location=RT_MODEL_VIEW_NORMALS) out vec4 Normal;

void main()
{
    vec4 texture_sample = texture(model_tex, pass_tex);

    if (use_3d_fog) {
        /*
         * Fog-cloud particles: run the full lighting path so the
         * cloud picks up ambient + per-light contributions, and let
         * compute_total_lighting()'s 3D fog branch blend it toward
         * the ambient based on noise3d density. Shadow factor is
         * pinned to 1.0 — billboards don't need self-shadowing.
         */
        vec3 unit_normal = normalize(surface_normal);
        vec3 view_dir = normalize(to_camera_vector);
        lighting_material mat = noise_material();
        lighting_result r = compute_total_lighting(unit_normal, view_dir, texture_sample.rgb,
                                                   1.0, mat, noise3d);

        FragColor = vec4(r.diffuse, 1.0) * texture_sample + vec4(r.specular, 0.0);
        EmissiveColor = vec4(0.0);
    } else {
        /*
         * Emissive billboard path: the common case for sparkle /
         * ember / dust particles. Keep the base color unlit so the
         * particle reads at full intensity, and route emission
         * through the same bloom_intensity/threshold pipeline
         * model.frag uses.
         */
        FragColor = vec4(texture_sample.rgb, 1.0);

        vec3 emission = bloom_intensity > 0.0 ? texture(emission_map, pass_tex).rgb : texture_sample.rgb;
        emission = max(emission - bloom_threshold, vec3(0.0)) * abs(bloom_intensity);
        EmissiveColor = vec4(use_hdr ? emission : min(emission, vec3(1.0)), 0.0);
    }

    /*
     * View-space normal + edge_mode packed into alpha, matching
     * model.frag's output for RT_MODEL_VIEW_NORMALS so the edge
     * filter sees a consistent encoding. outline_exclude gets
     * folded into edge_mode upstream in _models_render().
     */
    vec3 view_normal = mat3(view) * surface_normal;
    Normal = vec4(view_normal * 0.5 + 0.5, float(edge_mode & 0xff) / 255.0);
}
