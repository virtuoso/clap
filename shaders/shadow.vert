#version 460 core

#include "shader_constants.h"

/*
 * The attribute declarations must match model.vert's exactly (same set,
 * same variant gates): a mesh's interleaved vertex buffer is laid out
 * once, and every prog that draws the mesh reinterprets it through its
 * own reflected input list -- a mismatched set means a mismatched
 * stride/offsets and garbage vertices (and a wedged V3D binner).
 */
layout (location=ATTR_LOC_POSITION) in vec3 position;
layout (location=ATTR_LOC_TEX) in vec2 tex;
layout (location=ATTR_LOC_NORMAL) in vec3 normal;
#ifdef SHADER_VARIANT_NORMALS
layout (location=ATTR_LOC_TANGENT) in vec4 tangent;
#endif /* SHADER_VARIANT_NORMALS */
#ifdef SHADER_VARIANT_SKINNING
layout (location=ATTR_LOC_JOINTS) in uvec4 joints;
layout (location=ATTR_LOC_WEIGHTS) in vec4 weights;
#endif /* SHADER_VARIANT_SKINNING */

#include "ubo_transform.glsl"
#include "ubo_projview.glsl"
/* declared iff used, see model.frag */
#ifdef SHADER_VARIANT_SKINNING
#include "ubo_skinning.glsl"
#endif /* SHADER_VARIANT_SKINNING */

void main()
{
    vec4 total_local_pos = vec4(0.0);
    vec4 total_pos;

#ifdef SHADER_VARIANT_SKINNING
    if (use_skinning != 0) {
        for (int i = 0; i < 4; i++) {
            mat4 joint_transform = joint_transforms[joints[i]];
            vec4 local_pos = joint_transform * vec4(position, 1.0);
            total_local_pos += local_pos * weights[i];
        }

        total_pos = trs * total_local_pos;
    } else
#endif /* SHADER_VARIANT_SKINNING */
    {
        total_pos = trs * vec4(position, 1.0);
    }
#ifndef SHADER_SHADOW_MAP_ARRAY
    gl_Position = proj * view * total_pos;
#else
    gl_Position = total_pos;
#endif /* SHADER_SHADOW_MAP_ARRAY */
}
