#version 460 core

#include "shader_constants.h"
#include "texel_fetch.inc"

uniform sampler2DMS model_tex;
uniform sampler2DMS normal_map;
const int numSamples = MSAA_SAMPLES;

layout (location=0) in vec2 pass_tex;
layout (location=0) out vec4 FragColor;

const ivec2 offsets[8] = ivec2[](
    ivec2(-1,  1), ivec2( 0,  1), ivec2( 1,  1),
    ivec2(-1,  0),               ivec2( 1,  0),
    ivec2(-1, -1), ivec2( 0, -1), ivec2( 1, -1)
);

void main() {
    ivec2 texSize = textureSize(normal_map);
    ivec2 texelCoord = ivec2(pass_tex.x * texSize.x, pass_tex.y * texSize.y); // Integer texel coordinates

    // Ensure we're not sampling out-of-bounds
    texelCoord = clamp(texelCoord, ivec2(1), texSize - ivec2(2));

    // Fetch averaged colors for Sobel
    float kernel[9];
    for (int i = 0; i < 8; i++) {
        kernel[i] = dot(texel_fetch_2dms(normal_map, pass_tex + offsets[i] / vec2(texSize)).rgb, vec3(0.299, 0.587, 0.114)); // Grayscale
    }
    kernel[4] = dot(texel_fetch_2dms(normal_map, pass_tex).rgb, vec3(0.299, 0.587, 0.114)); // Center pixel

    // Sobel operator
    float edgeX = kernel[2] + 2.0 * kernel[4] + kernel[7] - (kernel[0] + 2.0 * kernel[3] + kernel[5]);
    float edgeY = kernel[0] + 2.0 * kernel[1] + kernel[2] - (kernel[5] + 2.0 * kernel[6] + kernel[7]);

    float edge = sqrt(edgeX * edgeX + edgeY * edgeY);

    FragColor = vec4(1.0 - vec3(edge), 1.0);
}
