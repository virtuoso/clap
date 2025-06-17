#ifndef SHADERS_LIGHTING_GLSL
#define SHADERS_LIGHTING_GLSL

#include "ubo_lighting.glsl"
#include "ubo_material.glsl"

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

lighting_result compute_total_lighting(vec3 unit_normal, vec3 to_light_vector[LIGHTS_MAX], vec3 view_dir)
{
    lighting_result r = lighting_result(vec3(0.0), vec3(0.0));
    // vec3 total_diffuse = vec3(0.0);
    // vec3 total_specular = vec3(0.0);

    for (int i = 0; i < nr_lights; i++) {
        lighting_result l = compute_blinn_phong(i, unit_normal, to_light_vector[i], view_dir);
        r.specular += l.specular;
        r.diffuse += l.diffuse;
    }

    r.diffuse += light_ambient;

    return r;
}

#endif /* SHADERS_UBO_LIGHTING_GLSL */
