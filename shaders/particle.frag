#version 460 core

#include "shader_constants.h"

layout (location = RT_MODEL_LIGHTING)       out vec4 FragColor;
layout (location = RT_MODEL_EMISSION)       out vec4 EmissiveColor;
layout (location = RT_MODEL_VIEW_NORMALS)   out vec4 Normal;

layout(location = 0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;

void main()
{
    FragColor = vec4(texture(model_tex, pass_tex).rgb, 1.0);
    EmissiveColor = vec4(texture(emission_map, pass_tex).rgb, 1.0);
    Normal = vec4(0.0);
}
