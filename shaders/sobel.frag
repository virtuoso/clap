#version 460 core

// Sobel Edge Detection Filter
// GLSL Fragment Shader
// Implementation by Patrick Hebron
// Lifted from https://gist.github.com/Hebali/6ebfc66106459aacee6a9fac029d0115

layout (location=0) in vec2 pass_tex;
uniform sampler2D	model_tex;
uniform float 		width;
uniform float 		height;

layout (location=0) out vec4 FragColor;

void make_kernel(inout vec4 n[9], sampler2D tex, vec2 coord)
{
	float w = 1.0 / width;
	float h = 1.0 / height;

	n[0] = texture(tex, coord + vec2( -w, -h));
	n[1] = texture(tex, coord + vec2(0.0, -h));
	n[2] = texture(tex, coord + vec2(  w, -h));
	n[3] = texture(tex, coord + vec2( -w, 0.0));
	n[4] = texture(tex, coord);
	n[5] = texture(tex, coord + vec2(  w, 0.0));
	n[6] = texture(tex, coord + vec2( -w, h));
	n[7] = texture(tex, coord + vec2(0.0, h));
	n[8] = texture(tex, coord + vec2(  w, h));
}

void main(void)
{
	vec4 n[9];
	make_kernel(n, model_tex, pass_tex.st);

	vec4 sobel_edge_h = n[2] + (2.0*n[5]) + n[8] - (n[0] + (2.0*n[3]) + n[6]);
  	vec4 sobel_edge_v = n[0] + (2.0*n[1]) + n[2] - (n[6] + (2.0*n[7]) + n[8]);
	vec4 sobel = sqrt((sobel_edge_h * sobel_edge_h) + (sobel_edge_v * sobel_edge_v));

	FragColor = vec4( 1.0 - sobel.rgb, 1.0 );
}
