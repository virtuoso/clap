#version 460 core

#include "shader_constants.h"
#include "texel_fetch.glsl"
#include "edge_filter.glsl"

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2DMS model_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2DMS normal_map;

layout (location=0) in vec2 pass_tex;
layout (location=0) out vec4 FragColor;

#include "ubo_postproc.glsl"

const ivec2 offsets[8] = ivec2[](
    ivec2(-1,  1), ivec2( 0,  1), ivec2( 1,  1),
    ivec2(-1,  0),               ivec2( 1,  0),
    ivec2(-1, -1), ivec2( 0, -1), ivec2( 1, -1)
);

float depth_fetch(sampler2DMS map, vec2 coords, float near_plane, float far_plane)
{
    return linearize_depth(texel_fetch_2dms(map, coords).r, near_plane, far_plane);
}

void main() {
    ivec2 texSize = textureSize(normal_map);
    ivec2 texelCoord = ivec2(pass_tex.x * texSize.x, pass_tex.y * texSize.y); // Integer texel coordinates

    // Ensure we're not sampling out-of-bounds
    texelCoord = clamp(texelCoord, ivec2(1), texSize - ivec2(2));

    float laplacian_edge =
        4.0 * depth_fetch(model_tex, pass_tex, near_plane, far_plane) -
        depth_fetch(model_tex, pass_tex + ivec2(1, 0) / vec2(texSize), near_plane, far_plane) -
        depth_fetch(model_tex, pass_tex + ivec2(-1, 0) / vec2(texSize), near_plane, far_plane) -
        depth_fetch(model_tex, pass_tex + ivec2(0, 1) / vec2(texSize), near_plane, far_plane) -
        depth_fetch(model_tex, pass_tex + ivec2(0, -1) / vec2(texSize), near_plane, far_plane);
    laplacian_edge = pow(laplacian_edge, 0.9);

    // Fetch averaged colors for Sobel
    float kernel[9];
    for (int i = 0; i < 8; i++) {
        kernel[i] = normals_fetch(normal_map, pass_tex + offsets[i] / vec2(texSize));
    }
    kernel[4] = normals_fetch(normal_map, pass_tex); // Center pixel

    // Sobel operator
    float edgeX = kernel[2] + 2.0 * kernel[4] + kernel[7] - (kernel[0] + 2.0 * kernel[3] + kernel[5]);
    float edgeY = kernel[0] + 2.0 * kernel[1] + kernel[2] - (kernel[5] + 2.0 * kernel[6] + kernel[7]);

    float edge = sqrt(edgeX * edgeX + edgeY * edgeY);
    float final_edge = max(edge, laplacian_edge);

    FragColor = vec4(1.0 - vec3(final_edge), 1.0);
}
