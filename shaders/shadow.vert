#version 460 core

layout (location=0) in vec3 position;
layout (location=1) in vec2 tex;
layout (location=2) in vec4 joints;
layout (location=3) in vec4 weights;

uniform mat4 trans;
uniform mat4 view;
uniform mat4 proj;
uniform int use_skinning;
uniform mat4 joint_transforms[100];

void main()
{
    vec4 total_local_pos = vec4(0.0);
    if (use_skinning != 0) {
        for (int i = 0; i < 4; i++) {
            mat4 joint_transform = joint_transforms[int(joints[i])];
            vec4 local_pos = joint_transform * vec4(position, 1.0);
            total_local_pos += local_pos * weights[i];
        }

        gl_Position = proj * view * trans * total_local_pos;
    } else {
        gl_Position = proj * view * trans * vec4(position, 1.0);
    }
}
