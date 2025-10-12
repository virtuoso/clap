#version 460 core

#include "config.h"
#include "shader_constants.h"

layout (location=ATTR_LOC_POSITION) in vec3 position;
layout (location=ATTR_LOC_TEX) in vec2 tex;
layout (location=ATTR_LOC_JOINTS) in vec4 joints;
layout (location=ATTR_LOC_WEIGHTS) in vec4 weights;

#include "ubo_transform.glsl"
#include "ubo_projview.glsl"
#include "ubo_skinning.glsl"

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
#ifndef CONFIG_SHADOW_MAP_ARRAY
    gl_Position = proj * view * total_pos;
#else
    gl_Position = total_pos;
#endif /* CONFIG_SHADOW_MAP_ARRAY */
}
