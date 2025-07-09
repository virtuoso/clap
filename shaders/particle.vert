#version 460 core

#include "shader_constants.h"

layout(location = ATTR_LOC_POSITION) in vec3 position;
layout(location = ATTR_LOC_TEX) in vec2 tex;

layout(location = 0) out vec2 pass_tex;

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
    gl_Position = proj * view * _trans * vec4(position, 1.0);
    pass_tex = tex;
}
