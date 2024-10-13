#version 330

layout (location=0) out vec4 FragColor;
in vec2 pass_tex;
in vec2 blur_coords[11];

uniform sampler2D model_tex;

void main()
{

    FragColor = vec4(0.0);
    FragColor += texture(model_tex, pass_tex + blur_coords[0]) * 0.0093;
    FragColor += texture(model_tex, pass_tex + blur_coords[1]) * 0.028002;
    FragColor += texture(model_tex, pass_tex + blur_coords[2]) * 0.065984;
    FragColor += texture(model_tex, pass_tex + blur_coords[3]) * 0.121703;
    FragColor += texture(model_tex, pass_tex + blur_coords[4]) * 0.175713;
    FragColor += texture(model_tex, pass_tex + blur_coords[5]) * 0.198596;
    FragColor += texture(model_tex, pass_tex + blur_coords[6]) * 0.175713;
    FragColor += texture(model_tex, pass_tex + blur_coords[7]) * 0.121703;
    FragColor += texture(model_tex, pass_tex + blur_coords[8]) * 0.065984;
    FragColor += texture(model_tex, pass_tex + blur_coords[9]) * 0.028002;
    FragColor += texture(model_tex, pass_tex + blur_coords[10]) * 0.0093;
}
