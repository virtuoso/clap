#ifndef SHADERS_LIGHTING_GLSL
#define SHADERS_LIGHTING_GLSL

#include "ubo_lighting.glsl"
#include "ubo_material.glsl"
#include "ubo_transform.glsl"
#include "noise.glsl"

struct lighting_result {
    vec3    diffuse;
    vec3    specular;
};

lighting_result compute_blinn_phong(int idx, vec3 unit_normal, vec3 to_light_vector, vec3 view_dir)
{
    float distance = light_directional[idx] ? 1.0 : length(to_light_vector);
    float att_fac = light_directional[idx] ? 1.0 : 1.0 / max(attenuation[idx].x + (attenuation[idx].y * distance) + (attenuation[idx].z * distance * distance), 0.001);
    vec3 unit_light_dir = normalize(light_directional[idx] ? -light_dir[idx] : to_light_vector);

    vec3 half_vector = normalize(unit_light_dir + view_dir);
    vec3 specular = vec3(0.0), diffuse = vec3(0.0);

    float n_dot_l = dot(unit_normal, unit_light_dir);
    if (n_dot_l > 0.0) {
        float brightness = max(n_dot_l, 0.0);

        float spec_angle = max(dot(unit_normal, half_vector), 0.0);
        float specular_strength = pow(spec_angle, shine_damper);

        specular = light_color[idx] * specular_strength * reflectivity * att_fac;
        diffuse = light_color[idx] * brightness * att_fac;
    }

    return lighting_result(diffuse, specular);
}

#define PI 3.1415926538

lighting_result compute_cook_torrance(int idx, vec3 unit_normal, vec3 to_light_vector, vec3 view_dir,
                                      vec3 base_color)
{
    float distance = light_directional[idx] ? 1.0 : length(to_light_vector);
    float att_fac = light_directional[idx] ? 1.0 : 1.0 / max(attenuation[idx].x + (attenuation[idx].y * distance) + (attenuation[idx].z * distance * distance), 0.001);
    vec3 l = normalize(light_directional[idx] ? -light_dir[idx] : to_light_vector);

    vec3 h = normalize(l + view_dir);

    float n_dot_l = max(dot(unit_normal, l), 0.0);
    float n_dot_v = max(dot(unit_normal, view_dir), 0.0);
    float n_dot_h = max(dot(unit_normal, h), 0.0);
    float v_dot_h = max(dot(view_dir, h), 0.0);

    mat4 inv_trans = inverse(trans);
    vec4 local_pos = inv_trans * world_pos;
    vec3 roughness_noise_src = local_pos.xyz * roughness_scale;
    vec3 metallic_noise_src = shared_scale ? roughness_noise_src : local_pos.xyz * metallic_scale;

    float roughness_noise = fbm(roughness_noise_src, roughness_amp, roughness_oct);
    float metallic_noise = 0.0;
    switch (metallic_mode) {
        case MAT_METALLIC_ROUGHNESS:
            metallic_noise = roughness_noise;
            break;
        case MAT_METALLIC_ONE_MINUS_ROUGHNESS:
            metallic_noise = 1.0 - roughness_noise;
            break;
        default:
        case MAT_METALLIC_INDEPENDENT:
            metallic_noise = fbm(metallic_noise_src, metallic_amp, metallic_oct);
            break;
    }

    float perc_roughness = roughness_oct > 0 ?
        mix(roughness, roughness_ceil, roughness_noise) :
        roughness;
    float perc_metallic = metallic_oct > 0 ?
        mix(metallic, metallic_ceil, metallic_noise) :
        metallic;

    float alpha = clamp(perc_roughness * perc_roughness, 0.05, 0.98);

    /* GGX normal distribution */
    float alpha_2 = alpha * alpha;
    float denom = max(n_dot_h * n_dot_h * (alpha_2 - 1.0) + 1.0, 1e-5);
    float d = alpha_2 / (PI * denom * denom);

    /* Schlick Fresnel approximation */
    vec3 f0 = mix(vec3(0.04), base_color, perc_metallic);
    vec3 f = clamp(f0 + (1.0 - f0) * pow(1.0 - v_dot_h, 5.0), 1e-5, 1.0);

    /* Smith-GGX geometry function */
    float k = (alpha + 1.0) * (alpha + 1.0) / 8.0;
    float g_v = n_dot_v / (n_dot_v * (1.0 - k) + k);
    float g_l = n_dot_l / (n_dot_l * (1.0 - k) + k);
    float g = max(g_v * g_l, 1e-5);

    vec3 numerator = d * g * f;
    float denominator = 4.0 * max(n_dot_l * n_dot_v, 0.001);
    vec3 specular = numerator / denominator;

    /* Energy conservation: scale diffuse by (1 - specular reflectance) */
    vec3 kd = (1.0 - f) * (1.0 - metallic);
    vec3 diffuse = kd * base_color / PI;

    lighting_result ret;
    ret.diffuse = diffuse * n_dot_l * light_color[idx] * att_fac;
    ret.specular = specular * n_dot_l * light_color[idx] * att_fac;

    return ret;
}

lighting_result compute_total_lighting(vec3 unit_normal, vec3 to_light_vector[LIGHTS_MAX], vec3 view_dir,
                                       vec3 base_color, float shadow_factor)
{
    lighting_result r = lighting_result(vec3(0.0), vec3(0.0));
    vec3 shadow_tinted = light_color[0] * shadow_tint;

    for (int i = 0; i < nr_lights; i++) {
        // lighting_result l = compute_blinn_phong(i, unit_normal, to_light_vector[i], view_dir);
        lighting_result l = compute_cook_torrance(i, unit_normal, to_light_vector[i], view_dir, base_color);

        /* XXX: shadow casting light source is 0 */
        if (i == 0) {
            l.diffuse = mix(l.diffuse, shadow_tinted, 1.0 - shadow_factor);
            if (shadow_factor < 1.0)
                l.specular = vec3(0.0);
        }

        r.specular += l.specular;
        r.diffuse += l.diffuse;
    }

    r.diffuse += mix(light_ambient, shadow_tinted, 1.0 - shadow_factor);

    return r;
}

#endif /* SHADERS_UBO_LIGHTING_GLSL */
