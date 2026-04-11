#version 460 core

#include "shader_constants.h"

layout(location = ATTR_LOC_POSITION) in vec3 position;
layout(location = ATTR_LOC_TEX) in vec2 tex;
layout(location = ATTR_LOC_NORMAL) in vec3 normal;

layout(location = 0) out vec2 pass_tex;
layout(location = 1) out vec3 surface_normal;
layout(location = 2) out vec3 to_camera_vector;
layout(location = 3) out vec4 world_pos;

layout (std140, binding = UBO_BINDING_particles) uniform particles {
    vec3 particle_pos[PARTICLES_MAX];
};

#include "ubo_transform.glsl"
#include "ubo_projview.glsl"

void main()
{
    /*
     * trs is already billboarded by particles_update(): the
     * upper-left 3x3 is the inverse of the view rotation so the
     * quad faces the camera. We swap in the per-instance particle
     * position as the translation column.
     */
    mat4 _trs = trs;
    _trs[3][0] = particle_pos[gl_InstanceIndex][0];
    _trs[3][1] = particle_pos[gl_InstanceIndex][1];
    _trs[3][2] = particle_pos[gl_InstanceIndex][2];

    world_pos = _trs * vec4(position, 1.0);
    gl_Position = proj * view * world_pos;

    surface_normal = mat3(_trs) * normal;
    to_camera_vector = (inverse_view * vec4(0.0, 0.0, 0.0, 1.0) - world_pos).xyz;

    pass_tex = tex;
}
