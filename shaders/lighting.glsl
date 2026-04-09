#ifndef SHADERS_LIGHTING_GLSL
#define SHADERS_LIGHTING_GLSL

#include "ubo_lighting.glsl"
#include "ubo_material.glsl"
#include "ubo_transform.glsl"
#include "ubo_postproc.glsl"
#include "noise.glsl"

struct lighting_result {
    vec3    diffuse;
    vec3    specular;
};

struct lighting_material {
    float   metallic;
    float   roughness;
};

lighting_material noise_material()
{
    vec4 local_pos = inverse_trs * world_pos;
    vec3 roughness_noise_src = local_pos.xyz * roughness_scale;
    vec3 metallic_noise_src = shared_scale ? roughness_noise_src : local_pos.xyz * metallic_scale;

    float roughness_noise = fbm(roughness_noise_src, roughness_amp, roughness_oct, 2.0);
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
            metallic_noise = fbm(metallic_noise_src, metallic_amp, metallic_oct, 2.0);
            break;
    }

    lighting_material ret;
    ret.roughness = roughness_oct > 0 ?
        mix(roughness, roughness_ceil, roughness_noise) :
        roughness;
    ret.metallic = metallic_oct > 0 ?
        mix(metallic, metallic_ceil, metallic_noise) :
        metallic;

    return ret;
}

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
                                      vec3 base_color, lighting_material mat)
{
    float distance = light_directional[idx] ? 1.0 : length(to_light_vector);
    float att_fac = light_directional[idx] ? 1.0 : 1.0 / max(attenuation[idx].x + (attenuation[idx].y * distance) + (attenuation[idx].z * distance * distance), 0.001);
    vec3 l = normalize(light_directional[idx] ? -light_dir[idx] : to_light_vector);

    vec3 h = normalize(l + view_dir);

    float n_dot_l = max(dot(unit_normal, l), 0.0);
    float n_dot_v = max(dot(unit_normal, view_dir), 0.0);
    float n_dot_h = max(dot(unit_normal, h), 0.0);
    float v_dot_h = max(dot(view_dir, h), 0.0);

    float alpha = clamp(mat.roughness * mat.roughness, 0.05, 0.98);

    /* GGX normal distribution */
    float alpha_2 = alpha * alpha;
    float denom = max(n_dot_h * n_dot_h * (alpha_2 - 1.0) + 1.0, 1e-5);
    float d = alpha_2 / (PI * denom * denom);

    /* Schlick Fresnel approximation */
    vec3 f0 = mix(vec3(0.04), base_color, mat.metallic);
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

float light_heatmap()
{
    uvec4 mask = texelFetch(light_map, ivec2(gl_FragCoord.x, height - gl_FragCoord.y) / TILE_WIDTH, 0);

    float heat = 0.0;
    for (uint c = 0; c < 4; c++) {
        uint q = mask[c];
        while (q != 0u) {
            if ((q & 1u) != 0u)
                heat += 0.2;
            q >>= 1u;
        }
    }

    return heat;
}

lighting_result compute_total_lighting(vec3 unit_normal, vec3 view_dir, vec3 base_color, float shadow_factor,
                                       lighting_material mat)
{
    lighting_result r = lighting_result(vec3(0.0), vec3(0.0));
    vec3 shadow_tinted = light_color[0] * shadow_tint;

    uvec4 mask = texelFetch(light_map, ivec2(gl_FragCoord.x, height - gl_FragCoord.y) / TILE_WIDTH, 0);

    for (uint c = 0, off = 0; c < 4; c++) {
        while (mask[c] != 0u) {
            bool set = (mask[c] & 1u) == 1;
            mask[c] >>= 1;
            uint i = off++;
            if (!set)   continue;

            vec3 to_light_vector = light_pos[i] - world_pos.xyz;
            if (use_normals)    to_light_vector = tbn * to_light_vector;

            lighting_result l = compute_cook_torrance(int(i), unit_normal, to_light_vector, view_dir, base_color, mat);

            /* XXX: shadow casting light source is 0 */
            if (i == 0) {
                if (length(l.diffuse) > length(shadow_tinted))
                    l.diffuse = mix(l.diffuse, shadow_tinted, 1.0 - shadow_factor);
                if (shadow_factor < 1.0)
                    l.specular = vec3(0.0);
            }

            r.specular += l.specular;
            r.diffuse += l.diffuse;
        }
    }

    r.diffuse += mix(light_ambient, shadow_tinted, 1.0 - shadow_factor);

    return r;
}

#endif /* SHADERS_UBO_LIGHTING_GLSL */
