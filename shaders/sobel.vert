#version 330

in vec3 position;
in vec2 tex;

uniform mat4 trans;

out vec2 pass_tex;

void main()
{
    gl_Position = trans * vec4(position, 1.0);
    pass_tex = tex;
}
