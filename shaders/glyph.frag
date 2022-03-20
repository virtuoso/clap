#version 330

uniform vec4 in_color;
uniform sampler2D model_tex;
uniform float color_passthrough;

layout (location=0) out vec4 FragColor;
in vec2 pass_tex;

void main()
{
    vec4 tex_color = texture(model_tex, pass_tex);

    if (color_passthrough > 0.6 && length(tex_color) > 0.1) {
    	tex_color = in_color;
    } else if (color_passthrough > 0.4 && length(tex_color) > 0.1) {
        tex_color.w = tex_color.w * in_color.w;
    }

    FragColor = tex_color;
}
