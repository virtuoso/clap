#version 460 core

layout (location=0) in vec3 position;

uniform mat4 proj;
uniform mat4 view;
uniform mat4 trans;

void main()
{
    gl_Position = proj * view * trans * vec4(position, 1.0);
}
