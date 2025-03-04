#version 460 core

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

uniform sampler2D model_tex;
uniform sampler2D emission_map;
// uniform float intensity; // For optional blending

void main()
{
    vec4 blurred = texture(model_tex, pass_tex);
    FragColor = mix(texture(emission_map, pass_tex), blurred, 0.8/*intensity*/);
}
