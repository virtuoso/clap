#version 460 core

#include "shader_constants.h"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 tex;

layout(location = 0) out vec2 pass_tex;

layout (std140, binding = UBO_BINDING_particles) uniform particles {
    vec3 particle_pos[PARTICLES_MAX];
};

#include "ubo_transform.glsl"

layout (std140, binding = UBO_BINDING_projview) uniform projview {
    mat4 proj;
    mat4 view;
    mat4 inverse_view;
};

void main()
{
    mat4 _trans = trans;
    _trans[3][0] = particle_pos[gl_InstanceID][0];
    _trans[3][1] = particle_pos[gl_InstanceID][1];
    _trans[3][2] = particle_pos[gl_InstanceID][2];
    gl_Position = proj * view * _trans * vec4(position, 1.0);
    pass_tex = tex;
}
