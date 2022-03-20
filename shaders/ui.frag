#version 330

layout (location=0) out vec4 FragColor;
in vec2 pass_tex;
// varying vec4 color_override;
// varying float color_pt;

uniform sampler2D model_tex;
uniform vec4 in_color;
uniform float color_passthrough;

/*void mux(out float o, in float x, in float y, in float a);
void mux(out float o, in float x, in float y, in float a)
{
    float f = (1. - cos(a*3.1415927)) / 2.;
    o = x * (1. - f) + y * f;
}*/

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

    // float vignette = pass_tex.x * pass_tex.y * (1.-pass_tex.x) * (1.-pass_tex.y);
    // float o = (1. - cos(vignette * 3.1415926) / 2.);
    // //mux(o, 0, 1, vignette);
    // tex_color.rgb *= o;
    // if ((pass_tex.x - 10.) * (pass_tex.x - 10.) + (pass_tex.y + 10.) * (pass_tex.y + 10.) < 100.)
    //     tex_color = vec4(0, 0, 0, 0);
    FragColor = tex_color;
}
