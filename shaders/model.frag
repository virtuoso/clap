#version 460 core

#include "config.h"
#include "shader_constants.h"
#include "texel_fetch.glsl"

layout (location=0) flat in int do_use_normals;
layout (location=1) in vec2 pass_tex;
layout (location=2) in vec3 surface_normal;
layout (location=3) in vec3 orig_normal;
layout (location=4) in vec3 to_light_vector[LIGHTS_MAX];
layout (location=8) in vec3 to_camera_vector;
layout (location=9) in vec4 world_pos;

#include "shadow.glsl"
#include "lighting.glsl"

layout (std140, binding = UBO_BINDING_projview) uniform projview {
    mat4 proj;
    mat4 view;
    mat4 inverse_view;
};

uniform sampler2D model_tex;
uniform sampler2D normal_map;
uniform sampler2D emission_map;

uniform bool shadow_vsm; /* XXX: move to shadow UBO */

uniform bool sobel_solid;
uniform float sobel_solid_id;
uniform bool use_msaa;
uniform bool outline_exclude;
uniform bool use_hdr;
uniform float bloom_scene;
uniform float bloom_threshold;
uniform float bloom_intensity;

layout (location=0) out vec4 FragColor;
layout (location=1) out vec4 EmissiveColor;
layout (location=2) out vec4 EdgeNormal;
layout (location=3) out float EdgeDepthMask;
layout (location=4) out vec4 ViewPosition;
layout (location=5) out vec4 Normal;

void main()
{
    vec3 normal_vec, unit_normal;

    if (do_use_normals > 0.5) {
        normal_vec = texture(normal_map, pass_tex).xyz * 2.0 - 1.0;
        unit_normal = normalize(normal_vec.xyz);
    } else {
        normal_vec = surface_normal;
        unit_normal = normalize(surface_normal);
    }

    vec3 view_dir = normalize(to_camera_vector);
    vec4 texture_sample = texture(model_tex, pass_tex);
    vec4 view_pos = view * world_pos;

    float shadow_factor = shadow_factor_calc(unit_normal, view_pos, light_dir[0], shadow_vsm, use_msaa);

    lighting_result r = compute_total_lighting(unit_normal, to_light_vector, view_dir, texture_sample.rgb);

    vec3 shadow_tint = light_color[0] * vec3(0.4, 0.3, 0.3); /* XXX: parameterize me */
    r.diffuse = mix(r.diffuse, shadow_tint, 1.0 - shadow_factor);

    FragColor = vec4(r.diffuse, 1.0) * texture_sample + vec4(r.specular, 1.0);
    EdgeDepthMask = gl_FragCoord.z;

    vec3 emission = bloom_intensity > 0.0 ? texture(emission_map, pass_tex).rgb : texture_sample.rgb;
    emission = max(emission - bloom_threshold, vec3(0.0)) * abs(bloom_intensity);
    EmissiveColor = vec4(use_hdr ? emission : min(emission, vec3(1.0)), 1.0);
    ViewPosition = view_pos;

    vec3 world_normal = mat3(view) * normal_vec;
    Normal = vec4(world_normal * 0.5 + 0.5, 1.0);

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
