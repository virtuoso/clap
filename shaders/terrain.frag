#version 460 core

#include "shader_constants.h"

layout (location=0) in vec2 pass_tex;
layout (location=1) in vec3 surface_normal;
layout (location=2) in vec3 to_light_vector;
layout (location=3) in vec3 to_camera_vector;

#include "ubo_lighting.glsl"

uniform sampler2D model_tex;
uniform float shine_damper;
uniform float reflectivity;

layout (location=0) out vec4 FragColor;

#define PI 3.1415926538

void main()
{

    vec3 unit_normal;

    unit_normal = normalize(surface_normal);

    /* XXX: factor out lighting calculation from model.frag, the below is bitrotting */
    vec3 unit_to_light_vector = normalize(to_light_vector);
    float n_dot1 = dot(unit_normal, unit_to_light_vector);
    float brightness = max(n_dot1, 0.2);
    vec3 diffuse = brightness * light_color[0];
    vec3 unit_to_camera_vector = normalize(to_camera_vector);
    vec3 light_direction = -unit_to_light_vector;
    vec3 reflected_light_direction = reflect(light_direction, unit_normal);
    float specular_factor = dot(reflected_light_direction, unit_to_camera_vector);
    specular_factor = max(specular_factor, 0.2);
    float damped_factor = pow(specular_factor, shine_damper);
    vec3 final_specular = damped_factor * reflectivity * light_color[0];

    float grassness = dot(unit_normal, vec3(0.0, 1.0, 0.0));
    vec2 texcoords = vec2(mod(pass_tex.x, 0.5), mod(pass_tex.y, 0.5));
    vec4 grass_sample = texture(model_tex, texcoords);
    vec4 rock_sample = texture(model_tex, vec2(texcoords.x + 0.5, texcoords.y + 0.5));
    float fac = grassness * grassness * grassness * grassness;
    vec4 mix = grass_sample * fac + rock_sample * (1.0 - fac);

    FragColor = vec4(diffuse, 1.0) * mix + vec4(final_specular, 1.0);
}
