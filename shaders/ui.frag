#version 460 core

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

uniform sampler2D model_tex;
uniform vec4 in_color;
uniform float color_passthrough;

void main()
{
    vec4 tex_color;

    if (color_passthrough >= 0.6) {
    	tex_color = in_color;
    } else {
        tex_color = texture(model_tex, pass_tex);
        if (color_passthrough >= 0.4)
            tex_color.w = in_color.w;
    }

    FragColor = tex_color;
}
