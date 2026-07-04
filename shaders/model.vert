#version 460 core

#include "shader_constants.h"

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

#include "ubo_lighting.glsl"
#include "ubo_transform.glsl"
#include "ubo_projview.glsl"
#include "ubo_skinning.glsl"
#include "ubo_material.glsl"

layout (location=0) out vec2 pass_tex;
layout (location=1) out vec3 surface_normal;
layout (location=2) out vec3 orig_normal;
layout (location=3) out vec3 to_camera_vector;
layout (location=4) out vec4 world_pos;
#ifdef SHADER_VARIANT_NORMALS
layout (location=5) out mat3 tbn;
/* the mat3 above occupies locations 5-7 */
#endif /* SHADER_VARIANT_NORMALS */
#ifdef SHADER_VARIANT_TAA
layout (location=8) out vec4 pass_curr_clip;
layout (location=9) out vec4 pass_prev_clip;
#endif /* SHADER_VARIANT_TAA */

void main()
{
    world_pos = trs * vec4(position, 1.0);

    vec4 our_normal = vec4(normal, 0);
    orig_normal = our_normal.xyz;

    vec4 total_local_pos = vec4(0, 0, 0, 0);
    vec4 total_normal = vec4(0, 0, 0, 0);
#ifdef SHADER_VARIANT_SKINNING
    if (use_skinning != 0) {
        for (int i = 0; i < 4; i++) {
            mat4 joint_transform = joint_transforms[joints[i]];
            vec4 local_pos = joint_transform * vec4(position, 1.0);
            total_local_pos += local_pos * weights[i];

            vec4 world_normal = joint_transform * vec4(normal, 0.0);
            total_normal += world_normal * weights[i];
        }

        gl_Position = proj * view * trs * total_local_pos;
        our_normal = total_normal;
    } else
#endif /* SHADER_VARIANT_SKINNING */
    {
        gl_Position = proj * view * trs * vec4(position, 1.0);
    }

#ifdef SHADER_VARIANT_TAA
    /* rigid approximation for skinned models: current pose, previous TRS */
#ifdef SHADER_VARIANT_SKINNING
    vec4 local_pos = use_skinning != 0 ? total_local_pos : vec4(position, 1.0);
#else
    vec4 local_pos = vec4(position, 1.0);
#endif /* !SHADER_VARIANT_SKINNING */
    pass_curr_clip = gl_Position;
    pass_prev_clip = prev_proj_view * prev_trs * local_pos;
#endif /* SHADER_VARIANT_TAA */

    pass_tex = tex;

    mat3 trs_rot = mat3(trs);

    // this is still needed in frag
#ifdef SHADER_VARIANT_NORMALS
    if (use_normals) {
        surface_normal = our_normal.xyz;

        vec3 N = normalize(trs_rot * surface_normal);
        vec3 T = normalize(trs_rot * tangent.xyz).xyz;
        vec3 B = normalize(cross(N, T) * tangent.w);

        mat3 view_rot = mat3(view);
        T = normalize(view_rot * T);
        B = normalize(view_rot * B);
        N = normalize(view_rot * N);
        tbn = mat3(T, B, N);

        to_camera_vector = tbn * (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;
    } else
#endif /* SHADER_VARIANT_NORMALS */
    {
        surface_normal = trs_rot * our_normal.xyz;

        to_camera_vector = (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;
    }
}
