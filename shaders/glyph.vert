#version 460 core

layout (location=0) in vec3 position;
layout (location=1) in vec2 tex;

uniform mat4 trans;
uniform mat4 proj;

layout (location=0) out vec2 pass_tex;

void main()
{
    gl_Position = /*proj * */trans * vec4(position, 1.0);
    pass_tex = tex;
}