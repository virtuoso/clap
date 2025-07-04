#version 460 core

#include "shader_constants.h"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2D normal_map;
layout (binding=SAMPLER_BINDING_sobel_tex) uniform sampler2D sobel_tex;

#include "ubo_projview.glsl"
#include "ubo_postproc.glsl"

// Linearize z-buffer depth (view space)
float linearize_depth(float depth)
{
    float z = depth * 2.0 - 1.0; // NDC
    return (2.0 * near_plane * far_plane) / (far_plane + near_plane - z * (far_plane - near_plane));
}

void main()
{
    // View-space position reconstruction
    float depth = texture(model_tex, pass_tex).r;
    float ndc_z = 2.0 * depth - 1.0;
    float view_depth = linearize_depth(depth);

    vec2 noise_uv = pass_tex * ssao_noise_scale;
    vec3 random_vec = normalize(texture(sobel_tex, noise_uv).xyz * 2.0 - 1.0);

    vec3 normal = normalize(texture(normal_map, pass_tex).rgb * 2.0 - 1.0);
    vec4 clip_space = vec4(pass_tex * 2.0 - 1.0, ndc_z, 1.0);
    vec4 view_pos = inverse(proj) * clip_space;
    view_pos /= view_pos.w;
    vec3 pos = view_pos.xyz;

    // Create TBN
    vec3 tangent = normalize(random_vec - normal * dot(random_vec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float depth_scale = clamp(pos.z / far_plane, 0.1, 1.0);
    float bias = mix(0.01, 0.05, 1.0 - depth_scale);

    float occlusion = 0.0;

    float scale = clamp(1.0 - pos.z / far_plane, 0.1, 1.0);
    float actual_radius = ssao_radius * scale;

    for (int i = 0; i < SSAO_KERNEL_SIZE; ++i) {
        vec3 sample_vec = TBN * ssao_kernel[i];
        vec3 sample_pos = pos + sample_vec * actual_radius;

        vec4 offset = proj * vec4(sample_pos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        float sample_depth = texture(model_tex, offset.xy).r;
        float sample_ndc_z = sample_depth * 2.0 - 1.0;
        vec4 sample_clip = vec4(offset.xy * 2.0 - 1.0, sample_ndc_z, 1.0);
        vec4 sample_view = inverse(proj) * sample_clip;
        sample_view /= sample_view.w;

        float range_check = smoothstep(0.0, 1.0, actual_radius / abs(pos.z - sample_view.z));
        if (sample_view.z >= sample_pos.z + bias)
            occlusion += range_check;
    }

    occlusion = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
    FragColor = vec4(vec3(occlusion), 1.0);
}
