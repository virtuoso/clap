#version 330

in vec3 position;
in vec2 tex;
in vec3 normal;
in vec4 tangent;
in vec4 joints;
in vec4 weights;

uniform vec3 light_pos[4];
uniform mat4 proj;
uniform mat4 view;
uniform mat4 inverse_view;
uniform mat4 trans;
uniform int use_normals;
uniform int use_skinning;
uniform mat4 joint_transforms[100];

flat out int do_use_normals;
out vec2 pass_tex;
out vec3 surface_normal;
out vec3 orig_normal;
out vec3 to_light_vector[4];
out vec3 to_camera_vector;
out float color_override;

void main()
{
    vec4 world_pos = trans * vec4(position, 1.0);

    vec4 our_normal = vec4(normal, 0);
    orig_normal = our_normal.xyz;

    vec4 total_local_pos = vec4(0, 0, 0, 0);
    vec4 total_normal = vec4(0, 0, 0, 0);
    if (use_skinning != 0) {
        for (int i = 0; i < 4; i++) {
            mat4 joint_transform = joint_transforms[int(joints[i])];
            vec4 local_pos = joint_transform * vec4(position, 1.0);
            total_local_pos += local_pos * weights[i];

            vec4 world_normal = joint_transform * vec4(normal, 0.0);
            total_normal += world_normal * weights[i];
        }

        gl_Position = proj * view * trans * total_local_pos;
        our_normal = trans * total_normal;
    } else {
        gl_Position = proj * view * trans * vec4(position, 1.0);
    }
    pass_tex = tex;

    // this is still needed in frag
    if (use_normals != 0) {
        do_use_normals = use_normals;
        surface_normal = (view * vec4(our_normal.xyz, 0.0)).xyz;

        vec3 N = normalize(surface_normal);
        vec3 T = normalize(view * vec4(tangent.xyz, 0)).xyz;
        vec3 B = normalize(cross(N, T));
        mat3 to_tangent_space = mat3(
            T.x, B.x, N.x,
            T.y, B.y, N.y,
            T.z, B.z, N.z
        );

        for (int i = 0; i < 4; i++) {
            to_light_vector[i] = to_tangent_space * (light_pos[i] - world_pos.xyz);
        }
        to_camera_vector = to_tangent_space * (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;
    } else {
        surface_normal = our_normal.xyz;

        for (int i = 0; i < 4; i++) {
            to_light_vector[i] = light_pos[i] - world_pos.xyz;
        }
        to_camera_vector = (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;
    }
}
