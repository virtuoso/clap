#version 330

in vec2 pass_tex;
in vec3 surface_normal;
in vec3 to_light_vector;
in vec3 to_camera_vector;
in float color_override;
in float do_use_normals;
in vec4 pass_tangent;

uniform sampler2D model_tex;
uniform sampler2D normal_map;
uniform vec3 light_color;
uniform float shine_damper;
uniform float reflectivity;
uniform vec4 highlight_color;

layout (location=0) out vec4 FragColor;
// out vec4 FragColor;

void main()
{
    if (highlight_color.w != 0.0) {
        FragColor = highlight_color;
        return;
    }
    if (color_override == 1.0) {
        FragColor = vec4(0.5, 1.0, 1.0, 1.0);
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
    vec4 texture_sample = texture(model_tex, pass_tex);
    FragColor = vec4(diffuse, 1.0) * texture_sample + vec4(final_specular, 1.0);
    //gl_FragColor.rgb = pow(gl_FragColor.rgb, vec3(1.0/2.2));
    // gl_FragColor = vec4(pass_tangent.xyz, 1);
    // gl_FragColor = pass_tangent;
}
