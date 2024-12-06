#version 460 core

#include "config.h"

layout (location=0) flat in int do_use_normals;
layout (location=1) in vec2 pass_tex;
layout (location=2) in vec3 surface_normal;
layout (location=3) in vec3 orig_normal;
layout (location=4) in vec3 to_light_vector[4];
layout (location=8) in vec3 to_camera_vector;
layout (location=9) in vec4 shadow_pos;

uniform sampler2D model_tex;
uniform sampler2D normal_map;
uniform sampler2D emission_map;
#ifdef CONFIG_GLES
uniform sampler2D shadow_map;
#else
uniform sampler2DMSArray shadow_map;
#endif /* CONFIG_GLES */
uniform vec3 light_color[4];
uniform vec3 attenuation[4];
uniform float shine_damper;
uniform float reflectivity;
uniform bool albedo_texture;
uniform int entity_hash;
uniform vec4 highlight_color;
uniform vec3 light_dir[4];
uniform bool shadow_outline;

layout (location=0) out vec4 FragColor;
layout (location=1) out vec4 EmissiveColor;
layout (location=2) out vec4 Albedo;

float shadow_factor_pcf(in sampler2DArray map, in vec4 pos, in float bias)
{
    const int pcf_count = 2;
    const float pcf_total_texels = pow(float(pcf_count) * 2.0 + 1.0, 2.0);
    float texel_size = 1.0 / float(textureSize(map, 0).x);
    float shadow_distance = 0;
    float total = 0.0;

    for (int x = -pcf_count; x < pcf_count; x++)
        for (int y = -pcf_count; y < pcf_count; y++) {
            float shadow_distance = texture(map, vec3(pos.xy + vec2(float(x), float(y)) * texel_size, 0.0)).r;
            if (pos.z - bias > shadow_distance)
                total += 1.0;
        }

    return 1.0 - total / pcf_total_texels * pos.w;
}

float shadow_factor_pcf(in sampler2D map, in vec4 pos, in float bias)
{
    const int pcf_count = 2;
    const float pcf_total_texels = pow(float(pcf_count) * 2.0 + 1.0, 2.0);
    float texel_size = 1.0 / float(textureSize(map, 0).x);
    float shadow_distance = 0;
    float total = 0.0;

    for (int x = -pcf_count; x < pcf_count; x++)
        for (int y = -pcf_count; y < pcf_count; y++) {
            float shadow_distance = texture(map, pos.xy + vec2(float(x), float(y)) * texel_size).r;
            if (pos.z - bias > shadow_distance)
                total += 1.0;
        }

    return 1.0 - total / pcf_total_texels * pos.w;
}

#ifndef CONFIG_GLES
float shadow_factor_msaa(in sampler2DMSArray map, in vec4 pos, in float bias)
{
    float shadow_distance = 0;
    float total = 0.0;

    for (int i = 0; i < 4; i++)
        shadow_distance += texelFetch(map,
                                      ivec3(pos.xy * textureSize(map).x, 0), i).r;

    shadow_distance /= 4;
    if (pos.z - bias > shadow_distance)
        total += 1.0;

    total /= 4;

    return 1.0 - total * pos.w;
}
#endif /* CONFIG_GLES */

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

    float shadow_factor = 1.0;

    float light_dot = dot(unit_normal, normalize(-light_dir[0]));
    if (light_dot > 0.0) {
        float bias = max(0.0005 * (1.0 - light_dot), 0.0008);
#ifdef CONFIG_GLES
        shadow_factor = shadow_factor_pcf(shadow_map, shadow_pos, bias);
#else
        shadow_factor = shadow_factor_msaa(shadow_map, shadow_pos, bias);
#endif /* CONFIG_GLES */
    }

    vec3 total_diffuse = vec3(0.0);
    vec3 total_specular = vec3(0.0);

    for (int i = 0; i < 4; i++) {
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

    if (albedo_texture) {
        Albedo = vec4((normalize(cross(texture_sample.rgb, vec3(
            abs(fract(cos(mod(float(entity_hash), 3.1415926)))),
            abs(fract(sin(mod(float(entity_hash), 3.1415926)))),
            abs(fract(log(float(entity_hash))))
        ))) + vec3(1.0, 1.0, 1.0) / 2.0), 1.0);
    } else {
        vec3 pos_normal = (normalize(orig_normal) + vec3(1.0, 1.0, 1.0)) / 2.0;
        Albedo = vec4(pos_normal * (shadow_outline ? shadow_factor : 1.0), 1.0);
    }
}
