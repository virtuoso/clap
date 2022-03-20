#version 330

in vec3 position;
in vec2 tex;

uniform mat4 trans;
uniform mat4 proj;
// uniform vec4 color;
// uniform float color_passthrough;

out vec2 pass_tex;
// varying vec4 color_override;
// varying float color_pt;

void main()
{
    gl_Position = /*proj * */trans * vec4(position, 1.0);
    pass_tex = tex;
    // color_override = color;
    // color_pt = color_passthrough;
}
