#version 460 core

layout (location=0) in vec2 pass_tex;

uniform vec4 in_color;
uniform float color_passthrough;

layout (location=0) out vec4 FragColor;

void main()
{
    vec4 tex_color;

    if (color_passthrough >= 0.6) {
        tex_color = in_color;
    } else {
        tex_color = vec4(1.0, 0.0, 0.0, 1.0);
        if (color_passthrough >= 0.4)
            tex_color.w = in_color.w;
    }

    FragColor = tex_color;
}
