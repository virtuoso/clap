#version 460 core

#include "shader_constants.h"
#include "texel_fetch.inc"

uniform sampler2D model_tex;

layout (location=0) in vec2 pass_tex;
layout (location=0) out vec4 weights;

void main()
{
    float kernel[9];
    for (int y = -1, i = 0; y <= 1; y++)
        for (int x = -1; x <= 1; x++, i++)
            kernel[i] = 1 - texel_fetch_2d(model_tex, pass_tex, ivec2(x, y)).r;

    float center = kernel[4];
    if (center < 1e-3) {
        weights = vec4(0.0);
        return;
    }

    // Directional continuity check
    float left_strength   = max(center + kernel[0] + kernel[6] - 3.0 * kernel[3], 0.0);
    float right_strength  = max(center + kernel[2] + kernel[8] - 3.0 * kernel[5], 0.0);
    float bottom_strength = max(center + kernel[0] + kernel[2] - 3.0 * kernel[1], 0.0);
    float top_strength    = max(center + kernel[6] + kernel[8] - 3.0 * kernel[7], 0.0);

    float Gx = -kernel[0] - 2.0 * kernel[3] - kernel[6] +
                kernel[2] + 2.0 * kernel[5] + kernel[8];
    float Gy = -kernel[0] - 2.0 * kernel[1] - kernel[2] +
                kernel[6] + 2.0 * kernel[7] + kernel[8];

    float edge_angle = atan(Gy, Gx); // Range: [-π, π]

    weights.x = left_strength;
    weights.y = right_strength;
    weights.z = top_strength;
    weights.w = bottom_strength;

    float dir_weight = abs(cos(edge_angle)); /* Horizontal edges get stronger left/right weights */
    weights.x *= dir_weight;
    weights.y *= dir_weight;

    dir_weight = abs(sin(edge_angle)); /* Vertical edges get stronger top/bottom weights */
    weights.z *= dir_weight;
    weights.w *= dir_weight;

    weights.x = max(weights.x, max(0.0, Gx));
    weights.y = max(weights.y, max(0.0, -Gx));
    weights.z = max(weights.z, max(0.0, -Gy));
    weights.w = max(weights.w, max(0.0, Gy));
}
