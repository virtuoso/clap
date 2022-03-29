#version 330

in vec3 position;
in vec2 tex;

uniform mat4 trans;
uniform float width;

out vec2 pass_tex;
out vec2 blur_coords[11];

void main()
{
    gl_Position = /*trans * */vec4(position, 1.0);
    pass_tex = tex;
    vec2 center_coords = position.xy * 0.5 + 0.5;
    float pixsz = 1.0 / width;
    for (int i = -5; i <= 5; i++)
        blur_coords[i + 5] = center_coords + vec2(pixsz * float(i), 0.0);
}