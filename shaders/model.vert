#version 460 core

#include "shader_constants.h"

layout (location=0) in vec3 position;
layout (location=1) in vec2 tex;
layout (location=2) in vec3 normal;
layout (location=3) in vec4 tangent;
layout (location=4) in vec4 joints;
layout (location=5) in vec4 weights;

layout (std140, binding = UBO_BINDING_lighting) uniform lighting {
    vec3 light_pos[LIGHTS_MAX];
    vec3 light_color[LIGHTS_MAX];
    vec3 light_dir[LIGHTS_MAX];
    vec3 attenuation[LIGHTS_MAX];
    bool light_directional[LIGHTS_MAX];
    int  nr_lights;
    vec3 light_ambient;
};

layout (std140, binding = UBO_BINDING_transform) uniform transform {
    mat4 trans;
};

layout (std140, binding = UBO_BINDING_projview) uniform projview {
    mat4 proj;
    mat4 view;
    mat4 inverse_view;
};

layout (std140, binding = UBO_BINDING_skinning) uniform skinning {
    int use_skinning;
    mat4 joint_transforms[JOINTS_MAX];
};

uniform int use_normals;

layout (location=0) flat out int do_use_normals;
layout (location=1) out vec2 pass_tex;
layout (location=2) out vec3 surface_normal;
layout (location=3) out vec3 orig_normal;
layout (location=4) out vec3 to_light_vector[LIGHTS_MAX];
layout (location=8) out vec3 to_camera_vector;
layout (location=9) out vec4 world_pos;

void main()
{
    world_pos = trans * vec4(position, 1.0);

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

        for (int i = 0; i < LIGHTS_MAX; i++) {
            to_light_vector[i] = to_tangent_space * (light_pos[i] - world_pos.xyz);
        }
        to_camera_vector = to_tangent_space * (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;
    } else {
        surface_normal = our_normal.xyz;

        for (int i = 0; i < LIGHTS_MAX; i++) {
            to_light_vector[i] = light_pos[i] - world_pos.xyz;
        }
        to_camera_vector = (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;
    }
}
