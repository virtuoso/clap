#version 330

layout (location=0) out vec4 FragColor;
in vec2 pass_tex;

uniform sampler2D model_tex;
uniform sampler2D normal_map;
uniform sampler2D emission_map;
uniform sampler2D sobel_tex;

void main()
{
    vec4 tex_color = texture(model_tex, pass_tex);
    vec4 highlight_color = texture(emission_map, pass_tex);
    vec4 sobel = texture(sobel_tex, pass_tex);

    FragColor = tex_color + highlight_color * 2.0;
    float factor = length(sobel.xyz) / 1.75;
    if (factor < 0.95)
        FragColor = vec4(FragColor.xyz * (pow(factor, 2.0)), 1.0);
        // FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
