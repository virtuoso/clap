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

layout (std140, binding = UBO_BINDING_lighting) uniform lighting {
    vec3 light_pos[LIGHTS_MAX];
    vec3 light_color[LIGHTS_MAX];
    vec3 light_dir[LIGHTS_MAX];
    vec3 attenuation[LIGHTS_MAX];
};

layout (std140, binding = UBO_BINDING_shadow) uniform shadow {
    mat4 shadow_mvp[CASCADES_MAX];
    float cascade_distances[CASCADES_MAX];
    bool shadow_outline;
    float shadow_outline_threshold;
};

layout (std140, binding = UBO_BINDING_projview) uniform projview {
    mat4 proj;
    mat4 view;
    mat4 inverse_view;
};

uniform sampler2D model_tex;
uniform sampler2D normal_map;
uniform sampler2D emission_map;
#ifdef CONFIG_GLES
uniform sampler2D shadow_map;
uniform sampler2D shadow_map1;
uniform sampler2D shadow_map2;
uniform sampler2D shadow_map3;
#else
uniform sampler2DArray shadow_map;
uniform sampler2DMSArray shadow_map_ms;
#endif /* CONFIG_GLES */
uniform bool shadow_vsm; /* XXX: move to shadow UBO */
uniform float shine_damper;
uniform float reflectivity;
uniform bool sobel_solid;
uniform float sobel_solid_id;
uniform vec4 highlight_color;
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

float shadow_factor_pcf(in sampler2DArray map, in vec4 pos, in int layer, in float bias)
{
    const int pcf_count = 2;
    const float pcf_total_texels = pow(float(pcf_count) * 2.0 + 1.0, 2.0);
    float total = 0.0;

    for (int x = -pcf_count; x <= pcf_count; x++)
        for (int y = -pcf_count; y <= pcf_count; y++) {
            float shadow_distance = texel_fetch_2darray(map, vec3(pos.xy, float(layer)), ivec2(x, y)).r;
            if (pos.z + bias < shadow_distance)
                total += 1.0;
        }

    return 1.0 - total / pcf_total_texels * pos.w;
}

float shadow_factor_pcf(in sampler2D map, in vec4 pos, in float bias)
{
    const int pcf_count = 2;
    const float pcf_total_texels = pow(float(pcf_count) * 2.0 + 1.0, 2.0);
    float total = 0.0;

    for (int x = -pcf_count; x <= pcf_count; x++)
        for (int y = -pcf_count; y <= pcf_count; y++) {
            float shadow_distance = texel_fetch_2d(map, pos.xy, ivec2(x, y)).r;
            if (pos.z + bias < shadow_distance)
                total += 1.0;
        }

    return 1.0 - total / pcf_total_texels * pos.w;
}

#ifndef CONFIG_GLES
float shadow_factor_msaa(in sampler2DMSArray map, in vec4 pos, in int layer, in float bias)
{
    float total = 0.0;

    for (int i = 0; i < MSAA_SAMPLES; i++) {
        float shadow_distance = texel_fetch_2dms_array_sample(map, vec3(pos.xy, float(layer)), i).r;

        if (pos.z + bias < shadow_distance)
            total += 1.0;
    }
    total /= MSAA_SAMPLES;

    return 1.0 - total * pos.w;
}

float shadow_factor_msaa_weighted(in sampler2DMSArray map, in vec4 pos, in int layer, in float bias)
{
    float total = 0.0;
    float mean = 0.0, variance = 0.0;
    float samples[MSAA_SAMPLES];

    for (int i = 0; i < MSAA_SAMPLES; i++) {
        samples[i] = texel_fetch_2dms_array_sample(map, vec3(pos.xy, float(layer)), i).r;
        mean += samples[i];
    }
    mean /= MSAA_SAMPLES;

    for (int i = 0; i < MSAA_SAMPLES; i++) {
        variance += pow(samples[i] - mean, 2);
    }
    variance /= MSAA_SAMPLES;

    float adaptWeight = exp(-variance * 16.0); // Higher variance = more conservative filtering

    for (int i = 0; i < MSAA_SAMPLES; i++) {
        float weight = mix(1.0, adaptWeight, variance);
        if (pos.z + bias < samples[i]) total += weight;
    }
    total /= MSAA_SAMPLES;

    return 1.0 - total * pos.w;
}
#endif /* CONFIG_GLES */

float shadow_factor_vsm_calc(in vec2 moments, in float d)
{
    float m1 = moments.x;
    float m2 = moments.y;

    /* Early out if fully lit */
    if (d < m1)
        return 1.0;

    float distance = d - m1;
    float variance = max(m2 - m1 * m1, 1e-6);

    /* Chebyshev upper bound */
    float p_max = variance / (variance + distance * distance);

    const float light_bleed_reduction = 0.8; /* XXX: parameterize me */
    p_max = clamp((p_max - light_bleed_reduction) / (1.0 - light_bleed_reduction), 0.0, 1.0);

    return smoothstep(0.15, 0.95, clamp(p_max, 0.0, 1.0));
}

