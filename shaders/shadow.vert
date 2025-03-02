#version 460 core

#include "config.h"
#include "shader_constants.h"

layout (location=0) in vec3 position;
layout (location=1) in vec2 tex;
layout (location=2) in vec4 joints;
layout (location=3) in vec4 weights;

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

void main()
{
    vec4 total_local_pos = vec4(0.0);
    vec4 total_pos;

    if (use_skinning != 0) {
        for (int i = 0; i < 4; i++) {
            mat4 joint_transform = joint_transforms[int(joints[i])];
            vec4 local_pos = joint_transform * vec4(position, 1.0);
            total_local_pos += local_pos * weights[i];
        }

        total_pos = trans * total_local_pos;
    } else {
        total_pos = trans * vec4(position, 1.0);
    }
#ifdef CONFIG_GLES
    gl_Position = proj * view * total_pos;
#else
    gl_Position = total_pos;
#endif /* CONFIG_GLES */
}
