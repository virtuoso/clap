#version 330

layout (location=0) out vec4 FragColor;
in vec2 pass_tex;

uniform sampler2D model_tex;
uniform vec4 in_color;
uniform float color_passthrough;

const float contrast = 0.3;

void main()
{
    vec4 tex_color;

    // color_passthrough = 2.;
    if (color_passthrough >= 0.6) {
        tex_color = in_color;
    } else {
        tex_color = texture(model_tex, pass_tex);
        if (color_passthrough >= 0.4)
            tex_color.w = in_color.w;
    }

    tex_color.rgb = (tex_color.rgb - 0.5) * (1.0 + contrast) + 0.5;
    // float vignette = pass_tex.x * pass_tex.y * (1.-pass_tex.x) * (1.-pass_tex.y);
    // float o = (1. - cos(vignette * 3.1415926) / 2.);
    // //mux(o, 0, 1, vignette);
    // tex_color.rgb *= o;
    // if ((pass_tex.x - 10.) * (pass_tex.x - 10.) + (pass_tex.y + 10.) * (pass_tex.y + 10.) < 100.)
    //     tex_color = vec4(0, 0, 0, 0);
    FragColor = tex_color;
}