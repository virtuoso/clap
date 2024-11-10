#version 330

in vec2 pass_tex;
in vec3 surface_normal;
in vec3 orig_normal;
in vec3 to_light_vector[4];
in vec3 to_camera_vector;
flat in int do_use_normals;
in vec4 pass_tangent;

uniform sampler2D model_tex;
uniform sampler2D normal_map;
uniform sampler2D emission_map;
uniform vec3 light_color[4];
uniform vec3 attenuation[4];
uniform float shine_damper;
uniform float reflectivity;
uniform bool albedo_texture;
uniform int entity_hash;
uniform vec4 highlight_color;

layout (location=0) out vec4 FragColor;
layout (location=1) out vec4 EmissiveColor;
layout (location=2) out vec4 Albedo;

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
        // /*gl_*/FragColor = normal_vec * pass_tangent;
        // return;
    } else {
        unit_normal = normalize(surface_normal);
    }

    vec3 unit_to_camera_vector = normalize(to_camera_vector);
    vec4 texture_sample = texture(model_tex, pass_tex);
    EmissiveColor = texture(emission_map, pass_tex);

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

    FragColor = vec4(max(total_diffuse, 0.2), 1.0) * texture_sample + vec4(total_specular, 1.0);

    if (albedo_texture) {
        Albedo = vec4((normalize(cross(texture_sample.rgb, vec3(
            abs(fract(cos(mod(float(entity_hash), 3.1415926)))),
            abs(fract(sin(mod(float(entity_hash), 3.1415926)))),
            abs(fract(log(float(entity_hash))))
        ))) + vec3(1.0, 1.0, 1.0) / 2.0), 1.0);
    } else {
        vec3 pos_normal = (normalize(orig_normal) + vec3(1.0, 1.0, 1.0)) / 2.0;
        Albedo = vec4(pos_normal, 1.0);
    }
}
