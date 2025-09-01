#version 460 core

#include "config.h"
#include "shader_constants.h"
#include "texel_fetch.glsl"

layout (location=0) in vec2 pass_tex;
layout (location=1) in vec3 surface_normal;
layout (location=2) in vec3 orig_normal;
layout (location=3) in vec3 to_camera_vector;
layout (location=4) in vec4 world_pos;
layout (location=5) in mat3 tbn;

#include "shadow.glsl"
#include "lighting.glsl"
#include "ubo_projview.glsl"
#include "ubo_render_common.glsl"
#include "ubo_outline.glsl"
#include "ubo_bloom.glsl"

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2D normal_map;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;

layout (location=0) out vec4 FragColor;
layout (location=1) out vec4 EmissiveColor;
layout (location=2) out vec4 EdgeNormal;
layout (location=3) out float EdgeDepthMask;
layout (location=4) out vec4 ViewPosition;
layout (location=5) out vec4 Normal;

void main()
{
    vec3 unit_normal;

    if (use_normals) {
        vec3 normal_sample = texture(normal_map, pass_tex).xyz * 2.0 - 1.0;
        unit_normal = normalize(tbn * normal_sample);
    } else {
        unit_normal = normalize(surface_normal);
    }

    vec3 view_dir = normalize(to_camera_vector);
    vec4 texture_sample = texture(model_tex, pass_tex);
    vec4 view_pos = view * world_pos;

    float shadow_factor = shadow_factor_calc(unit_normal, view_pos, light_dir[0], shadow_vsm, use_msaa);

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
    lighting_result r = compute_total_lighting(unit_normal, view_dir, texture_sample.rgb, shadow_factor);

    FragColor = vec4(r.diffuse, 1.0) * texture_sample + vec4(r.specular, 1.0);
    EdgeDepthMask = gl_FragCoord.z;

    vec3 emission = bloom_intensity > 0.0 ? texture(emission_map, pass_tex).rgb : texture_sample.rgb;
    emission = max(emission - bloom_threshold, vec3(0.0)) * abs(bloom_intensity);
    EmissiveColor = vec4(use_hdr ? emission : min(emission, vec3(1.0)), 1.0);
    ViewPosition = view_pos;

    vec3 view_normal = mat3(view) * orig_normal;
    Normal = vec4(view_normal * 0.5 + 0.5, 1.0);

    if (sobel_solid) {
        EdgeNormal = vec4(texture_sample.rgb, sobel_solid_id);
    } else if (outline_exclude) {
        EdgeNormal = vec4(vec3(0.0), 1.0);
        EdgeDepthMask = 1.0;
    } else {
        vec3 pos_normal = (normalize(orig_normal) + vec3(1.0, 1.0, 1.0)) / 2.0;
        EdgeNormal = vec4(pos_normal * (shadow_outline && shadow_factor < shadow_outline_threshold ? 0.0 : 1.0), 1.0);
    }
}
