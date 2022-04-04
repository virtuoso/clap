#version 330

in vec3 position;
in vec2 tex;
in vec3 normal;
in vec4 tangent;

uniform vec3 light_pos;
uniform mat4 proj;
uniform mat4 view;
uniform mat4 inverse_view;
uniform mat4 trans;

out vec2 pass_tex;
out vec3 surface_normal;
out vec3 to_light_vector;
out vec3 to_camera_vector;
out float color_override;

void main()
{
    vec4 world_pos = trans * vec4(position, 1.0);

    vec4 our_normal = vec4(normal, 0);
    gl_Position = proj * view * trans * vec4(position, 1.0);
    pass_tex = tex;

    // this is still needed in frag
    surface_normal = (trans * vec4(our_normal.xyz, 0.0)).xyz;

    to_light_vector = light_pos - world_pos.xyz;
    to_camera_vector = (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;
}
