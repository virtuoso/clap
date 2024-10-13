#version 330

in vec3 position;
in vec2 tex;

uniform mat4 trans;
uniform float height;

out vec2 pass_tex;
out vec2 blur_coords[11];

void main()
{
    gl_Position = vec4(position, 1.0);
    pass_tex = tex;
    float pixsz = 1.0 / height;
    for (int i = -5; i <= 5; i++)
        blur_coords[i + 5] = vec2(0.0, pixsz * float(i));
}
