#version 460 core

#include "shader_constants.h"

layout(location = ATTR_LOC_POSITION) in vec3 position;
layout(location = ATTR_LOC_TEX) in vec2 tex;
layout(location = ATTR_LOC_NORMAL) in vec3 normal;

layout(location = 0) out vec2 pass_tex;
layout (location = 1) out vec3 surface_normal;
layout (location = 2) out vec3 to_camera_vector;
layout (location = 3) out vec4 world_pos;

layout (std140, binding = UBO_BINDING_particles) uniform particles {
    vec3 particle_pos[PARTICLES_MAX];
};

#include "ubo_transform.glsl"
#include "ubo_projview.glsl"

void main()
{
    mat4 _trans = trans;
    _trans[3][0] = particle_pos[gl_InstanceIndex][0];
    _trans[3][1] = particle_pos[gl_InstanceIndex][1];
    _trans[3][2] = particle_pos[gl_InstanceIndex][2];

    world_pos = _trans * vec4(position, 1.0);
    gl_Position = proj * view * world_pos;

    // world_pos = vec4(particle_pos[gl_InstanceIndex] + position, 1.0);
    surface_normal = mat3(_trans) * normal.xyz;
    to_camera_vector = (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;

    pass_tex = tex;
}
