#version 460 core

#include "shader_constants.h"
#include "pass-tex.glsl"
#include "view_pos.glsl"

layout (location=0) out float FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_depth_tex) uniform sampler2D depth_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2D normal_map;
layout (binding=SAMPLER_BINDING_sobel_tex) uniform sampler2D sobel_tex;

#include "ubo_projview.glsl"
#include "ubo_postproc.glsl"

void main()
{
    vec2 noise_uv = pass_tex * ssao_noise_scale;
    vec3 random_vec = vec3(normalize(texture(sobel_tex, noise_uv).xy), 0.0);

    vec3 normal_sample = texture(normal_map, pass_tex).xyz;
    vec3 normal = normalize(normal_sample * 2.0 - 1.0);
    vec3 pos = view_pos_from_depth(depth_tex, inverse_proj, pass_tex);
    float dz = fwidth(pos.z);
    FragColor = 1.0;
    if (pos.z > 0.0)    return;

    // Create TBN
    vec3 tangent = normalize(random_vec - normal * dot(random_vec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // Z in view space is always negative
    float bias = max(0.002, 2.0 * dz);
    bias *= mix(1.0, 4.0, 1.0 - abs(normal.z));

    float occlusion = 0.0;

    float scale = clamp(1.0 + pos.z / far_plane, 0.1, 1.0);
    float actual_radius = ssao_radius * scale;

    for (int i = 0; i < SSAO_KERNEL_SIZE; ++i) {
        vec3 sample_vec = TBN * ssao_kernel[i];
        vec3 sample_pos = pos + sample_vec * actual_radius;

        vec4 offset = proj * vec4(sample_pos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        vec3 sample_view = view_pos_from_depth(depth_tex, inverse_proj, convert_pass_tex(offset.xy));
        if (sample_view.z > 0.0)    continue;

        float range_check = 1.0 - clamp(abs(pos.z - sample_view.z) / scale, 0.0, 1.0);
        if (sample_view.z >= sample_pos.z + bias)
            occlusion += range_check;
    }

    FragColor = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
}
