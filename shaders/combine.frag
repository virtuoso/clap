#version 330

layout (location=0) out vec4 FragColor;
in vec2 pass_tex;

uniform sampler2D model_tex;
uniform sampler2D normal_map;
uniform sampler2D emission_map;

void main()
{
    vec4 tex_color = texture(model_tex, pass_tex);
    vec4 highlight_color = texture(emission_map, pass_tex);

    FragColor = tex_color + highlight_color * 2.0;
}
