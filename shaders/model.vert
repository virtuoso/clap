#version 330

in vec3 position;
in vec2 tex;
in vec3 normal;
in vec4 tangent;
in vec4 joints;
in vec4 weights;

uniform vec3 ray;
uniform vec3 light_pos;
uniform mat4 proj;
uniform mat4 view;
uniform mat4 inverse_view;
uniform mat4 trans;
uniform float use_normals;
uniform float use_skinning;
uniform mat4 joint_transforms[50];

out float do_use_normals;
out vec2 pass_tex;
out vec3 surface_normal;
out vec3 to_light_vector;
out vec3 to_camera_vector;
out float color_override;

void main()
{
    vec4 world_pos = trans * vec4(position, 1.0);
    color_override = 0.0;
    if (ray.z > 0.5) {
        if (pow(world_pos.x - ray.x, 2.0) + pow(world_pos.z - ray.y, 2.0) <= 64.0)
            color_override = 1.0;
    }

    vec3 our_normal = normal;
    vec4 total_local_pos = vec4(0, 0, 0, 0);
    vec4 total_normal = vec4(0, 0, 0, 0);
    if (use_skinning > 0.5) {
        for (int i = 0; i < 4; i++) {
            mat4 joint_transform = joint_transforms[int(joints[i])];
            // joint_transform = mat4(1.0);
            vec4 local_pos = joint_transform * vec4(position, 1.0);
            total_local_pos += local_pos * weights[i];
            // total_local_pos += vec4(position, 1.0) * weights[i];

            vec4 world_normal = joint_transform * vec4(normal, 0.0);
            total_normal += world_normal * weights[i];
        }
        // total_local_pos += vec4(position, 1.0) * 0.6;
        // total_normal += vec4(normal, 0.0) * 0.6;

        gl_Position = proj * /*view * trans * */total_local_pos;
        our_normal = total_normal.xyz;
    } else {
        gl_Position = proj * view * trans * vec4(position, 1.0);
    }
    pass_tex = tex;

    // this is still needed in frag
    if (use_normals > 0.5) {
        do_use_normals = use_normals;
        surface_normal = (view * vec4(our_normal, 0.0)).xyz;

        vec3 N = normalize(surface_normal);
        vec3 T = normalize(view * vec4(tangent.xyz, 0)).xyz;
        vec3 B = normalize(cross(N, T));
        mat3 to_tangent_space = mat3(
            T.x, B.x, N.x,
            T.y, B.y, N.y,
            T.z, B.z, N.z
        );

        to_light_vector = to_tangent_space * (light_pos - world_pos.xyz);
        to_camera_vector = to_tangent_space * (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;
    } else {
        surface_normal = (trans * vec4(our_normal, 0.0)).xyz;

        to_light_vector = light_pos - world_pos.xyz;
        to_camera_vector = (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;
    }
}
