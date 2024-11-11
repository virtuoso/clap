#version 460 core

layout (location=0) in vec3 position;
layout (location=1) in vec2 tex;

uniform mat4 trans;
uniform float height;

layout (location=0) out vec2 pass_tex;
layout (location=1) out vec2 blur_coords[11];

void main()
{
    gl_Position = vec4(position, 1.0);
    pass_tex = tex;
    float pixsz = 1.0 / height;
    for (int i = -5; i <= 5; i++)
        blur_coords[i + 5] = vec2(0.0, pixsz * float(i));
}
