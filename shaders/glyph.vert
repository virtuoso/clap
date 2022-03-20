#version 330

in vec3 position;
in vec2 tex;

uniform mat4 trans;
uniform mat4 proj;

out vec2 pass_tex;

void main()
{
    gl_Position = /*proj * */trans * vec4(position, 1.0);
    pass_tex = tex;
}