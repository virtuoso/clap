#version 460 core

#include "shader_constants.h"

layout (binding=SAMPLER_BINDING_light_map) uniform usampler2D light_map;

#include "ubo_bloom.glsl"
#include "ubo_outline.glsl"
#include "ubo_postproc.glsl"
#include "ubo_postproc.glsl"
#include "ubo_shadow.glsl"

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 EmissiveColor;
layout (location = 2) out vec4 EdgeNormal;
layout (location = 3) out vec4 EdgeDepthMask;
layout (location = 4) out vec4 ViewPosition;
layout (location = 5) out vec4 Normal;

layout(location = 0) in vec2 pass_tex;
layout (location = 1) in vec3 surface_normal;
layout (location = 2) in vec3 to_camera_vector;
layout (location = 3) in vec4 world_pos;
layout (location = 4) in flat int instance_id;

mat3 tbn = mat3(1.0);

#include "lighting.glsl"

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_emission_map) uniform sampler2D emission_map;

void main()
{
    vec3 unit_normal = normalize(surface_normal);
    vec3 view_dir = normalize(to_camera_vector);
    vec4 texture_sample = texture(model_tex, pass_tex);
    float alpha = 1.0;

    if (use_3d_fog) {
        // FragColor = vec4(vec3(gl_FragCoord.xyz), 1.0);
        // FragColor = vec4(vec3((world_pos.xyz + 50.0) * 0.01), 1.0);
        // FragColor = vec4(sample_noise3d(pass_tex.xyx, 0.2), 0.1);//vec4(r.diffuse, 1.0);//vec4(r.diffuse, 1.0) * texture_sample + vec4(r.specular, 1.0);
        // FragColor = vec4(sample_noise3d(world_pos.xyz, 3.0), 0.05);//vec4(r.diffuse, 1.0);//
        alpha = cloud_mask(pass_tex.xy, fog_3d_amp, fog_3d_scale);
        float fog = fog_cloud(world_pos.xyz, fog_3d_amp, fog_3d_scale);
        unit_normal = normalize(unit_normal + noise(world_pos.xyz) * (fog * 2.0 - 1.0));

        lighting_result r = compute_total_lighting(unit_normal, view_dir, fog_color, 1.0);

        // FragColor = vec4(vec3(gl_FragCoord.xyz), 1.0);
        // FragColor = vec4(vec3((world_pos.xyz + 50.0) * 0.01), 1.0);
        // FragColor = vec4(sample_noise3d(pass_tex.xyx, 0.2), 0.1);//vec4(r.diffuse, 1.0);//vec4(r.diffuse, 1.0) * texture_sample + vec4(r.specular, 1.0);
        // FragColor = vec4(sample_noise3d(world_pos.xyz, 3.0), 0.05);//vec4(r.diffuse, 1.0);//
        // float alpha = cloud_mask(pass_tex.xy, fog_3d_amp, fog_3d_scale) * fog_3d_amp;

        FragColor = vec4(r.diffuse * fog, alpha) + vec4(r.specular, 0.0);

        // FragColor = vec4(1.0, 0.0, 0.0, alpha);// + vec4(r.specular, 0.0);
        // FragColor = vec4((r.diffuse + r.specular), 1.0);
        Normal = vec4(unit_normal, 0.0);
        EmissiveColor = vec4(0.0);
    } else {
        FragColor = texture(model_tex, pass_tex);
        Normal = vec4(0.0);
        EmissiveColor = vec4(texture(emission_map, pass_tex).rgb * bloom_intensity, 1.0);
    }

    if (outline_exclude) {
        // EdgeNormal = vec4(vec3(0.0), 1.0 - float(instance_id) / 256.0);
        EdgeNormal = vec4(vec3(0.0), 0.0);
        // EdgeDepthMask = -1.0;
        EdgeDepthMask = vec4(0.0);//1.0;
    } else {
        EdgeNormal = vec4(0.0);
        EdgeDepthMask = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);//0.0;
    }
    ViewPosition = vec4(0.0);
}
