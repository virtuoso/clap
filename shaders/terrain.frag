#version 330

in vec2 pass_tex;
in vec3 surface_normal;
in vec3 to_light_vector;
in vec3 to_camera_vector;
in float do_use_normals;
in vec4 pass_tangent;

uniform sampler2D model_tex;
uniform vec3 light_color;
uniform float shine_damper;
uniform float reflectivity;

layout (location=0) out vec4 FragColor;
// out vec4 FragColor;
#define PI 3.1415926538

void main()
{

    vec3 unit_normal;

    unit_normal = normalize(surface_normal);

    vec3 unit_to_light_vector = normalize(to_light_vector);
    float n_dot1 = dot(unit_normal, unit_to_light_vector);
    float brightness = max(n_dot1, 0.2);
    vec3 diffuse = brightness * light_color;
    vec3 unit_to_camera_vector = normalize(to_camera_vector);
    vec3 light_direction = -unit_to_light_vector;
    vec3 reflected_light_direction = reflect(light_direction, unit_normal);
    float specular_factor = dot(reflected_light_direction, unit_to_camera_vector);
    specular_factor = max(specular_factor, 0.2);
    float damped_factor = pow(specular_factor, shine_damper);
    vec3 final_specular = damped_factor * reflectivity * light_color;

    float grassness = dot(unit_normal, vec3(0.0, 1.0, 0.0));
    vec2 texcoords = vec2(mod(pass_tex.x, 0.5), mod(pass_tex.y, 0.5));
    vec4 grass_sample = texture(model_tex, texcoords);
    vec4 rock_sample = texture(model_tex, vec2(texcoords.x + 0.5, texcoords.y + 0.5));
    float fac = grassness * grassness * grassness * grassness;
    vec4 mix = grass_sample * fac + rock_sample * (1.0 - fac);

    FragColor = vec4(diffuse, 1.0) * mix + vec4(final_specular, 1.0);
    //gl_FragColor.rgb = pow(gl_FragColor.rgb, vec3(1.0/2.2));
    // gl_FragColor = vec4(pass_tangent.xyz, 1);
    // gl_FragColor = pass_tangent;
}
