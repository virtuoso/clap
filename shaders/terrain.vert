#version 460 core

#include "shader_constants.h"

layout (location=0) in vec3 position;
layout (location=1) in vec2 tex;
layout (location=2) in vec3 normal;
layout (location=3) in vec4 tangent;

#include "ubo_lighting.glsl"
#include "ubo_transform.glsl"
#include "ubo_projview.glsl"

layout (location=0) out vec2 pass_tex;
layout (location=1) out vec3 surface_normal;
layout (location=2) out vec3 to_light_vector;
layout (location=3) out vec3 to_camera_vector;

void main()
{
    vec4 world_pos = trans * vec4(position, 1.0);

    vec4 our_normal = vec4(normal, 0);
    gl_Position = proj * view * trans * vec4(position, 1.0);
    pass_tex = tex;

    // this is still needed in frag
    surface_normal = (trans * vec4(our_normal.xyz, 0.0)).xyz;

    /* XXX: factor out lighting from the model shader into a common file */
    to_light_vector = light_pos[0] - world_pos.xyz;
    to_camera_vector = (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;
}
