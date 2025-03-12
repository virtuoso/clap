#version 460 core

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

uniform sampler2D model_tex;

void main()
{
    vec2 texelSize = 1.0 / textureSize(model_tex, 0);
    
    vec4 color = texture(model_tex, pass_tex) * 4 +
                 texture(model_tex, pass_tex + vec2(-texelSize.x, -texelSize.y)) +
                 texture(model_tex, pass_tex + vec2( texelSize.x, -texelSize.y)) +
                 texture(model_tex, pass_tex + vec2(-texelSize.x,  texelSize.y)) +
                 texture(model_tex, pass_tex + vec2( texelSize.x,  texelSize.y));

    FragColor = color * 0.125; // Gaussian filter average
}
