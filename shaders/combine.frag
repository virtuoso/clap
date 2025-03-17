#version 460 core

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

uniform sampler2D model_tex;
uniform sampler2D emission_map;
uniform sampler2D sobel_tex;

void main()
{
    vec4 tex_color = texture(model_tex, pass_tex);
    vec4 highlight_color = texture(emission_map, pass_tex);
    vec4 sobel = texture(sobel_tex, pass_tex);

    FragColor = tex_color + highlight_color * 2.0;
    float factor = sobel.x;
    FragColor = vec4(mix(FragColor.xyz, vec3(0.0), 1 - factor), 1.0);
}
