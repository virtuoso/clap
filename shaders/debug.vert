#version 330

in vec3 position;
in vec2 tex;

uniform mat4 proj;
uniform mat4 view;
// uniform mat4 inverse_view;
uniform mat4 trans;
out vec2 pass_tex;

void main()
{
    gl_Position = proj * view * trans * vec4(position, 1.0);
}
