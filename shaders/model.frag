#version 460 core

#include "config.h"
#include "shader_constants.h"
#include "texel_fetch.inc"

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
uniform float shine_damper;
uniform float reflectivity;
uniform bool sobel_solid;
uniform int sobel_solid_id;
uniform vec4 highlight_color;
uniform bool use_msaa;

layout (location=0) out vec4 FragColor;
layout (location=1) out vec4 EmissiveColor;
layout (location=2) out vec4 Albedo;

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
#endif /* CONFIG_GLES */

float shadow_factor_calc(in vec3 unit_normal)
{
    float shadow_factor = 1.0;

    float light_dot = dot(unit_normal, normalize(-light_dir[0]));
    vec4 view_pos = view * world_pos;
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
            shadow_factor = shadow_factor_pcf(shadow_map, proj_coords, bias);
            break;
        case 1:
            shadow_factor = shadow_factor_pcf(shadow_map1, proj_coords, bias);
            break;
        case 2:
            shadow_factor = shadow_factor_pcf(shadow_map2, proj_coords, bias);
            break;
        default:
        case 3:
            shadow_factor = shadow_factor_pcf(shadow_map3, proj_coords, bias);
            break;
    }
#else
    if (use_msaa)
        shadow_factor = shadow_factor_msaa(shadow_map_ms, proj_coords, layer, bias);
    else
        shadow_factor = shadow_factor_pcf(shadow_map, proj_coords, layer, bias);
#endif /* CONFIG_GLES */

    return mix(shadow_factor, 1.0, pow(1 - light_dot, 1.3));
}

void main()
{
    if (highlight_color.w != 0.0) {
        FragColor = highlight_color;
        return;
    }

    vec3 unit_normal;

    if (do_use_normals > 0.5) {
        vec4 normal_vec = texture(normal_map, pass_tex) * 2.0 - 1.0;
        unit_normal = normalize(normal_vec.xyz);
    } else {
        unit_normal = normalize(surface_normal);
    }

    vec3 unit_to_camera_vector = normalize(to_camera_vector);
    vec4 texture_sample = texture(model_tex, pass_tex);
    EmissiveColor = texture(emission_map, pass_tex);

    float shadow_factor = shadow_factor_calc(unit_normal);

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

    total_diffuse = max(total_diffuse, 0.2) * shadow_factor;
    FragColor = vec4(total_diffuse, 1.0) * texture_sample + vec4(total_specular, 1.0);

    if (sobel_solid) {
        Albedo = vec4((normalize(cross(texture_sample.rgb, vec3(
            abs(fract(cos(mod(float(sobel_solid_id), 3.1415926)))),
            abs(fract(sin(mod(float(sobel_solid_id), 3.1415926)))),
            abs(fract(log(float(sobel_solid_id))))
        ))) + vec3(1.0, 1.0, 1.0) / 2.0), 1.0);
    } else {
        vec3 pos_normal = (normalize(orig_normal) + vec3(1.0, 1.0, 1.0)) / 2.0;
        Albedo = vec4(pos_normal * (shadow_outline ? shadow_factor : 1.0), 1.0);
    }
}
