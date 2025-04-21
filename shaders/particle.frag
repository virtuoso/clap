#version 460 core

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 EmissiveColor;
layout (location = 2) out vec4 Albedo;
layout (location = 3) out float Depth;
layout (location = 4) out vec4 Gbuffer;

layout(location = 0) in vec2 pass_tex;

uniform sampler2D model_tex;
uniform sampler2D emission_map;

void main()
{
    FragColor = vec4(texture(model_tex, pass_tex).rgb, 1.0);
    EmissiveColor = vec4(texture(emission_map, pass_tex).rgb, 1.0);
    Albedo = vec4(0.0);
    Depth = 0.0;
    Gbuffer = vec4(0.0);
}