float shadow_factor_vsm(in sampler2D map, in vec4 pos)
{
    vec2 moments = texel_fetch_2d(map, pos.xy, ivec2(0)).rg;

    return shadow_factor_vsm_calc(moments, pos.z);
}

float shadow_factor_vsm(in sampler2DArray map, in vec4 pos, in int layer)
{
    vec2 moments = texel_fetch_2darray(map, vec3(pos.xy, float(layer)), ivec2(0)).rg;

    return shadow_factor_vsm_calc(moments, pos.z);
}

float shadow_factor_calc(in vec3 unit_normal, in vec4 view_pos)
{
    float shadow_factor = 1.0;

    float light_dot = dot(unit_normal, normalize(-light_dir[0]));
    if (light_dot < 0)
        return shadow_factor;

    int layer = -1;

    for (int i = 0; i < CASCADES_MAX; i++) {
        if (-view_pos.z < cascade_distances[i]) {
            layer = i;
            break;
        }
    }

    if (layer < 0)
        layer = CASCADES_MAX - 1;

    vec4 shadow_pos = shadow_mvp[layer] * world_pos;
    vec4 proj_coords = vec4(shadow_pos.xyz / shadow_pos.w, shadow_pos.w);
    proj_coords = proj_coords * 0.5 + 0.5;

    float bias = max(0.0005 * (1.0 - light_dot), 0.0008);
#ifdef CONFIG_GLES
    switch (layer) {
        case 0:
            shadow_factor = shadow_vsm ?
                shadow_factor_vsm(shadow_map, proj_coords) :
                shadow_factor_pcf(shadow_map, proj_coords, bias);
            break;
        case 1:
            shadow_factor = shadow_vsm ?
                shadow_factor_vsm(shadow_map1, proj_coords) :
                shadow_factor_pcf(shadow_map1, proj_coords, bias);
            break;
        case 2:
            shadow_factor = shadow_vsm ?
                shadow_factor_vsm(shadow_map2, proj_coords) :
                shadow_factor_pcf(shadow_map2, proj_coords, bias);
            break;
        default:
        case 3:
            shadow_factor = shadow_vsm ?
                shadow_factor_vsm(shadow_map3, proj_coords) :
                shadow_factor_pcf(shadow_map3, proj_coords, bias);
            break;
    }
#else
    if (use_msaa)
        shadow_factor = shadow_factor_msaa_weighted(shadow_map_ms, proj_coords, layer, bias);
    else
        shadow_factor = shadow_vsm ?
            shadow_factor_vsm(shadow_map, proj_coords, layer) :
            shadow_factor_pcf(shadow_map, proj_coords, layer, bias);
#endif /* CONFIG_GLES */

    return mix(shadow_factor, 1.0, pow(1 - light_dot, 1.3));
}

void main()
{
    if (highlight_color.w != 0.0) {
        FragColor = highlight_color;
        return;
    }

    vec3 normal_vec, unit_normal;

    if (do_use_normals > 0.5) {
        normal_vec = texture(normal_map, pass_tex).xyz * 2.0 - 1.0;
        unit_normal = normalize(normal_vec.xyz);
    } else {
        normal_vec = surface_normal;
        unit_normal = normalize(surface_normal);
    }

    vec3 unit_to_camera_vector = normalize(to_camera_vector);
    vec4 texture_sample = texture(model_tex, pass_tex);
    vec4 view_pos = view * world_pos;

    float shadow_factor = shadow_factor_calc(unit_normal, view_pos);

    vec3 total_diffuse = vec3(0.0);
    vec3 total_specular = vec3(0.0);

    for (int i = 0; i < LIGHTS_MAX; i++) {
        float distance = length(to_light_vector[i]);
        float att_fac = attenuation[i].x + (attenuation[i].y * distance) + (attenuation[i].z * distance * distance);
        vec3 unit_to_light_vector = normalize(to_light_vector[i]);
        float n_dot1 = dot(unit_normal, unit_to_light_vector);
        float brightness = max(n_dot1, 0.0);
        vec3 light_direction = -unit_to_light_vector;
        vec3 reflected_light_direction = reflect(light_direction, unit_normal);
        float specular_factor = dot(reflected_light_direction, unit_to_camera_vector);
        specular_factor = max(specular_factor, 0.2);
        float damped_factor = pow(specular_factor, shine_damper);
        total_specular = total_specular + damped_factor * reflectivity * light_color[i] / att_fac;
        total_diffuse = total_diffuse + brightness * light_color[i] / att_fac;
    }

    vec3 shadow_tint = light_color[0] * vec3(0.4, 0.3, 0.3); /* XXX: parameterize me */
    total_diffuse = mix(max(total_diffuse, 0.2), shadow_tint, 1.0 - shadow_factor);

    FragColor = vec4(total_diffuse, 1.0) * texture_sample + vec4(total_specular, 1.0);
    EdgeDepthMask = gl_FragCoord.z;

    vec3 emission = texture(emission_map, pass_tex).rgb;
    emission = max(emission - bloom_threshold, vec3(0.0)) * bloom_intensity;
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
